#pragma once

#include <sampasrs/utils.hpp>

#include <boost/core/bit.hpp>
#include <boost/endian/conversion.hpp>
#include <fmt/core.h>
#include <tins/packet.h>
#include <tins/rawpdu.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
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
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace sampasrs {

using namespace Tins;
using payload_data = std::vector<uint8_t>;

inline uint8_t odd_parity(uint64_t word)
{
  return std::bitset<64>(word).count() % 2;
}

struct Hit {
  Hit() = default;
  explicit Hit(uint64_t new_data)
      : data {new_data}
  {
  }
  explicit Hit(const uint8_t* ptr)
      : data {read_from_buffer<uint64_t>(ptr)}
  {
  }

  template <unsigned char start, unsigned char end>
  uint64_t bit_range() const
  {
    return get_bit_range<uint64_t, start, end>(data);
  }

  enum : uint8_t { DATA = 0,
    HEADER = 1,
    END = 2,
    TRIGTOOEARLY = 3 };

  uint8_t pk() const { return bit_range<62, 64>(); }
  // Queue index from 1 to 16
  uint8_t queue() const { return bit_range<52, 58>(); }
  // Queue index from 0 to 15
  uint8_t queue_index() const { return queue() - 1; }
  bool full() const { return bit_range<31, 32>() != 0U; }

  // Header fields
  uint8_t data_parity() const { return bit_range<51, 52>(); }
  uint32_t bx_count() const
  {
    return bit_range<32, 51>() << 1U | bit_range<29, 30>();
  }
  uint8_t channel_addr() const { return bit_range<24, 29>(); }
  uint8_t sampa_addr() const { return bit_range<20, 24>(); }
  uint16_t word_count() const { return bit_range<10, 20>(); }
  uint8_t pkt() const { return bit_range<7, 10>(); }
  uint8_t header_parity() const { return bit_range<6, 7>(); }
  uint8_t hamming() const { return bit_range<0, 6>(); }

  // Number of hit data expected for this queue, including header
  short hit_count() const
  {
    static constexpr uint16_t words_per_hit = 5;
    // Ceil integer division plus queue header
    return static_cast<short>(
        (word_count() + words_per_hit - 1) / words_per_hit + 1);
  }

  // 32 * sampa + channel
  int global_channel() const
  {
    static constexpr int sampa_channels = 32;
    return sampa_channels * sampa_addr() + channel_addr();
  }

  // Data fields
  short word4() const { return static_cast<short>(bit_range<42, 52>()); }
  short word3() const { return static_cast<short>(bit_range<32, 42>()); }
  short word2() const { return static_cast<short>(bit_range<20, 30>()); }
  short word1() const { return static_cast<short>(bit_range<10, 20>()); }
  short word0() const { return static_cast<short>(bit_range<0, 10>()); }

  short word(uint8_t i) const
  {
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
  static uint8_t hamming_to_real_index(uint8_t index)
  {
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
  bool check_header_integrity(bool do_correction = false)
  {
    using bitmask = std::bitset<64>;
    static constexpr uint8_t hamming_code_size = 50;

    // Generate Hamming code masks
    // see: https://en.wikipedia.org/wiki/Hamming_code#General_algorithm
    static const auto masks = []() {
      std::array<uint64_t, hamming_parity_bits> masks {};

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

    static constexpr uint64_t overall_parity_mask = ((uint64_t {1} << 52U) - 1) ^ (uint64_t(0b11) << 30U);
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
  uint8_t compute_data_parity(uint8_t words = 5) const
  {
    static const uint64_t mask = []() {
      uint64_t mask = (uint64_t {1} << 52U) - 1;
      // Ignore full and non set bits
      mask ^= uint64_t {0b11} << 30U;
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
      uint64_t mask = (uint64_t {1} << bits_to_keep) - 1;
      masked_data &= mask;
    }

    return odd_parity(masked_data);
  }

  std::string to_string() const
  {
    std::string out {};
    switch (pk()) {
    case HEADER:
      out = fmt::format("Pk {} Queue {:2d} Bx_count {} Word_count {}Ch_addr {:2d} sampa_addr {:2d}",
          pk(), queue(), bx_count(), word_count(), channel_addr(), sampa_addr());
      break;
    default:
      out = fmt::format("Pk {} Queue {:2d} Words {:4d} {:4d} {:4d} {:4d} {:4d} Full {:d}",
          pk(), queue(), word0(), word1(), word2(), word3(), word4(), full());
      break;
    }
    return out;
  }

  static constexpr uint8_t words_per_hit = 5;
  static constexpr uint8_t bits_per_word = 10;
  static constexpr uint8_t hamming_parity_bits = 6;
  uint64_t data;
};

template <typename T>
void deserialize(std::ifstream& file, T& out)
{
  file.read(reinterpret_cast<char*>(&out), sizeof(out));
}

template <typename T>
void deserialize(std::ifstream& file, std::vector<T>& out)
{
  unsigned int size {};
  deserialize(file, size);
  out.resize(size);
  file.read(reinterpret_cast<char*>(out.data()), out.size() * sizeof(T));
}

template <typename T>
void serialize(std::ofstream& file, T input)
{
  file.write(reinterpret_cast<const char*>(&input), sizeof(input));
}

template <typename T>
void serialize(std::ofstream& file, const std::vector<T>& input)
{
  serialize(file, static_cast<unsigned int>(input.size()));
  file.write(reinterpret_cast<const char*>(input.data()), input.size() * sizeof(T));
}

struct Payload {
  Payload() = default;

  explicit Payload(payload_data&& _data, long _timestamp = 0)
      : timestamp(_timestamp)
      , data(std::move(_data))
  {
  }

  explicit Payload(Packet&& packet)
      : timestamp(std::chrono::microseconds(packet.timestamp()).count())
      , data(std::move(packet.pdu()->rfind_pdu<RawPDU>().payload()))
  {
  }

  static Payload read(std::ifstream& file)
  {
    Payload payload {};
    deserialize(file, payload.timestamp);
    deserialize(file, payload.data);
    return payload;
  }

  void write(std::ofstream& file) const
  {
    serialize(file, timestamp);
    serialize(file, data);
  }

  uint32_t frame_counter() const
  {
    return read_from_buffer<uint32_t>(&data[header_offset]); // NOLINT
  }

  uint32_t data_id() const
  {
    auto data_fec = read_from_buffer<uint32_t>(&data[4 + header_offset]);
    return get_bit_range<uint32_t, 8, 32>(data_fec);
  }

  uint8_t fec_id() const
  {
    return read_from_buffer<uint8_t>(&data[7 + header_offset]) >> 4u;
  }

  uint32_t time() const
  {
    return read_from_buffer<uint32_t>(&data[8 + header_offset]);
  }

  uint32_t overflow() const
  {
    return read_from_buffer<uint32_t>(&data[12 + header_offset]);
  }

  size_t n_hits() const { return (data.size() - hit_offset) / sizeof(Hit); }

  Hit hit(size_t i) const
  {
    const auto index = i * sizeof(Hit) + hit_offset;
    return Hit(&data[index]);
  }

  size_t byte_size() const { return data.size() - hit_offset; }

  static constexpr size_t header_size = 16;

  long timestamp = 0;
  payload_data data;
  size_t header_offset = 0;
  size_t hit_offset = header_size;
};

struct Event {
  std::vector<Hit> hits {};
  std::vector<size_t> waveform_begin {};
  long timestamp = 0;
  uint32_t bx_count = 0;
  uint8_t fec_id = 0;
  std::bitset<8> error = 0;
  short open_queues = 0; // Number of channels receiving data

  enum Err : unsigned char {
    DataCorrupt,
    HeaderCorrupt,
    MissingHeader,
    MissingData,
    FullQueue,
    MissingPayload
  };

  bool valid() const { return error.none(); }
  void set_error(Err err) { error.set(err); }

  Hit get_header(size_t waveform) const
  {
    return hits[waveform_begin[waveform]];
  }

  size_t waveform_count() const { return waveform_begin.size(); }

  size_t word_count(size_t waveform) const
  {
    return get_header(waveform).word_count();
  }

  short get_word(size_t waveform, size_t word) const
  {
    size_t hit_idx = waveform_begin[waveform] + 1 + word / Hit::words_per_hit;
    uint8_t word_idx = word % Hit::words_per_hit;
    return hits[hit_idx].word(word_idx);
  }

  std::vector<short> copy_waveform(size_t waveform) const
  {
    std::vector<short> data;
    auto words = word_count(waveform);
    data.resize(words);

    for (size_t i = 0; i < words; ++i) {
      data[i] = get_word(waveform, i);
    }
    return data;
  }

  size_t add_waveform(short n_hits)
  {
    waveform_begin.push_back(hits.size());
    hits.resize(hits.size() + n_hits, Hit {uint64_t(0)});
    return waveform_begin.back();
  }

  static Event read(std::ifstream& file)
  {
    Event event {};
    deserialize(file, event.hits);
    deserialize(file, event.waveform_begin);
    deserialize(file, event.timestamp);
    deserialize(file, event.bx_count);
    deserialize(file, event.fec_id);
    deserialize(file, event.error);
    return event;
  }

  void write(std::ofstream& file) const
  {
    serialize(file, hits);
    serialize(file, waveform_begin);
    serialize(file, timestamp);
    serialize(file, bx_count);
    serialize(file, fec_id);
    serialize(file, error);
  }

  size_t byte_size() const
  {
    return hits.size() * sizeof(Hit);
  }
};

class EventSorter {
  public:
  explicit EventSorter(const std::function<void(Event&&)>& event_handler)
      : m_event_handler(event_handler)
  {
  }

  void process(Payload& payload)
  {
    ++m_processed_payloads;

    if (payload.data.size() < Payload::header_size) {
      return;
    }

    // Try to fix eventual alignment problems
    if (enable_remove_caca) {
      remove_caca(payload);
    }

    // Ignore invalid and empty payloads
    if (payload.data_id() != expected_data_id) {
      return;
    }

    for (size_t i = 0; i < payload.n_hits(); ++i) {
      auto hit = payload.hit(i);
      process(hit, payload.fec_id(), payload.timestamp);
    }
  }

  void clear()
  {
    m_event_pool.clear();
    std::fill(m_queues.begin(), m_queues.end(), Queue {});
    m_processed_events = 0;
    m_processed_payloads = 0;
  }

  size_t get_processed_events() const { return m_processed_events; }

  private:
  struct Queue {
    Event* event = nullptr;      // event it belongs to
    short remaining_hits = 0;    // Expected number of hits until the end of the queue
    unsigned int next_index = 0; // where to store new queue data in the event
    bool is_open = false;
    uint8_t data_parity = 0;
    uint8_t expected_data_parity {};
    int words_in_last_hit {};

    void clear() { *this = Queue {}; }
  };

  void process(Hit hit, uint8_t fec_id, long timestamp)
  {
    auto queue_id = hit.queue_index();
    if (queue_id >= queue_size) {
      return;
    }
    auto& queue = m_queues[queue_id];

    switch (hit.pk()) {
    case Hit::HEADER: {
      if (!hit.check_header_integrity(enable_header_fix)) {
        break;
      }
      auto bx_count = hit.bx_count();
      auto& event = m_event_pool[hit.bx_count()];

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
        queue.event->set_error(Event::Err::MissingData);
      }
      close_queue(queue);
    } break;

    case Hit::TRIGTOOEARLY: {
      store_hit(queue, hit);
      close_queue(queue);
    } break;
    }
  }

  void open_queue(Queue& queue, Hit hit, Event& new_event)
  {
    ++new_event.open_queues;

    if (queue.event != nullptr) {
      --queue.event->open_queues;

      if (queue.is_open) {
        queue.event->set_error(Event::Err::MissingHeader);
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

  static void store_hit(Queue& queue, Hit hit)
  {
    // We missed the queue header, we will ignore this hit
    if (!queue.is_open) {
      return;
    }

    // Queue got more or less hits than expected
    if (queue.remaining_hits <= 0) {
      queue.event->set_error(Event::MissingData);
      close_queue(queue);
      return;
    }

    if (hit.full()) {
      queue.event->set_error(Event::FullQueue);
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

  static void close_queue(Queue& queue)
  {
    // Check data parity
    if (queue.event != nullptr && (queue.data_parity != queue.expected_data_parity)) {
      queue.event->set_error(Event::DataCorrupt);
    }
    queue.is_open = false;
  }

  void process(Event& event)
  {
    auto bx_count = event.bx_count;
    // Ignore initial and incomplete events
    if ((event.valid() || process_invalid_events) && m_processed_events > 3) {
      // Process event
      m_event_handler(std::move(event));
    }
    // Remove event from pool
    m_event_pool.erase(bx_count);

    ++m_processed_events;
  }

  void remove_caca(Payload& payload)
  {
    auto& data = payload.data;
    long header_offset = static_cast<long>(payload.header_offset);
    long hit_offset = static_cast<long>(payload.hit_offset);

    // First we ignore all the cacas
    if (data[0] == 0xca) {
      // we have caca at the beginning

      if (data[data.size() - 1] == 0xca && data[data.size() - 2] == 0xca) {
        // we have caca at the end, the entire packet is useless
        return;
      }

      // Damn! the data is misaligned
      auto first_valid = std::find_if(data.begin(), data.end(),
          [](auto x) { return x != 0xcaU; });
      header_offset = first_valid - data.begin();
      hit_offset = header_offset + static_cast<long>(Payload::header_size);
    }

    static constexpr unsigned int min_hits = 6;

    auto remaining_bytes = data.size() - header_offset;
    if (remaining_bytes < sizeof(Hit) * (min_hits + 1) + Payload::header_size) {
      return;
    }

    payload.header_offset = header_offset;
    if (payload.data_id() != expected_data_id) {
      return;
    }

    // Then we check if the alignment didn't change

    unsigned char new_alignment = 0;
    // first we test if the previous alignment is still valid
    if (check_alignment(data.begin() + hit_offset + m_alignment, data.end())) {
      new_alignment = m_alignment;
    } else {
      // The alignment changed!
      for (auto index = data.begin() + hit_offset;
           !check_alignment(index, data.end()) && new_alignment < sizeof(Hit);
           ++new_alignment, ++index) {
      }

      if (new_alignment == 8) {
        throw std::runtime_error("DEU RUIM NO ALINHAMENTO");
      }
    }

    int stored_leftover = static_cast<int>(sizeof(Hit)) - new_alignment;
    if (new_alignment != 0) {
      if (m_alignment == new_alignment) {
        // process leftover
        std::copy(data.begin() + hit_offset,
            data.begin() + hit_offset + new_alignment,
            m_leftover.begin() + stored_leftover);
        process(Hit(m_leftover.data()), payload.fec_id(), payload.timestamp);
      }

      std::copy(data.end() - stored_leftover, data.end(), m_leftover.begin());
    }

    m_alignment = new_alignment;
    payload.hit_offset = hit_offset + new_alignment;
  }

  // check if the alignment is correct using the constant bits
  static bool check_alignment(payload_data::iterator begin,
      payload_data::iterator end)
  {
    const auto n_hits = (end - begin) / sizeof(Hit);

    bool alignment_found = true;
    for (int i = 0; alignment_found && i < n_hits; ++i, begin += sizeof(Hit)) {
      // TODO: we should improve the tested bits
      // pk11110q qqqq...
      alignment_found = (*begin & 0b00000010U) == 0 && (*(begin + 4) & 0b01000000U) == 0;
    }
    return alignment_found;
  }

  public:
  static constexpr uint32_t expected_data_id = 0x564d33U; // VM3
  bool process_invalid_events = false;                    // The events marked invalid will also be passed to the event_handler
  bool enable_remove_caca = true;                         // Remove the caca bytes and try to fix the misalignment cause by it
  bool enable_header_fix = false;                         // Try to fix headers that fail the Hamming code test

  private:
  static constexpr size_t queue_size = 16;
  std::unordered_map<uint32_t, Event> m_event_pool {};
  std::array<Queue, queue_size> m_queues {}; // where to store next queue data
  size_t m_processed_events = 0;
  size_t m_processed_payloads = 0;
  unsigned char m_alignment = 0;
  std::array<uint8_t, sizeof(Hit)> m_leftover {};
  std::function<void(Event&&)> m_event_handler;
};

} // namespace sampasrs