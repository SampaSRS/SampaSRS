#pragma once

#include <sampasrs/root_fix.hpp>

#include <sampasrs/decoder.hpp>
#include <sampasrs/utils.hpp>

#include "RtypesCore.h"
#include "TApplication.h"
#include "TRootCanvas.h"
#include "TSystem.h"
#include "TText.h"
#include <TCanvas.h>
#include <TGraph.h>
#include <TH1.h>
#include <TH1F.h>
#include <boost/circular_buffer.hpp>
#include <fmt/core.h>
#include <tins/tins.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>

namespace sampasrs {

// Helper class to pass data between different threads
template <typename T>
class FIFO {
  public:
  FIFO() = default;

  explicit FIFO(size_t buffer_size, size_t min_output, size_t max_output)
  {
    config(buffer_size, min_output, max_output);
  }

  void config(size_t buffer_size, size_t min_output, size_t max_output)
  {
    m_buffer.set_capacity(buffer_size);
    m_min_output = min_output;
    m_max_output = max_output;
    m_output.reserve(max_output);
  }

  // Retrieve elements from the buffer
  [[nodiscard]] std::vector<T>& get(long timeout_milliseconds = 100)
  {
    m_output.clear();

    std::unique_lock<std::mutex> lock(m_mutex);
    // Wait until the buffer has enough elements, then acquire the lock
    m_buffer_ready.wait_for(lock, std::chrono::milliseconds(timeout_milliseconds), [&] {
      return m_buffer.size() >= m_min_output;
    });

    if (!m_buffer.empty()) {
      // Move elements to output container
      const size_t output_payloads = std::min(m_buffer.size(), m_max_output);
      for (int i = 0; i < output_payloads; ++i) {
        m_output.emplace_back(std::move(m_buffer.front()));
        m_buffer.pop_front();
      }
    }
    return m_output;
  }

  const boost::circular_buffer<T>& get_buffer() const { return m_buffer; };

  void put(T&& element)
  {
    {
      std::lock_guard<std::mutex> lock(m_mutex);
      m_buffer.push_back(std::move(element));
    }
    m_buffer_ready.notify_all();
  }

  size_t size() const { return m_buffer.size(); }
  size_t capacity() const { return m_buffer.capacity(); }
  bool enable() const { return capacity() != 0; }

  private:
  boost::circular_buffer<T> m_buffer {};
  std::vector<T> m_output {};
  std::condition_variable m_buffer_ready {};
  std::mutex m_mutex {};
  size_t m_min_output {};
  size_t m_max_output {};
};

// Network sniffer and raw data store
class Acquisition {
  using sc = std::chrono::steady_clock;
  using fast_clock = std::chrono::high_resolution_clock;

  public:
  explicit Acquisition(const std::string& file_prefix, bool save_raw = true, const std::string& fec_address = "10.0.0.2")
      : m_file_prefix(file_prefix)
      , m_fec_address(fec_address)
      , m_save_raw(save_raw)
  {
  }

