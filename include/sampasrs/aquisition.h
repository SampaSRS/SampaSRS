#pragma once

#include <boost/circular_buffer.hpp>
#include <boost/core/bit.hpp>
#include <boost/endian/conversion.hpp>
#include <fmt/core.h>
#include <tins/tins.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace sampasrs {

using namespace Tins;
using payload_data = std::vector<uint8_t>;

template <typename T, unsigned char start, unsigned char end>
constexpr T get_bit_range(const T input) {
  constexpr T bit_mask = (T{1} << static_cast<uint8_t>(end - start)) - 1;
  return (input >> start) & bit_mask;
}

template <typename T> T read_from_buffer(const uint8_t *ptr) {
  T output;
  std::memcpy(&output, ptr, sizeof(T));
  boost::endian::big_to_native_inplace(output);
  return output;
}

inline uint8_t odd_parity(uint64_t word) {
  return std::bitset<64>(word).count() % 2;
}

struct Hit {
  Hit() = default;
  explicit Hit(uint64_t new_data) : data{new_data} {}
  explicit Hit(const uint8_t *ptr) : data{read_from_buffer<uint64_t>(ptr)} {}

  template <unsigned char start, unsigned char end> uint64_t bit_range() const {
    return get_bit_range<uint64_t, start, end>(data);
  }

  enum : uint8_t { DATA = 0, HEADER = 1, END = 2, TRIGTOOEARLY = 3 };

  uint8_t pk() const { return bit_range<62, 64>(); }
  // Queue index from 1 to 16
  uint8_t queue() const { return bit_range<52, 58>(); }
  // Queue index from 0 to 15
  uint8_t queue_index() const { return queue() - 1; }

  // Header fields
  uint8_t data_parity() const { return bit_range<51, 52>(); }
  uint32_t bx_count() const {
    return bit_range<32, 51>() << 1U | bit_range<29, 30>();
  }
  uint8_t channel_addr() const { return bit_range<24, 29>(); }
  uint8_t sampa_addr() const { return bit_range<20, 24>(); }
  uint16_t word_count() const { return bit_range<10, 20>(); }
  uint8_t pkt() const { return bit_range<7, 10>(); }
  uint8_t header_parity() const { return bit_range<6, 7>(); }
  uint8_t hamming() const { return bit_range<0, 6>(); }

  // Number of hit data expected for this queue, including header
  short hit_count() const {
    static constexpr uint16_t words_per_hit = 5;
    // Ceil integer division plus queue header
    return static_cast<short>(
        (word_count() + words_per_hit - 1) / words_per_hit + 1);
  }

  // Data fields
  bool full() const { return bit_range<31, 32>() != 0U; }
  short word4() const { return static_cast<short>(bit_range<42, 52>()); }
  short word3() const { return static_cast<short>(bit_range<32, 42>()); }
  short word2() const { return static_cast<short>(bit_range<20, 30>()); }
  short word1() const { return static_cast<short>(bit_range<10, 20>()); }
  short word0() const { return static_cast<short>(bit_range<0, 10>()); }

  short word(uint8_t i) const {
    switch (i) {
    case 0:
      return word0();
    case 1:
      return word1();
    case 2:
      return word2();
    case 3:
      return word3();
    case 4:
      return word4();
    default:
      return 0;
    }
  }

  // Convert between the Hamming code bit position to the actual header bit
  // position
  static uint8_t hamming_to_real_index(uint8_t index) {
    const auto most_significant_bit = 8 - boost::core::countl_zero(index);
    const bool is_parity_bit = boost::core::has_single_bit(index);

    if (is_parity_bit) {
      return most_significant_bit - 1;
    }

    index += 6 - most_significant_bit;
    // Ignore unused bits 30 and 31
    if (index > 29) {
      index += 2;
    }
    return index;
  }

