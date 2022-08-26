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
using hit_data = uint64_t;

template <typename T, unsigned char start, unsigned char end>
constexpr T get_bit_range(const T input) {
  constexpr T bit_mask = (T{1} << (end - start)) - 1;
  return (input >> start) & bit_mask;
}

template <typename T> T read_from_buffer(const uint8_t *ptr) {
  T output;
  std::memcpy(&output, ptr, sizeof(T));
  boost::endian::big_to_native_inplace(output);
  return output;
}

struct Hit {
  Hit() = default;
  explicit Hit(hit_data new_data) : data{new_data} {}
  explicit Hit(const uint8_t *ptr) : data{read_from_buffer<hit_data>(ptr)} {}

  template <unsigned char start, unsigned char end> uint64_t bit_range() const {
    return get_bit_range<uint64_t, start, end>(data);
  }

  enum : uint8_t { DATA = 0, HEADER = 1, END = 2, TRIGTOOEARLY = 3 };

  uint8_t pk() const { return bit_range<62, 64>(); }
  uint8_t queue() const { return bit_range<52, 58>(); }
  // Queue index from 0 to 15
  uint8_t queue_index() const { return queue() - 1; }

  // Header fields
  uint8_t payload_parity() const { return bit_range<51, 52>(); }
  uint32_t bx_counter() const { return bit_range<32, 51>(); }
  uint8_t bc_count() const { return bit_range<29, 32>(); }
  uint8_t channel_addr() const { return bit_range<24, 29>(); }
  uint8_t sampa_addr() const { return bit_range<20, 24>(); }
  uint16_t word_count() const { return bit_range<10, 20>(); }
  uint8_t pkt() const { return bit_range<7, 10>(); }
  uint8_t header_parity() const { return bit_range<6, 7>(); }
  uint8_t hamming() const { return bit_range<0, 6>(); }

  // Number of hit data expected for this queue, including header
  uint16_t hit_count() const {
    static constexpr uint16_t words_per_hit = 5;
    // Ceil integer division plus queue header
    return (word_count() + words_per_hit - 1) / words_per_hit + 1;
  }

  // Data fields
  uint16_t word4() const { return bit_range<42, 52>(); }
  uint16_t word3() const { return bit_range<32, 42>(); }
  bool full() const { return bit_range<31, 32>(); }
  uint16_t word2() const { return bit_range<20, 30>(); }
  uint16_t word1() const { return bit_range<10, 20>(); }
  uint16_t word0() const { return bit_range<0, 10>(); }

  uint16_t word(uint8_t i) {
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
    }
    return 0;
  }

  hit_data data;
};

class Payload {
  static constexpr size_t hit_start_pos = 16;

public:
  Payload(payload_data &data) : m_data(data) {}

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
  WaveForm(const hit_data *data, size_t size) : m_data{data}, m_size{size} {
    m_size = Hit(m_data[0]).hit_count();
  }
  Hit header() const { return Hit(m_data[0]); }
  uint16_t operator[](size_t idx) const {
    return Hit(m_data[1 + idx / 5]).word(idx % 5);
  }
  size_t size() const { return m_size; }

private:
  const hit_data *m_data;
  size_t m_size;
};

struct Event {
  std::vector<Hit> hits{};
  int channel_count = 0; // Number of channels in this event
  uint32_t bx_counter = 0;
  short open_queues = 0; // Number of channels receiving data
  bool valid = true;
  bool empty() const { return channel_count == 0; }
};

class EventSorter {
public:
  static constexpr size_t queue_size = 16;

  struct Queue {
    Event *event = nullptr;      // event it belongs to
    unsigned int next_index = 0; // where to store new queue data in the event
    short remaining_hits =
        0; // Expected number of hits until the end of the queue
    bool is_open = false;
  };

  EventSorter(const std::function<void(Event)> &event_handler)
      : m_event_handler(event_handler) {}

  void process(Payload &payload) {
    ++m_processed_payloads;
    // Invalid payload
    if (payload.data_id() != 0x564d33) {
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
  void process(Hit hit) {
    auto queue_id = hit.queue_index();
    if (queue_id >= queue_size) {
      return;
    }
    auto &queue = m_queues[queue_id];

    switch (hit.pk()) {
    case Hit::HEADER: {
      auto bx_counter = hit.bx_counter();
      auto &event = m_event_pool[hit.bx_counter()];
      event.bx_counter = bx_counter;

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

    ++new_event.channel_count;
    new_event.hits.resize(new_event.hits.size() + queue.remaining_hits);
    store_hit(queue, hit);
  }

  void store_hit(Queue &queue, Hit hit) {
    // We missed this queue header and we will ignore it
    if (!queue.is_open) {
      return;
    }

    if (queue.remaining_hits <= 0) {
      queue.event->valid = false;
      close_queue(queue);
      return;
    }
    queue.event->hits[queue.next_index] = hit;
    ++queue.next_index;
    --queue.remaining_hits;
  }

  void close_queue(Queue &queue) { queue.is_open = false; }

  void invalidate_events() {
    for (auto &bx_event : m_event_pool) {
      bx_event.second.valid = false;
    }
  }

  void dump(Event &event) {
    // Move event data to output buffer
    // Ignore first events and incomplete events
    if (event.valid && m_processed_events > 3) {
      m_event_handler(std::move(event));
    }
    // Remove event from pool
    m_event_pool.erase(event.bx_counter);

    ++m_processed_events;
  }

private:
  std::function<void(Event)> m_event_handler;
  std::unordered_map<uint32_t, Event> m_event_pool{};
  std::array<Queue, queue_size> m_queues{}; // where to store next queue data
  size_t m_processed_events = 0;
  size_t m_processed_payloads = 0;
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
        line = fmt::format("Pk {} Queue {:2d} Bx_counter {} Word counter {} "
                           "Ch_addr {:2d} sampa_addr {:2d} Header_par {}\n",
                           hit.pk(), hit.queue(), hit.bx_counter(),
                           hit.word_count(), hit.channel_addr(),
                           hit.sampa_addr(), hit.header_parity());
        std::cout << line;
      } else {
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
