#include <sampasrs/acquisition.hpp>
#include <sampasrs/slow_control.hpp>
#include <sampasrs/utils.hpp>

#include <fmt/format.h>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

int main(int argc, const char* argv[])
{
  int target_event_count = 100;
  int event_count = 0;

  auto event_handler = [&](sampasrs::Event&& event) {
    if (!event.valid() || event_count >= target_event_count) {
      return;
    }
    ++event_count;
    // Process valid events here
    // ...
  };

  // Sampa config
  std::vector<std::string> commands {
      "reset_fec",
      "reset_sampas",
      // the rest of the commands...
  };

  // Send commands
  sampasrs::SlowControl control {};
  for (const auto& cmd : commands) {
    control.send_command(cmd);
  }

  {
    // Start aquisition and event processing on the background, no raw file stored
    sampasrs::Acquisition pedestal_run(event_handler);

    // Loop until we are done
    while (event_count < target_event_count) {
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // Print some infos during acquisition
      const auto stats = pedestal_run.get_stats();
      fmt::print("Events recorded: {} | Events/s {:5.2f} | Invalid events {:5.2f} % | Buffer usage: {:5.2f} % | Net speed: {:5.2f} MB/s | Write speed {:5.2f} MB/s\n",
          stats.valid_events, stats.valid_event_rate, stats.invalid_event_ratio, stats.write_buffer_use, stats.read_speed, stats.write_speed);
    }
  } // pedestal_run goes out of scope and will be destroyed, ending the acquisition

  control.send_command("stop");
  // You can write the pedestal file bellow ...
}
