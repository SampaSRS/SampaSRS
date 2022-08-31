#include <fmt/core.h>
#include <sampasrs/sampasrs.h>
#include <tins/tins.h>

#include <cstddef>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, const char *argv[]) {
  using namespace Tins;
  using namespace sampasrs;

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
        line =
            fmt::format("Pk {} Queue {:2d} Bx_count {} Word_count {} "
                        "Ch_addr {:2d} sampa_addr {:2d}\n",
                        hit.pk(), hit.queue(), hit.bx_count(), hit.word_count(),
                        hit.channel_addr(), hit.sampa_addr());
        std::cout << line;
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