  void run()
  {
    // Define data pipeline and buffers
    FIFO<Payload> reader_buffer {};
    FIFO<Event> decoder_buffer {};
    FIFO<Payload> tmp_payload_buffer {};
    FIFO<Event> tmp_event_buffer {};

    std::vector<std::thread> pipeline;
    if (m_save_raw) {
      reader_buffer.config(2000000, 50, 10000);
      tmp_payload_buffer.config(100000, 50, 10000);
      decoder_buffer.config(100, 10, 100);
      pipeline.emplace_back(&Acquisition::writer_task<Payload>, this, std::ref(reader_buffer), std::ref(tmp_payload_buffer));
      pipeline.emplace_back(&Acquisition::decoder_task, this, std::ref(tmp_payload_buffer), std::ref(decoder_buffer));
      // pipeline.emplace_back(&Acquisition::monitor_task, this, std::ref(decoder_buffer));
    } else {
      reader_buffer.config(100000, 50, 10000);
      decoder_buffer.config(100000, 10, 1000);
      tmp_event_buffer.config(10000, 10, 1000);
      pipeline.emplace_back(&Acquisition::decoder_task, this, std::ref(reader_buffer), std::ref(decoder_buffer));
      pipeline.emplace_back(&Acquisition::writer_task<Event>, this, std::ref(decoder_buffer), std::ref(tmp_event_buffer));
      // pipeline.emplace_back(&Acquisition::monitor_task, this, std::ref(tmp_event_buffer));
    }

    std::thread reader(&Acquisition::reader_task, this, std::ref(reader_buffer));
    // std::thread logger(&Acquisition::logger_task, this);

    // for (;;) {
    //   const auto key = std::cin.get();
    //   if (key == 'x') {
    //     m_is_running = false;
    //     fmt::print("Exiting\n");
    //     break;
    //   }
    // }

    if (m_save_raw) {
      monitor_task(decoder_buffer);
    } else {
      monitor_task(tmp_event_buffer);
    }

    // logger.join();

    for (auto& thread : pipeline) {
      if (thread.joinable()) {
        thread.join();
      }
    }

    // TODO: find a proper way to break the sniffer loop
    // Send packet to unblock the sniffer's loop
    Tins::PacketSender sender;
    auto pkt = IP("10.0.0.3", m_fec_address) / UDP(6006) / RawPDU("tchau");
    sender.send(pkt);
    reader.join();
  }

  private:
  struct ReadStats {
    size_t bytes = 0;
    size_t packets = 0;
    size_t buffer_items = 0;
    size_t buffer_size = 0;
    std::chrono::steady_clock::duration total_time {};
    std::chrono::steady_clock::duration process_time {};
  };

  struct DecodeStats {
    size_t bytes = 0;
    size_t valid_events = 0;
    size_t total_events = 0;
    std::chrono::steady_clock::duration total_time {};
    std::chrono::steady_clock::duration process_time {};
  };

  struct WriteStats {
    size_t bytes = 0;
    size_t buffer_items = 0;
    size_t buffer_size = 0;
    std::chrono::steady_clock::duration total_time {};
    std::chrono::steady_clock::duration process_time {};
  };

  struct Stats {
    ReadStats read {};
    DecodeStats decode {};
    WriteStats write {};
    std::chrono::steady_clock::time_point last {};

    float read_speed {};   // in MB
    float write_speed {};  // in MB
    float decode_speed {}; // in MB

    float packet_rate {};         // packets / second
    float total_event_rate {};    // events / second
    float valid_event_rate {};    // events / seconds
    float invalid_event_ratio {}; // %

    float read_buffer_use {};  // %
    float write_buffer_use {}; // %

    float read_load {};   // %
    float decode_load {}; // %
    float write_load {};  // %

    size_t total_packets {}; // received network packets
    size_t valid_events {};
    size_t total_events {};
  };

