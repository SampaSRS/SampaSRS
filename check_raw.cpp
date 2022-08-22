#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, const char *argv[]) {
  std::string file_name = "sampasrs.raw";
  if (argc > 1) {
    file_name = argv[1];
  }
  std::ifstream file(file_name, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Unable to open file");
  }

  std::array<uint8_t, 1032> payload;

  uint32_t payload_count = 0;
  while (!file.eof()) {
    file.read(reinterpret_cast<char *>(payload.data()), payload.size());
    unsigned int payload_id;
    std::memcpy(&payload_id, &payload[0], sizeof(payload_id));
    if (payload_id != payload_count) {
      std::cout << "Payloads lost " << payload_count << " - " << payload_id
                << "\n";
      payload_count = payload_id;
    }

    for (uint16_t i = 4, value = 0; i < payload.size(); i += 2, ++value) {
      uint16_t input_value;
      std::memcpy(&input_value, &payload[i], sizeof(input_value));
      if (input_value != value) {
        std::cout << "Payload " << payload_count << " corrupted\n";
        break;
      }
    }
    ++payload_count;
  }
  std::cout << "Total payloads " << payload_count << "\n";
}
