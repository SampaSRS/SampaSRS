// Replay captured packets stored in a .pcapng

#include <tins/tins.h>

#include <chrono>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, const char* argv[])
{
  if (argc < 2) {
    std::cerr << "No file name\n";
    return 1;
  }
  std::string file_name = argv[1];

  auto initial_timestamp = std::chrono::microseconds(0);
  auto now = std::chrono::steady_clock::now();

  Tins::PacketSender sender;
  auto send_packets = [&](Tins::Packet& packet) {
    auto timestamp = std::chrono::microseconds(packet.timestamp());
    if (initial_timestamp.count() == 0) {
      initial_timestamp = timestamp;
    }
    auto dt = timestamp - initial_timestamp;
    std::this_thread::sleep_until(now + dt);
    sender.send(packet.pdu()->rfind_pdu<Tins::IP>());
    return true;
  };

  Tins::FileSniffer sniffer(file_name);
  sniffer.sniff_loop(send_packets);
}