  void update_stats()
  {
    const auto now = std::chrono::steady_clock::now();
    const auto dt = std::chrono::duration<float>(now - m_stats.last).count();

    // Current stats value
    const auto read = m_read_stats;
    const auto write = m_write_stats;
    const auto decode = m_decoder_stats;

    // Update derived stats
    static constexpr float to_mb = 1.f / 1024.f / 1024.f;
    static constexpr float eps = 1e-6;
    m_stats.read_speed = static_cast<float>(read.bytes - m_stats.read.bytes) / dt * to_mb;
    m_stats.write_speed = static_cast<float>(write.bytes - m_stats.write.bytes) / dt * to_mb;
    m_stats.decode_speed = static_cast<float>(decode.bytes - m_stats.decode.bytes) / dt * to_mb;

    m_stats.packet_rate = static_cast<float>(read.packets - m_stats.read.packets) / dt;
    m_stats.valid_event_rate = static_cast<float>(decode.valid_events - m_stats.decode.valid_events) / dt;
    m_stats.total_event_rate = static_cast<float>(decode.total_events - m_stats.decode.total_events) / dt;
    m_stats.invalid_event_ratio = static_cast<float>(decode.total_events - decode.valid_events) / (static_cast<float>(decode.total_events) + eps) * 100.f;

    m_stats.read_buffer_use = static_cast<float>(m_stats.read.buffer_items) / (static_cast<float>(m_stats.read.buffer_size) + eps) * 100.f;
    m_stats.write_buffer_use = static_cast<float>(m_stats.write.buffer_items) / (static_cast<float>(m_stats.write.buffer_size) + eps) * 100.f;

    m_stats.read_load = std::chrono::duration<float>(read.process_time - m_stats.read.process_time).count()
        / (std::chrono::duration<float>(read.total_time - m_stats.read.total_time).count() + eps) * 100.f;
    m_stats.decode_load = std::chrono::duration<float>(decode.process_time - m_stats.decode.process_time).count()
        / (std::chrono::duration<float>(decode.total_time - m_stats.decode.total_time).count() + eps) * 100.f;
    m_stats.write_load = std::chrono::duration<float>(write.process_time - m_stats.write.process_time).count()
        / (std::chrono::duration<float>(write.total_time - m_stats.write.total_time).count() + eps) * 100.f;

    m_stats.total_packets = read.packets;
    m_stats.valid_events = decode.valid_events;
    m_stats.total_events = decode.total_events;

    // Update cache
    m_stats.read = read;
    m_stats.write = write;
    m_stats.decode = decode;
    m_stats.last = now;
  }

  void reader_task(FIFO<Payload>& output)
  {
    // Find interface do listen
    NetworkInterface iface;
    if (m_fec_address.empty()) {
      iface = NetworkInterface::default_interface().name();
    } else {
      IPv4Address to_resolve(m_fec_address);
      iface = NetworkInterface(to_resolve).name();
    }
    std::wcout << "Listening to interface: " << iface.friendly_name() << "\n";

    // Sniff on interface
    SnifferConfiguration config;
    static const char* filter = "udp port 6006 and dst host 10.0.0.3";
    config.set_filter(filter);
    Sniffer sniffer(iface.name(), config);
    sniffer.set_timeout(10);

    m_read_stats.buffer_size = output.capacity();

    while (m_is_running) {
      const auto start = std::chrono::steady_clock::now();
      Packet packet = sniffer.next_packet();
      const auto start_process = std::chrono::steady_clock::now();

      if (!packet) {
        // Error reading packet
        continue;
      }
      m_read_stats.bytes += packet.pdu()->size();

      Payload payload(std::move(packet));
      ++m_read_stats.packets;

      output.put(std::move(payload));

      m_read_stats.buffer_items = output.size();

      const auto end = std::chrono::steady_clock::now();
      m_read_stats.total_time += end - start;
      m_read_stats.process_time += end - start_process;
    }
  }

  void decoder_task(FIFO<Payload>& input, FIFO<Event>& output)
  {
    auto event_handler = [&](Event&& event) {
      ++m_decoder_stats.total_events;
      m_decoder_stats.bytes += event.byte_size();

      if (event.valid()) {
        ++m_decoder_stats.valid_events;
        output.put(std::move(event));
      }
    };

    EventSorter sorter(event_handler);
    sorter.enable_header_fix = false;
    sorter.enable_remove_caca = true;
    sorter.process_invalid_events = true;

    while (m_is_running) {
      const auto start = std::chrono::steady_clock::now();
      auto& payloads = input.get();
      const auto start_process = std::chrono::steady_clock::now();

      for (auto& payload : payloads) {
        sorter.process(payload);
      }

      const auto end = std::chrono::steady_clock::now();
      m_decoder_stats.total_time += end - start;
      m_decoder_stats.process_time += end - start_process;
    }
  }

  std::string next_file_name(std::string_view extension = "raw")
  {
    auto file_name = fmt::format("{}-{:04d}.{}", m_file_prefix, m_file_count, extension);
    ++m_file_count;
    return file_name;
  }

  template <typename T>
  std::string next_file_name()
  {
    if constexpr (std::is_same<T, Payload>::value) {
      return next_file_name("raw");
    }
    return next_file_name("rawev");
  }

