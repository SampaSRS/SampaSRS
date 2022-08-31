#include <boost/asio.hpp>
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

class PacketSender {
  using udp = boost::asio::ip::udp;

public:
  PacketSender(const std::string &address, int port)
      : m_socket(m_io_context, udp::v4()) {
    udp::resolver resolver(m_io_context);
    m_endpoint =
        *resolver.resolve(udp::v4(), address, std::to_string(port)).begin();
  }

  void send(const uint8_t *ptr, size_t size) {
    try {
      m_socket.send_to(boost::asio::buffer(ptr, size), m_endpoint);
    } catch (std::exception &e) {
      std::cerr << e.what() << "\n";
    }
  }

  void send(const std::vector<uint8_t> &payload) {
    try {
      m_socket.send_to(boost::asio::buffer(payload), m_endpoint);
    } catch (std::exception &e) {
      std::cerr << e.what() << "\n";
    }
  }

private:
  boost::asio::io_context m_io_context{};
  udp::socket m_socket;
  udp::endpoint m_endpoint;
};

template <typename Buffer>
void write_to_file(const Buffer &buffer, std::ofstream &file) {
  for (size_t i = 0; i < buffer.size(); ++i) {
    file.write(reinterpret_cast<const char *>(buffer[i].data()),
               static_cast<long>(buffer[i].size()));
  }
}

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
  uint16_t word4() const { return bit_range<42, 52>(); }
  uint16_t word3() const { return bit_range<32, 42>(); }
  bool full() const { return bit_range<31, 32>() != 0U; }
  uint16_t word2() const { return bit_range<20, 30>(); }
  uint16_t word1() const { return bit_range<10, 20>(); }
  uint16_t word0() const { return bit_range<0, 10>(); }

  uint16_t word(uint8_t i) const {
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
    static constexpr uint64_t mask = []() {
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

class Payload {
  static constexpr size_t hit_start_pos = 16;

public:
  explicit Payload(payload_data &data) : m_data(data) {}

  uint32_t frame_counter() const {
    return read_from_buffer<uint32_t>(&m_data[0]);
  }

  uint32_t data_id() const {
    auto data_fec = read_from_buffer<uint32_t>(&m_data[4]);
    return get_bit_range<uint32_t, 8, 32>(data_fec);
  }

  uint8_t fec_id() const {
    auto data_fec = read_from_buffer<uint32_t>(&m_data[4]);
    return get_bit_range<uint32_t, 0, 8>(data_fec);
  }

  uint32_t time() const { return read_from_buffer<uint32_t>(&m_data[8]); }

  uint32_t overflow() const { return read_from_buffer<uint32_t>(&m_data[12]); }

  size_t n_hits() const {
    return (m_data.size() - hit_start_pos) / sizeof(Hit);
  }

  Hit next_hit() {
    Hit hit(&m_data[m_hit_idx]);
    m_hit_idx += sizeof(Hit);
    return hit;
  }

  void reset() { m_hit_idx = hit_start_pos; }

private:
  payload_data &m_data;
  size_t m_hit_idx = hit_start_pos;
};

struct Event {
  std::vector<Hit> hits{};
  std::vector<size_t> channels_start{};
  uint32_t bx_count = 0;
  short open_queues = 0; // Number of channels receiving data
  bool valid = true;

  Hit header(size_t channel) const { return hits[channels_start[channel]]; }

  size_t channel_words(size_t channel) const {
    return header(channel).word_count();
  }

  uint16_t waveform(size_t channel, size_t word) const { // NOLINT
    auto i = channels_start[channel];
    return hits[1 + i / Hit::words_per_hit].word(i % Hit::words_per_hit);
  }

  size_t channel_count() const { return channels_start.size(); }

  size_t add_channel(short channel_hits) {
    channels_start.push_back(hits.size());
    hits.resize(hits.size() + channel_hits, Hit{uint64_t(0)});
    return channels_start.back();
  }
};

class EventSorter {
public:
  static constexpr size_t queue_size = 16;

  explicit EventSorter(const std::function<void(Event)> &event_handler,
                       bool fix_header = false)
      : m_event_handler(event_handler), m_try_header_fix(fix_header) {}

  void process(Payload &payload) {
    ++m_processed_payloads;
    // Invalid payload
    static constexpr uint32_t valid_data_id = 0x564d33U; // VM3
    if (payload.data_id() != valid_data_id) {
      invalidate_events();
      return;
    }

    for (size_t i = 0; i < payload.n_hits(); ++i) {
      auto hit = payload.next_hit();
      process(hit);
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

  void process(Hit hit) {
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
      event.bx_count = bx_count;

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

      // Check if previous event is finished
      if (queue.event->open_queues <= 0) {
        dump(*queue.event);
      }
    }

    queue.event = &new_event;
    queue.remaining_hits = hit.hit_count();
    queue.next_index = new_event.add_channel(queue.remaining_hits);
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

  void invalidate_events() {
    for (auto &bx_event : m_event_pool) {
      bx_event.second.valid = false;
    }
  }

  void dump(Event &event) {
    auto bx_count = event.bx_count;
    // Ignore first and incomplete events
    if (event.valid && m_processed_events > 3) {
      // Process valid event
      m_event_handler(std::move(event));
    }
    // Remove event from pool
    m_event_pool.erase(bx_count);

    ++m_processed_events;
  }

  std::unordered_map<uint32_t, Event> m_event_pool{};
  std::array<Queue, queue_size> m_queues{}; // where to store next queue data
  size_t m_processed_events = 0;
  size_t m_processed_payloads = 0;
  std::function<void(Event)> m_event_handler;
  bool m_try_header_fix;
};

class Aquisition {
  using sc = std::chrono::steady_clock;
  using fast_clock = std::chrono::high_resolution_clock;
  using Buffer = boost::circular_buffer<payload_data>;

public:
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
      auto &payload = packet.pdu()->rfind_pdu<RawPDU>().payload();

      // FIXME: the SRS docs says we can get frame trails of just 4 bytes:
      // 0xfafafafa
      // TODO: allow variable size packets
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
    const size_t file_packets_limit = size_t(2) << 20U; // ~2 GB
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
        // Wait until for the buffer to enough elements then we acquire the
        // lock
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
    std::thread writer(&Aquisition::writer_task, this);
    std::thread reader(&Aquisition::reader_task, this);

    for (;;) {
      const auto key = std::cin.get();
      if (key == 'x') {
        m_keep_acquisition = false;
        fmt::print("Exiting\n");
        break;
      }
    }

    // TODO: find a way to stop the sniffer loop
    // reader.join();
    writer.join();
  }

private:
  std::string m_file_prefix;
  std::string m_address;
  int m_port;
  Buffer m_network_buffer;
  std::mutex m_buffer_access{};
  std::condition_variable m_buffer_ready{};
  int m_file_count = 0;
  bool m_keep_acquisition = true;
};

} // namespace sampasrs
