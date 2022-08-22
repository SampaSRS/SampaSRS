#include "sampasrs.h"
#include <fmt/core.h>

#include <chrono>
#include <cstddef>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using sc = std::chrono::steady_clock;

int main(int argc, char *argv[]) {
  if (argc < 2) {
    std::cerr << "Usage: fake_packets <host> <rate in MB/s>\n";
    return 1;
  }

  float mb_per_second = 125;
  if (argc > 2) {
    mb_per_second = std::stof(argv[2]);
  }

  PacketSender sender(argv[1], 6006);
  std::cout << "Connected\n";

  std::vector<uint8_t> payload(1032, 0);
  uint16_t stair = 0;
  for (size_t i = 4; i < payload.size(); i += sizeof(stair)) {
    std::memcpy(&payload[i], &stair, sizeof(stair));
    ++stair;
  }

  int n_repeat = 1000;
  float repeat_mb =
      static_cast<float>(payload.size() * n_repeat) / 1024. / 1024.;
  const float dt = repeat_mb / mb_per_second;
  unsigned int sent_packets = 0;
  float sent_data = 0;
  auto start = sc::now();

  for (;;) {
    const auto start_sending = sc::now();
    for (int i = 0; i < n_repeat; ++i) {
      sender.send(payload);
      ++sent_packets;
      std::memcpy(&payload[0], &sent_packets, sizeof(sent_packets));
    }

    sent_data += repeat_mb;
    const auto end = sc::now();
    const std::chrono::duration<float> duration = end - start;
    if (duration.count() > 1) {
      const float rate = sent_data / duration.count();
      fmt::print("Packets sent {} - {:.1f} MB/s\n", sent_packets, rate);
      start = end;
      sent_data = 0;
    }

    std::this_thread::sleep_until(start_sending +
                                  std::chrono::duration<float>(dt));
  }
  return 0;
}
