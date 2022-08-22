#include <circular_buffer.hpp>
#include <fmt/core.h>
#include <tins/tins.h>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace Tins;
using sc = std::chrono::steady_clock;
using fast_clock = std::chrono::high_resolution_clock;
using payload_data = std::vector<uint8_t>;
using Buffer = cb::circular_buffer<payload_data>;

template <typename Buffer>
void write_to_file(const Buffer &buffer, std::ofstream &file) {
  for (size_t i = 0; i < buffer.size(); ++i) {
    file.write(reinterpret_cast<const char *>(buffer[i].data()),
               static_cast<long>(buffer[i].size()));
  }
}

class SampaAquisition {
public:
  SampaAquisition(const std::string &file_prefix, const std::string &network,
                  int port = 6006, size_t buffer_size = 2000000)
      : m_file_prefix(file_prefix), m_network(network), m_port(port),
        m_network_buffer(std::max(buffer_size, size_t(100000))) {}

  void reader_task() {
    // Sniff on interface
    SnifferConfiguration config;
    auto filter = fmt::format("udp port {}", m_port);
    config.set_filter(filter);
    Sniffer sniffer(m_network, config);
    sniffer.set_timeout(10);

    while (m_keep_acquisition) {
      Packet packet = sniffer.next_packet();
      if (!packet) {
        // Error reading packet
        continue;
      }
      auto &payload = packet.pdu()->rfind_pdu<RawPDU>().payload();

      // FIXME: the SRS docs says we can get frame trails of just 4 bytes:
      // 0xfafafafa
      if (payload.size() == 1032) {
        {
          std::lock_guard<std::mutex> lock(m_buffer_access);
          m_network_buffer.push_back(std::move(payload));
        }
        m_buffer_ready.notify_all();
      }
    }
  }

  std::string next_file_name() {
    auto file_name = fmt::format("{}.{:04d}.raw", m_file_prefix, m_file_count);
    ++m_file_count;
    return file_name;
  }

  void writer_task() {
    // min/max packets to write to file at once
    const size_t min_packets = 50;
    const size_t max_packets = 10000;
    const size_t file_packets_limit = 2 << 20; // ~2 GB
    auto packets_saved = std::numeric_limits<unsigned int>::max();

    std::vector<payload_data> file_buffer;
    file_buffer.reserve(max_packets);
    std::ofstream file;

    const float to_mb = 1 / 1024. / 1024.;
    size_t bytes_received = 0;
    size_t packets_received = 0;
    double waiting_duration = 0;
    double writing_duration = 0;
    auto info_timer = sc::now();

    while (m_keep_acquisition) {
      // Create a new file if the previous one exceeded the max size
      if (packets_saved > file_packets_limit) {
        if (file.is_open()) {
          file.close();
        }
        packets_saved = 0;

        auto file_name = next_file_name();
        fmt::print("Writing to {}\n", file_name);
        file.open(file_name, std::ios::binary);
        if (!file) {
          throw std::runtime_error("Unable to create output file.");
        }
      }

      // Synchronized section, this code section will block the reader task
      {
        std::unique_lock<std::mutex> lock(m_buffer_access);
        // Wait until for the buffer to enough elements then we acquire the lock
        auto waiting_timer = fast_clock::now();
        m_buffer_ready.wait_for(lock, std::chrono::seconds(1), [&] {
          return m_network_buffer.size() >= min_packets;
        });
        waiting_duration +=
            std::chrono::duration<double>(fast_clock::now() - waiting_timer)
                .count();

        if (m_network_buffer.empty()) {
          continue;
        }

        // Get payloads to write
        const size_t payload_count =
            std::min(m_network_buffer.size(), file_buffer.capacity());
        for (int i = 0; i < payload_count; ++i) {
          bytes_received += m_network_buffer.front().size();
          file_buffer.emplace_back(std::move(m_network_buffer.front()));
          m_network_buffer.pop_front();
        }
        packets_received += payload_count;
        packets_saved += payload_count;
      }

      // Write to file
      auto writing_timer = fast_clock::now();
      write_to_file(file_buffer, file);
      writing_duration +=
          std::chrono::duration<double>(fast_clock::now() - writing_timer)
              .count();

      file_buffer.clear();

      // Print info
      const auto next_info_timer = sc::now();
      const auto info_duration =
          std::chrono::duration<float>(next_info_timer - info_timer).count();
      if (info_duration > 1) {
        const float write_rate =
            static_cast<float>(bytes_received) * to_mb / info_duration;
        const float buffer_usage =
            static_cast<float>(m_network_buffer.size()) /
            static_cast<float>(m_network_buffer.capacity()) * 100;
        const double write_ratio =
            writing_duration / (writing_duration + waiting_duration) * 100;

        fmt::print(
            "{} - {:.1f} MB/s - buffer usage {:.1f} % - Write time {:.1f} %\n",
            packets_received, write_rate, buffer_usage, write_ratio);

        info_timer = next_info_timer;
        bytes_received = 0;
        waiting_duration = 0;
        writing_duration = 0;
      }
    }
    fmt::print("Writing buffer to file....\n");
    // Write remaining payloads in the buffer
    write_to_file(m_network_buffer, file);
    fmt::print("Done\n");
  }

  void run() {
    std::thread writer(&SampaAquisition::writer_task, this);
    std::thread reader(&SampaAquisition::reader_task, this);

    for (;;) {
      const auto key = std::cin.get();
      if (key == 'x') {
        m_keep_acquisition = false;
        fmt::print("Exiting\n");
        break;
      }
    }

    // TODO: find a way to stop the sniff loop
    // reader.join();
    writer.join();
  }

private:
  std::string m_network;
  std::string m_file_prefix;
  int m_port;
  Buffer m_network_buffer;
  std::mutex m_buffer_access{};
  std::condition_variable m_buffer_ready{};
  int m_file_count = 0;
  bool m_keep_acquisition = true;
};

int main(int argc, const char *argv[]) {
  std::string file_prefix = "sampasrs";
  if (argc > 1) {
    file_prefix = argv[1];
  }

  std::string network_interface = "lo";
  if (argc > 2) {
    network_interface = argv[2];
  }

  SampaAquisition sampa(file_prefix, network_interface);
  sampa.run();
}
