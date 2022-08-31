#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, const char *argv[]) {
  std::string file_name = "sampasrs.0000.raw";
  if (argc > 1) {
    file_name = argv[1];
  }
  std::ifstream file(file_name, std::ios::binary);
  if (!file) {
    std::cerr << "Unable to open file\n";
    return 1;
  }

  std::array<uint8_t, 1032> payload{};

  uint32_t payload_count = 0;
  while (!file.eof()) {
    bool corrupted = false;
    file.read(reinterpret_cast<char *>(payload.data()), payload.size());
    for (size_t i = 4, value = 0; i < payload.size(); i += 2, ++value) {
      uint16_t input_value;
      std::memcpy(&input_value, &payload[i], sizeof(input_value));
      if (input_value != value) {
        std::cout << "Payload " << payload_count << " corrupted\n";
        std::cout << input_value << " " << value << "\n";
        corrupted = true;
        break;
      }
    }
    if (corrupted) {
      continue;
    }

    unsigned int payload_id;
    std::memcpy(&payload_id, &payload[0], sizeof(payload_id));
    if (payload_id != payload_count) {
      std::cout << "Payloads lost " << payload_count << " - " << payload_id
                << "\n";
      payload_count = payload_id;
    }

    ++payload_count;
  }
  std::cout << "Total payloads " << payload_count << "\n";
}
