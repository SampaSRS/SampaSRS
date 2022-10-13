#include <sampasrs/acquisition.hpp>

#include <fmt/format.h>

#include <chrono>
#include <string>
#include <thread>

int main(int argc, const char* argv[])
{
  std::string file_prefix = "sampasrs";
  if (argc > 1) {
    file_prefix = argv[1];
  }

  std::string address = "10.0.0.2"; // FEC address, used to find which network interface to sniff
  if (argc > 2) {
    address = argv[2];
  }

  const bool save_raw = true;
  sampasrs::Acquisition sampa(file_prefix, save_raw, {}, address); // Start aquisition

  // Loop forever
  while (true) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const auto stats = sampa.get_stats();
    fmt::print("Events recorded: {} | Events/s {:5.2f} | Invalid events {:5.2f} % | Buffer usage: {:5.2f} % | Net speed: {:5.2f} MB/s | Write speed {:5.2f} MB/s\n",
        stats.valid_events, stats.valid_event_rate, stats.invalid_event_ratio, stats.write_buffer_use, stats.read_speed, stats.write_speed);
  }
}