  template <typename T>
  void writer_task(FIFO<T>& input, FIFO<T>& output)
  {
    m_write_stats.buffer_size = input.capacity();
    const size_t max_file_size = size_t(2) << 30U; // ~2 GB in byte
    auto file_size = std::numeric_limits<unsigned int>::max();

    std::ofstream file;

    while (m_is_running) {
      // Create a new file if the previous one exceeded the max size
      if (file_size > max_file_size) {
        if (file.is_open()) {
          file.close();
        }
        file_size = 0;

        auto file_name = next_file_name<T>();
        fmt::print("Writing to {}\n", file_name);
        file.open(file_name, std::ios::binary);
        if (!file) {
          throw std::runtime_error("Unable to create output file.");
        }
      }

      const auto start = std::chrono::steady_clock::now();
      auto& data = input.get();
      const auto start_process = std::chrono::steady_clock::now();

      if (data.empty()) {
        continue;
      }

      auto writing_timer = fast_clock::now();
      for (const auto& x : data) {

        auto data_size = x.byte_size();
        m_write_stats.bytes += data_size;
        file_size += data_size;

        // Write to file
        x.write(file);
      }

      if (output.enable()) {
        for (auto& x : data) {
          output.put(std::move(x));
        }
      }

      m_write_stats.buffer_items = input.size();

      const auto end = std::chrono::steady_clock::now();
      m_write_stats.total_time += end - start;
      m_write_stats.process_time += end - start_process;
    }

    fmt::print("Writing buffer to file...\n");
    // Write remaining payloads in the buffer
    for (const auto& x : input.get_buffer()) {
      x.write(file);
    }

    fmt::print("Done\n");
  }

  static int channel_id(const Event& event, size_t waveform)
  {
    auto header = event.get_header(waveform);
    constexpr int sampa_channels = 32;
    return sampa_channels * header.sampa_addr() + header.channel_addr();
  }