  // Check header integrity using Hamming code
  bool check_header_integrity(bool do_correction = false) {
    using bitmask = std::bitset<64>;
    static constexpr uint8_t hamming_code_size = 50;

    // Generate Hamming code masks
    // see: https://en.wikipedia.org/wiki/Hamming_code#General_algorithm
    static const auto masks = []() {
      std::array<uint64_t, hamming_parity_bits> masks{};

      for (size_t pow = 0; pow < masks.size(); ++pow) {
        auto base = 1U << pow;
        bitmask mask = 0U;
        for (uint8_t code_index = 1; code_index < hamming_code_size;
             ++code_index) {
          auto data_index = hamming_to_real_index(code_index);
          mask[data_index] = ((code_index & base) != 0);
        }
        masks[pow] = mask.to_ulong();
      }

      return masks;
    }();

    // Compute parity matrix
    uint64_t syndrome = 0;
    for (size_t i = 0; i < masks.size(); ++i) {
      auto parity = odd_parity(data & masks[i]);
      syndrome += parity << i;
    }

    static constexpr uint64_t overall_parity_mask =
        ((uint64_t{1} << 52U) - 1) ^ (uint64_t(0b11) << 30U);
    // The parity will always be even when we include the parity bit
    const auto overall_parity = odd_parity(data & overall_parity_mask);

    if (!do_correction) {
      return overall_parity == 0 && syndrome == 0;
    }

    // No error
    if (overall_parity == 0 && syndrome == 0) {
      return true;
    }

    // More than one error, dont correct
    if (overall_parity == 0 && syndrome != 0) {
      return false;
    }

    // Fix single bit error
    if (overall_parity == 1 && syndrome < hamming_code_size) {
      auto bit_in_error = hamming_to_real_index(syndrome);
      data = bitmask(data).flip(bit_in_error).to_ulong();
      return true;
    }
    return false;
  }

  // return odd parity of data hit
  uint8_t compute_data_parity(uint8_t words = 5) const {
    static const uint64_t mask = []() {
      uint64_t mask = (uint64_t{1} << 52U) - 1;
      // Ignore full and non set bits
      mask ^= uint64_t{0b11} << 30U;
      return mask;
    }();

    uint64_t masked_data = data & mask;

    // FIXME: will this work with TRIGTOOEARLY hits?
    // Ignore padding words at the end of the data stream
    if (words != 0) {
      uint8_t bits_to_keep = bits_per_word * words;
      if (words > 3) {
        bits_to_keep += 2;
      }
      uint64_t mask = (uint64_t{1} << bits_to_keep) - 1;
      masked_data &= mask;
    }

    return odd_parity(masked_data);
  }

  static constexpr uint8_t words_per_hit = 5;
  static constexpr uint8_t bits_per_word = 10;
  static constexpr uint8_t hamming_parity_bits = 6;
  uint64_t data;
};

struct Payload {
  Payload() = default;

  explicit Payload(payload_data &&_data, long _timestamp = 0)
      : timestamp(_timestamp), data(std::move(_data)) {}

  explicit Payload(Packet &&packet)
      : timestamp(std::chrono::microseconds(packet.timestamp()).count()),
        data(std::move(packet.pdu()->rfind_pdu<RawPDU>().payload())) {}

  static Payload read(std::ifstream &file) {
    Payload payload{};
    payload.data.resize(Payload::size);
    file.read(reinterpret_cast<char *>(&payload.timestamp),
              sizeof(payload.timestamp));
    file.read(reinterpret_cast<char *>(payload.data.data()),
              static_cast<long>(payload.data.size()));
    return payload;
  }

  void write(std::ofstream &file) const {
    file.write(reinterpret_cast<const char *>(&timestamp), sizeof(timestamp));
    file.write(reinterpret_cast<const char *>(data.data()),
               static_cast<long>(data.size()));
  }

  uint32_t frame_counter() const {
    return read_from_buffer<uint32_t>(&data[0]); // NOLINT
  }

  uint32_t data_id() const {
    auto data_fec = read_from_buffer<uint32_t>(&data[4]);
    return get_bit_range<uint32_t, 8, 32>(data_fec);
  }

  uint8_t fec_id() const { return read_from_buffer<uint8_t>(&data[7]) >> 4u; }

  uint32_t time() const { return read_from_buffer<uint32_t>(&data[8]); }

  uint32_t overflow() const { return read_from_buffer<uint32_t>(&data[12]); }

  size_t n_hits() const { return (data.size() - hit_start_pos) / sizeof(Hit); }

  Hit hit(size_t i) const {
    const auto index = i * sizeof(Hit) + hit_start_pos;
    return Hit(&data[index]);
  }

  static constexpr size_t hit_start_pos = 16;
  static constexpr size_t size = 1032;

  long timestamp = 0;
  payload_data data;
};

