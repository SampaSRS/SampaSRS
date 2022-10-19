#pragma once

#include <sampasrs/decoder.hpp>
#include <sampasrs/utils.hpp>

#include <boost/circular_buffer.hpp>
#include <boost/histogram.hpp> // make_histogram, regular, weight, indexed
#include <fmt/core.h>
#include <tins/tins.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <optional>
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
  bool empty() const { return size() == 0; }
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
  public:
  using fast_clock = std::chrono::high_resolution_clock;
  using Clock = std::chrono::steady_clock;

  explicit Acquisition(const std::string& file_prefix, bool save_raw = true,
      const std::optional<std::function<void(Event&&)>>& event_handler = {},
      const std::string& fec_address = "10.0.0.2")
      : m_file_prefix(file_prefix)
      , m_fec_address(fec_address)
  {
    // Check prefix to avoid deleting old files
    const auto first_file_name = save_raw ? next_file_name<Payload>(false) : next_file_name<Event>(false);
    if (std::filesystem::exists(first_file_name)) {
      throw std::runtime_error(fmt::format("File \"{}\" exists", first_file_name));
    }

    const auto save = save_raw ? Store::Raw : Store::Event;
    start(save, event_handler);
  }

  explicit Acquisition(const std::function<void(Event&&)>& event_handler, const std::string& fec_address = "10.0.0.2")
      : m_fec_address(fec_address)
  {
    start(Store::Raw, event_handler);
  }

  void stop(size_t events = 0)
  {
    std::mutex m;
    std::condition_variable v;
    std::unique_lock<std::mutex> lock(m);

    v.wait(lock, [&] { return events >= m_decoder_stats.valid_events; });
    m_is_running = false;
  }

  ~Acquisition()
  {
    m_is_running = false;

    for (size_t i = 0; i < m_pipeline.size() - 1; ++i) {
      if (m_pipeline[i].joinable()) {
        m_pipeline[i].join();
      }
    }

    // Send packet to unblock the sniffer's loop
    // TODO: find a less hacky way to break this
    Tins::PacketSender sender;
    auto pkt = Tins::IP("10.0.0.3", m_fec_address) / Tins::UDP(6006) / Tins::RawPDU("tchau");
    sender.send(pkt);

    m_pipeline.back().join(); // reader thread
  }

  struct ReadStats {
    size_t bytes = 0;
    size_t packets = 0;
    size_t buffer_items = 0;
    size_t buffer_size = 0;
    Clock::duration total_time {};
    Clock::duration process_time {};
  };

  struct DecodeStats {
    size_t bytes = 0;
    size_t valid_events = 0;
    size_t total_events = 0;
    Clock::duration total_time {};
    Clock::duration process_time {};
  };

  struct WriteStats {
    size_t bytes = 0;
    size_t buffer_items = 0;
    size_t buffer_size = 0;
    Clock::duration total_time {};
    Clock::duration process_time {};
  };

  struct Stats {
    ReadStats read {};
    DecodeStats decode {};
    WriteStats write {};
    Clock::time_point last {};

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

  const Stats& get_stats() const { return m_stats; }

  private:
  void update_stats()
  {
    const auto now = Clock::now();
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

  enum class Store {
    None,
    Raw,
    Event
  };

  void start(Store store, const std::optional<std::function<void(Event&&)>>& event_handler = {})
  {
    // Start data aquisition and processing
    switch (store) {
    case Store::None:
      m_reader_buffer.config(100000, 50, 10000);
      m_out_event_buffer.config(10000, 10, 1000);

      m_pipeline.emplace_back(&Acquisition::decoder_task, this, std::ref(m_reader_buffer), std::ref(m_out_event_buffer));
      break;

    case Store::Raw:
      m_reader_buffer.config(2000000, 50, 10000);
      m_tmp_payload_buffer.config(100000, 50, 100);
      m_out_event_buffer.config(1000, 10, 100);

      m_pipeline.emplace_back(&Acquisition::writer_task<Payload>, this, std::ref(m_reader_buffer), std::ref(m_tmp_payload_buffer));
      m_pipeline.emplace_back(&Acquisition::decoder_task, this, std::ref(m_tmp_payload_buffer), std::ref(m_out_event_buffer));
      break;

    case Store::Event:
      m_reader_buffer.config(100000, 50, 10000);
      m_decoder_buffer.config(100000, 10, 1000);
      m_out_event_buffer.config(10000, 10, 1000);

      m_pipeline.emplace_back(&Acquisition::decoder_task, this, std::ref(m_reader_buffer), std::ref(m_decoder_buffer));
      m_pipeline.emplace_back(&Acquisition::writer_task<Event>, this, std::ref(m_decoder_buffer), std::ref(m_out_event_buffer));
      break;
    }

    if (event_handler.has_value()) {
      m_pipeline.emplace_back(&Acquisition::event_handler_task, this, std::ref(m_out_event_buffer), event_handler.value());
    }

    m_pipeline.emplace_back(&Acquisition::reader_task, this, std::ref(m_reader_buffer));
  }

  void reader_task(FIFO<Payload>& output)
  {
    using namespace Tins;
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
      const auto start = Clock::now();
      Packet packet = sniffer.next_packet();
      const auto start_process = Clock::now();

      if (!packet) {
        // Error reading packet
        continue;
      }
      m_read_stats.bytes += packet.pdu()->size();

      Payload payload(std::move(packet));
      ++m_read_stats.packets;

      output.put(std::move(payload));

      m_read_stats.buffer_items = output.size();

      const auto end = Clock::now();
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
    Timer stats_timer(std::chrono::milliseconds(1000)); // Stats update interval

    EventSorter sorter(event_handler);
    sorter.enable_header_fix = false;
    sorter.enable_remove_caca = true;
    sorter.process_invalid_events = true;

    while (m_is_running || !input.empty()) {
      const auto start = Clock::now();
      auto& payloads = input.get();
      const auto start_process = Clock::now();

      for (auto& payload : payloads) {
        sorter.process(payload);
      }

      const auto end = Clock::now();
      m_decoder_stats.total_time += end - start;
      m_decoder_stats.process_time += end - start_process;

      // Update stats
      if (stats_timer) {
        update_stats();
      }
    }
  }

  std::string next_file_name(std::string_view extension = "raw", bool increment = true)
  {
    auto file_name = fmt::format("{}-{:04d}.{}", m_file_prefix, m_file_count, extension);
    if (increment) {
      ++m_file_count;
    }
    return file_name;
  }

  template <typename T>
  std::string next_file_name(bool increment = true)
  {
    if constexpr (std::is_same<T, Payload>::value) {
      return next_file_name("raw", increment);
    }
    return next_file_name("rawev", increment);
  }

  template <typename T>
  void writer_task(FIFO<T>& input, FIFO<T>& output)
  {
    m_write_stats.buffer_size = input.capacity();
    const size_t max_file_size = size_t(2) << 30U; // ~2 GB in bytes
    auto file_size = std::numeric_limits<unsigned int>::max();

    std::ofstream file;

    while (m_is_running || !input.empty()) {
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

      const auto start = Clock::now();
      auto& data = input.get();
      const auto start_process = Clock::now();

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

      const auto end = Clock::now();
      m_write_stats.total_time += end - start;
      m_write_stats.process_time += end - start_process;
    }
  }

  void event_handler_task(FIFO<Event>& input, const std::function<void(Event&&)>& event_handle) const
  {
    const auto get_timeout = std::chrono::milliseconds(100); // Max interval between event processes
    while (m_is_running || !input.empty()) {
      auto& events = input.get(get_timeout.count());

      // Process events data
      for (auto&& event : events) {
        event_handle(std::move(event));
      }
    }
  }

  std::string m_file_prefix {};
  std::string m_fec_address {};

  ReadStats m_read_stats {};
  DecodeStats m_decoder_stats {};
  WriteStats m_write_stats {};
  Stats m_stats {};

  int m_file_count = 0;
  bool m_is_running = true;

  // Define data pipeline and buffers
  std::vector<std::thread> m_pipeline {};
  FIFO<Payload> m_reader_buffer {};
  FIFO<Event> m_decoder_buffer {};
  FIFO<Payload> m_tmp_payload_buffer {};
  FIFO<Event> m_out_event_buffer {};
};

} // namespace sampasrs