  void monitor_task(FIFO<Event>& input)
  {
    TApplication app("app", nullptr, nullptr);

    TCanvas canvas("SampaSRS", "SampaSRS", 1400, 800);
    TPad text_pad("info_pad", "The pad 20% of the height", 0.0, 0.9, 1.0, 1.0);
    TPad graph_pad("graph_pad", "The pad 80% of the height", 0.0, 0.0, 1.0, 0.9);
    graph_pad.Divide(3);
    text_pad.Draw();
    graph_pad.Draw();

    graph_pad.cd(1);
    constexpr int adc_resolution = 1024;
    TH1F energy_hist("energy_hist", "Energy Spectrum", adc_resolution, 0, adc_resolution);
    energy_hist.GetXaxis()->SetTitle("ADC");
    energy_hist.GetYaxis()->SetTitle("Count");
    energy_hist.SetStats(false);
    energy_hist.SetDirectory(nullptr);
    energy_hist.Draw();

    graph_pad.cd(2);
    int min_channel = 0;
    int max_channel = 128;
    TH1F channels_hist("channels_hits", "Channel Hits", max_channel - min_channel, min_channel, max_channel);
    channels_hist.GetXaxis()->SetTitle("Channel");
    channels_hist.GetYaxis()->SetTitle("Count");
    channels_hist.SetStats(false);
    channels_hist.SetDirectory(nullptr);
    channels_hist.Draw();

    graph_pad.cd(3);
    bool draw_waveform = false;
    constexpr int max_words = 1002;
    TGraph waveform_graph(max_words);
    waveform_graph.SetMaximum(1023);
    waveform_graph.SetTitle("Waveform");
    waveform_graph.GetXaxis()->SetTitle("Time");
    waveform_graph.GetYaxis()->SetTitle("ADC");
    waveform_graph.Draw();

    auto* rc = static_cast<TRootCanvas*>(canvas.GetCanvasImp()); // NOLINT
    rc->Connect("CloseWindow()", "TApplication", gApplication, "Terminate()");

    bool first_event = true;
    auto last_graph_update = std::chrono::steady_clock::now();
    auto last_info_update = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::milliseconds(100); // Min interval between canvas updates

    while (m_is_running) {
      auto& events = input.get(timeout.count());

      gSystem->ProcessEvents();

      // Process events data
      for (const auto& event : events) {
        if (!event.valid()) {
          continue;
        }

        // Find channel and value of the highest adc measurement for this event
        size_t selected_waveform = 0; // event channel waveform with highest adc value
        short energy = 0;             // highest event adc value
        int current_min_channel = std::numeric_limits<int>::max();
        int current_max_channel = 0;

        for (size_t waveform = 0; waveform < event.waveform_count(); ++waveform) {
          for (size_t word = 0; word < event.word_count(waveform); ++word) {
            const int channel = channel_id(event, selected_waveform);
            current_min_channel = std::min(current_min_channel, channel);
            current_max_channel = std::max(current_max_channel, channel);

            auto signal = event.get_word(waveform, word);
            if (energy < signal) {
              selected_waveform = waveform;
              energy = signal;
            }
          }
        }

        // Update info graphs with selected channel and energy
        if (energy > 0) {
          const int channel = channel_id(event, selected_waveform);

          // Adjust number of channels in the histogram
          if (first_event) {
            min_channel = current_max_channel;
            max_channel = current_max_channel;
            first_event = false;
          } else if (min_channel > current_min_channel || max_channel < current_max_channel) {
            min_channel = std::min(current_min_channel, min_channel);
            max_channel = std::max(current_max_channel, max_channel);
            channels_hist.SetBins(max_channel - min_channel, min_channel, max_channel);
          }

          channels_hist.Fill(channel);
          energy_hist.Fill(energy);

          if (!draw_waveform) {
            const auto words = static_cast<int>(event.word_count(selected_waveform));
            waveform_graph.Set(words);
            for (int i = 0; i < words; ++i) {
              waveform_graph.SetPoint(i, i, event.get_word(selected_waveform, i));
            }
            draw_waveform = true;
          }
        }
      }

      // Update info
      static constexpr auto info_interval = std::chrono::milliseconds(1000);
      const auto now = std::chrono::steady_clock::now();
      if (now - last_info_update > info_interval) {
        update_stats();
        last_info_update = now;
      }

      // Redraw graphs at a fixed rate
      if (now - last_graph_update > timeout) {
        const auto reader_info = fmt::format("Reader: {:5.1f} MB/s - Buffer usage {:5.1f} % - load {:5.1f} %",
            m_stats.read_speed, m_stats.read_buffer_use, m_stats.read_load);
        const auto decoder_info = fmt::format("Decoder: Events {} - {:.0f} Events/s - Invalid events {:5.1f} % - load {:5.1f} %",
            m_stats.valid_events, m_stats.valid_event_rate, m_stats.invalid_event_ratio, m_stats.decode_load);
        const auto writer_info = fmt::format("Writer: {:5.1f} MB/s - load {:5.1f} %\n",
            m_stats.write_speed, m_stats.write_load);

        text_pad.cd();
        TText reader_text(0, 0.8, reader_info.data());
        reader_text.SetTextSize(0.25);
        reader_text.Draw();
        TText decoder_text(0, 0.5, decoder_info.data());
        decoder_text.SetTextSize(0.25);
        decoder_text.Draw();
        TText writer_text(0, 0.2, writer_info.data());
        writer_text.SetTextSize(0.25);
        writer_text.Draw();

        graph_pad.cd(1);
        energy_hist.Draw();

        graph_pad.cd(2);
        channels_hist.Draw();

        if (draw_waveform) {
          graph_pad.cd(3);
          waveform_graph.Draw("apl");
          draw_waveform = false;
        }

        canvas.Modified();
        canvas.Update();
        last_graph_update = now;
      }
    }
    app.Run();
  }

  std::string m_file_prefix;
  std::string m_fec_address;
  bool m_save_raw;

  ReadStats m_read_stats {};
  DecodeStats m_decoder_stats {};
  WriteStats m_write_stats {};
  Stats m_stats {};

  int m_file_count = 0;
  bool m_is_running = true;
};

} // namespace sampasrs