struct Event {
  std::vector<Hit> hits{};
  std::vector<size_t> waveform_begin{};
  long timestamp = 0;
  uint32_t bx_count = 0;
  short open_queues = 0; // Number of channels receiving data
  uint8_t fec_id = 0;
  bool valid = true;

  Hit get_header(size_t waveform) const {
    return hits[waveform_begin[waveform]];
  }

  size_t waveform_count() const { return waveform_begin.size(); }

  size_t word_count(size_t waveform) const {
    return get_header(waveform).word_count();
  }

  short get_word(size_t waveform, size_t word) const { // NOLINT
    size_t hit_idx = waveform_begin[waveform] + 1 + word / Hit::words_per_hit;
    uint8_t word_idx = word % Hit::words_per_hit;
    return hits[hit_idx].word(word_idx);
  }

  std::vector<short> copy_waveform(size_t waveform) const {
    std::vector<short> data;
    auto words = word_count(waveform);
    data.resize(words);

    for (size_t i = 0; i < words; ++i) {
      data[i] = get_word(waveform, i);
    }
    return data;
  }

  size_t add_waveform(short n_hits) {
    waveform_begin.push_back(hits.size());
    hits.resize(hits.size() + n_hits, Hit{uint64_t(0)});
    return waveform_begin.back();
  }
};

class EventSorter {
public:
  explicit EventSorter(const std::function<void(Event)> &event_handler,
                       bool fix_header = false)
      : m_event_handler(event_handler), m_try_header_fix(fix_header) {}

  void process(const Payload &payload) {
    ++m_processed_payloads;

    const auto data_id = payload.data_id();
    static constexpr uint32_t valid_data_id = 0x564d33U; // VM3
    static constexpr uint32_t empty_data_id = 0xcacacaU; // caca

    // Ignore caca
    if (data_id == empty_data_id) {
      return;
    }

    // Invalid payload
    if (data_id != valid_data_id) {
      invalidate_all_events();
      return;
    }

    for (size_t i = 0; i < payload.n_hits(); ++i) {
      auto hit = payload.hit(i);
      process(hit, payload.fec_id(), payload.timestamp);
    }
  }

  void clear() {
    m_event_pool.clear();
    std::fill(m_queues.begin(), m_queues.end(), Queue{});
    m_processed_events = 0;
    m_processed_payloads = 0;
  }

  size_t get_processed_events() const { return m_processed_events; }

private:
  struct Queue {
    Event *event = nullptr; // event it belongs to
    short remaining_hits =
        0; // Expected number of hits until the end of the queue
    unsigned int next_index = 0; // where to store new queue data in the event
    bool is_open = false;
    uint8_t data_parity = 0;
    uint8_t expected_data_parity{};
    int words_in_last_hit{};

    void clear() { *this = Queue{}; }
  };

  void process(Hit hit, uint8_t fec_id, long timestamp) {
    auto queue_id = hit.queue_index();
    if (queue_id >= queue_size) {
      return;
    }
    auto &queue = m_queues[queue_id];

    switch (hit.pk()) {
    case Hit::HEADER: {
      if (!hit.check_header_integrity(m_try_header_fix)) {
        break;
      }
      auto bx_count = hit.bx_count();
      auto &event = m_event_pool[hit.bx_count()];

      // New event
      if (event.hits.empty()) {
        event.bx_count = bx_count;
        event.fec_id = fec_id;
        event.timestamp = timestamp;
      }

      open_queue(queue, hit, event);
    } break;

    case Hit::DATA: {
      store_hit(queue, hit);
    } break;

    case Hit::END: {
      store_hit(queue, hit);
      if (queue.remaining_hits != 0) {
        queue.event->valid = false;
      }
      close_queue(queue);
    } break;

    case Hit::TRIGTOOEARLY: {
      store_hit(queue, hit);
      close_queue(queue);
    } break;
    }
  }

  void open_queue(Queue &queue, Hit hit, Event &new_event) {
    ++new_event.open_queues;

    if (queue.event != nullptr) {
      --queue.event->open_queues;

      if (queue.is_open) {
        queue.event->valid = false;
      }

      // Check if previous event is complete
      if (queue.event->open_queues <= 0) {
        process(*queue.event);
      }
    }

    queue.event = &new_event;
    queue.remaining_hits = hit.hit_count();
    queue.next_index = new_event.add_waveform(queue.remaining_hits);
    queue.is_open = true;
    queue.data_parity = 0;
    queue.expected_data_parity = hit.data_parity();
    queue.words_in_last_hit = hit.word_count() % Hit::words_per_hit;
    if (queue.words_in_last_hit == 0) {
      queue.words_in_last_hit = Hit::words_per_hit;
    }

    store_hit(queue, hit);
  }

