#include "sampasrs.h"
#include <boost/circular_buffer.hpp>
#include <boost/endian/conversion.hpp>
#include <fmt/core.h>
#include <tins/tins.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

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

  bool check_header_integrity() const {
    return valid_header_parity() && valid_hamming_code();
  }

  // Check the header parity bit
  // \return true if the header is ok
  bool valid_header_parity() const {
    static constexpr uint64_t mask =
        ((uint64_t{1} << 52U) - 1) ^ (uint64_t(0b11) << 30U);
    // The parity will always be even when we include the parity bit
    return odd_parity(data & mask) == 0;
  }

  // Check header integrity using Hamming code, but no correction is done
  bool valid_hamming_code() const {
    // Generate Hamming code masks
    // see: https://en.wikipedia.org/wiki/Hamming_code#General_algorithm
    static const auto masks = []() {
      using bitmask = std::bitset<64>;
      std::array<uint64_t, hamming_code_size> masks{};

      for (size_t pow = 0; pow < masks.size(); ++pow) {
        size_t base = 1U << pow;
        bitmask mask = 0;
        mask[pow] = true;
        for (size_t code_index = 1, data_index = 7; data_index < 52;
             ++code_index) {
          // bits 30 and 31 are not in use
          if (data_index == 30) {
            data_index += 2;
          }
          // Jump base bits
          if (bitmask(code_index).count() == 1) {
            continue;
          }
          mask[data_index] = ((code_index & base) != 0);
          ++data_index;
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

    // TODO: Fix single bit error

    return syndrome == 0;
  }

  // return true for odd parity
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
  static constexpr uint8_t hamming_code_size = 6;
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

struct WaveForm {
  WaveForm(const uint64_t *data, size_t size) : m_data{data}, m_size{size} {
    m_size = Hit(m_data[0]).hit_count();
  }
  Hit header() const { return Hit(m_data[0]); }
  uint16_t operator[](size_t idx) const {
    return Hit(m_data[1 + idx / Hit::words_per_hit])
        .word(idx % Hit::words_per_hit);
  }
  size_t size() const { return m_size; }

private:
  const uint64_t *m_data;
  size_t m_size;
};

struct Event {
  std::vector<Hit> hits{};
  int channel_count = 0; // Number of channels in this event
  uint32_t bx_count = 0;
  short open_queues = 0; // Number of channels receiving data
  bool valid = true;
  bool empty() const { return channel_count == 0; }
};

class EventSorter {
public:
  static constexpr size_t queue_size = 16;

  explicit EventSorter(const std::function<void(Event)> &event_handler)
      : m_event_handler(event_handler) {}

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
    Event *event = nullptr;      // event it belongs to
    unsigned int next_index = 0; // where to store new queue data in the event
    short remaining_hits =
        0; // Expected number of hits until the end of the queue
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
      if (!hit.check_header_integrity()) {
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
    queue.next_index = new_event.hits.size();
    queue.remaining_hits = hit.hit_count();
    queue.is_open = true;
    queue.data_parity = 0;
    queue.expected_data_parity = hit.data_parity();
    queue.words_in_last_hit = hit.word_count() % Hit::words_per_hit;
    if (queue.words_in_last_hit == 0) {
      queue.words_in_last_hit = Hit::words_per_hit;
    }

    ++new_event.channel_count;
    new_event.hits.resize(new_event.hits.size() + queue.remaining_hits);
    store_hit(queue, hit);
  }

  static void store_hit(Queue &queue, Hit hit) {
    // We missed this queue header, we will ignore this hit
    if (!queue.is_open) {
      return;
    }

    // Queue got more or less hits than expected
    if (queue.remaining_hits <= 0) {
      queue.event->valid = false;
      close_queue(queue);
      return;
    }

    // store hit in the corresponding
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
};

int main(int argc, const char *argv[]) {
  using namespace Tins;

  std::string file_name = "../example_data/Run15.pcapng";
  if (argc > 1) {
    file_name = argv[1];
  }
  FileSniffer sniffer(file_name);

  size_t n_events = 0;
  std::ofstream out_file("output.txt");

  auto event_handler = [&](Event event) {
    for (auto hit : event.hits) {
      std::string line;
      if (hit.pk() == Hit::HEADER) {
        line = fmt::format(
            "Pk {} Queue {:2d} Bx_count {} Word_count {} "
            "Ch_addr {:2d} sampa_addr {:2d} Header_par {} Data_par {} Hamming "
            "{}\n",
            hit.pk(), hit.queue(), hit.bx_count(), hit.word_count(),
            hit.channel_addr(), hit.sampa_addr(), hit.header_parity(),
            hit.data_parity(), hit.hamming());
        std::cout << line;
        if (!hit.valid_header_parity()) {
          std::cout << "########### ERROR IN PARITY #########\n";
        }
      } else {
        auto parity = hit.compute_data_parity();
        line = fmt::format("Pk {} Queue {:2d} Words {:4d} {:4d} {:4d} {:4d} "
                           "{:4d} Full {:d}\n",
                           hit.pk(), hit.queue(), hit.word0(), hit.word1(),
                           hit.word2(), hit.word3(), hit.word4(), hit.full());
      }
      out_file << line;
    }
    ++n_events;
  };

  EventSorter sorter(event_handler);

  auto sniffer_callback = [&](PDU &pdu) {
    auto &data = pdu.rfind_pdu<RawPDU>().payload();
    Payload payload(data);
    sorter.process(payload);
    return true;
  };

  sniffer.sniff_loop(sniffer_callback);
  std::cout << "Valid events " << n_events << "\n";
  std::cout << "Total events " << sorter.get_processed_events() << "\n";
};
