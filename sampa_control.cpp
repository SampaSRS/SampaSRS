#include <sampasrs/slow_control.h>

#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

int main(int argc, const char *argv[]) {
  sampasrs::SlowControl sampa{};
  sampa.fec_address = "10.0.0.2";
  sampa.receive_timeout = 1000; // milli seconds

  std::string file_name = "config.txt";
  if (argc > 1) {
    file_name = argv[1];
  }

  std::string command_line{};
  std::ifstream config_file(file_name);
  if (config_file) {
    std::cout << "Executing config file: " << file_name << "\n";

    while (std::getline(config_file, command_line)) {
      std::cout << command_line << "\n";
      bool ok = sampa.send_command_line(command_line);
      if (!ok) {
        std::cerr << "Error sending command\n";
        return 1;
      }
    }
  }

  for (;;) {
    std::cin >> command_line;

    if (command_line == "x" || command_line == "exit") {
      break;
    }

    bool ok = sampa.send_command_line(command_line);
    if (!ok) {
      std::cerr << "Error sending command\n";
      return 1;
    }
  }
}