  static void store_hit(Queue &queue, Hit hit) {
    // We missed the queue header, we will ignore this hit
    if (!queue.is_open) {
      return;
    }

    // Queue got more or less hits than expected
    if (queue.remaining_hits <= 0) {
      queue.event->valid = false;
      close_queue(queue);
      return;
    }

    // store hit in the corresponding event
    queue.event->hits[queue.next_index] = hit;
    ++queue.next_index;
    --queue.remaining_hits;

    // Compute data parity
    switch (hit.pk()) {
    case Hit::DATA:
      queue.data_parity ^= hit.compute_data_parity();
      break;
    case Hit::END:
    case Hit::TRIGTOOEARLY:
      queue.data_parity ^= hit.compute_data_parity(queue.words_in_last_hit);
      break;
    }
  }

  static void close_queue(Queue &queue) {
    // Check data parity
    if (queue.event != nullptr &&
        (queue.data_parity != queue.expected_data_parity)) {
      queue.event->valid = false;
    }
    queue.is_open = false;
  }

  void invalidate_all_events() {
    for (auto &bx_event : m_event_pool) {
      bx_event.second.valid = false;
    }
  }

  void process(Event &event) {
    auto bx_count = event.bx_count;
    // Ignore initial and incomplete events
    if ((event.valid || process_invalid_events) && m_processed_events > 3) {
      // Process event
      m_event_handler(std::move(event));
    }
    // Remove event from pool
    m_event_pool.erase(bx_count);

    ++m_processed_events;
  }

public:
  bool process_invalid_events = false;

private:
  static constexpr size_t queue_size = 16;
  std::unordered_map<uint32_t, Event> m_event_pool{};
  std::array<Queue, queue_size> m_queues{}; // where to store next queue data
  size_t m_processed_events = 0;
  size_t m_processed_payloads = 0;
  std::function<void(Event &&)> m_event_handler;
  bool m_try_header_fix;
};

template <typename Buffer>
void write_to_file(const Buffer &buffer, std::ofstream &file) {
  for (const auto &payload : buffer) {
    payload.write(file);
  }
}

// Network sniffer and raw data store
class Aquisition {
  using sc = std::chrono::steady_clock;
  using fast_clock = std::chrono::high_resolution_clock;
  using Buffer = boost::circular_buffer<Payload>;

public:
  struct ReadStats {
    size_t bytes = 0;
    size_t packets = 0;
  };

  struct WriteStats {
    size_t bytes = 0;
    double waiting_seconds = 0;
    double writing_seconds = 0;
  };

  Aquisition(const std::string &file_prefix, const std::string &address,
             int port = 6006, size_t buffer_size = 2000000)
      : m_file_prefix(file_prefix), m_address(address), m_port(port),
        m_network_buffer(std::max(buffer_size, size_t(100000))) {}

  void reader_task() {
    // Find interface do listen
    NetworkInterface iface;
    if (m_address.empty()) {
      iface = NetworkInterface::default_interface().name();
    } else {
      IPv4Address to_resolve(m_address);
      iface = NetworkInterface(to_resolve).name();
    }
    std::wcout << "Listening to interface: " << iface.friendly_name() << "\n";

    // Sniff on interface
    SnifferConfiguration config;
    auto filter = fmt::format("udp port {}", m_port);
    config.set_filter(filter);
    Sniffer sniffer(iface.name(), config);
    sniffer.set_timeout(10);

    while (m_keep_acquisition) {
      Packet packet = sniffer.next_packet();
      if (!packet) {
        // Error reading packet
        continue;
      }
      m_read_stats.bytes += packet.pdu()->size();

      Payload payload(std::move(packet));
      ++m_read_stats.packets;

      // TODO: allow variable size packets (is this needed?)
      if (payload.data.size() == Payload::size) {
        {
          std::lock_guard<std::mutex> lock(m_buffer_access);
          m_network_buffer.push_back(std::move(payload));
        }
        m_buffer_ready.notify_all();
      }
    }
  }

  std::string next_file_name() {
    auto file_name = fmt::format("{}-{:04d}.raw", m_file_prefix, m_file_count);
    ++m_file_count;
    return file_name;
  }

  void writer_task() {
    // min/max packets to write to file at once
    const size_t min_packets = 50;
    const size_t max_packets = 10000;
    const size_t file_packets_limit = size_t(2) << 20U; // ~2 GB
    auto packets_saved = std::numeric_limits<unsigned int>::max();

    std::vector<Payload> file_buffer;
    file_buffer.reserve(max_packets);
    std::ofstream file;

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
        // Wait until for the buffer to enough elements then we acquire the
        // lock
        auto waiting_timer = fast_clock::now();
        m_buffer_ready.wait_for(lock, std::chrono::seconds(1), [&] {
          return m_network_buffer.size() >= min_packets;
        });
        m_write_stats.waiting_seconds +=
            std::chrono::duration<double>(fast_clock::now() - waiting_timer)
                .count();

        if (m_network_buffer.empty()) {
          continue;
        }

        // Get payloads to write
        const size_t payload_count =
            std::min(m_network_buffer.size(), file_buffer.capacity());
        for (int i = 0; i < payload_count; ++i) {
          m_write_stats.bytes += m_network_buffer.front().data.size();
          file_buffer.emplace_back(std::move(m_network_buffer.front()));
          m_network_buffer.pop_front();
        }
        packets_saved += payload_count;
      }

      // Write to file
      auto writing_timer = fast_clock::now();
      write_to_file(file_buffer, file);
      m_write_stats.writing_seconds +=
          std::chrono::duration<double>(fast_clock::now() - writing_timer)
              .count();

      file_buffer.clear();
    }
    fmt::print("Writing buffer to file...\n");
    // Write remaining payloads in the buffer
    write_to_file(m_network_buffer, file);
    fmt::print("Done\n");
  }

  void logger_task() {
    ReadStats last_read_stats{};
    WriteStats last_write_stats{};
    auto last_time = sc::now();

    std::this_thread::sleep_for(std::chrono::seconds(1));
    while (m_keep_acquisition) {
      auto now = sc::now();
      auto read_stats = m_read_stats;
      auto write_stats = m_write_stats;
      auto dt = std::chrono::duration<double>(now - last_time).count();

      auto received_mb =
          static_cast<double>(read_stats.bytes - last_read_stats.bytes) /
          1024. / 1024.;
      auto read_rate = received_mb / dt;

      auto buffer_usage = static_cast<double>(m_network_buffer.size()) /
                          static_cast<double>(m_network_buffer.capacity());

      auto writing_time =
          write_stats.writing_seconds - last_write_stats.writing_seconds;
      auto waiting_time =
          write_stats.waiting_seconds - last_write_stats.waiting_seconds;
      auto write_load =
          writing_time / (waiting_time + writing_time + 1e-10) * 100.;

      fmt::print("{} - {:5.1f} MB/s - buffer usage {:5.1f} % - Write load "
                 "{:5.1f} %\n",
                 read_stats.packets, read_rate, buffer_usage, write_load);

      last_read_stats = read_stats;
      last_write_stats = write_stats;
      last_time = now;
      std::this_thread::sleep_for(std::chrono::seconds(1));
    }
  }

  void run() {
    std::thread writer(&Aquisition::writer_task, this);
    std::thread reader(&Aquisition::reader_task, this);
    std::thread logger(&Aquisition::logger_task, this);

    for (;;) {
      const auto key = std::cin.get();
      if (key == 'x') {
        m_keep_acquisition = false;
        fmt::print("Exiting\n");
        break;
      }
    }

    logger.join();
    writer.join();

    // Send packet to unblock the sniffer's loop
    PacketSender sender;
    auto pkt = IP(m_address) / UDP(m_port) / RawPDU("tchau");
    sender.send(pkt);
    reader.join();
  }

private:
  std::string m_file_prefix;
  std::string m_address;
  int m_port;
  Buffer m_network_buffer;
  ReadStats m_read_stats{};
  WriteStats m_write_stats{};
  std::mutex m_buffer_access{};
  std::condition_variable m_buffer_ready{};
  int m_file_count = 0;
  bool m_keep_acquisition = true;
};

} // namespace sampasrs
