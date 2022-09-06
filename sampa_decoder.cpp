#include <TFile.h>
#include <TTree.h>
#include <fmt/core.h>
#include <sampasrs/aquisition.hpp>
#include <tins/tins.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, const char *argv[]) {
  using namespace Tins;
  using namespace sampasrs;

  if (argc < 2) {
    std::cerr << "Usage: sampa_decoder <input file>\n";
    return 1;
  }
  std::string file_name = argv[1];

  // Read binary or pcap file
  auto file_extension = std::filesystem::path(file_name).extension().string();
  bool read_binary = file_extension == ".raw";

  size_t n_events = 0;
  TFile out_file("waveform.root", "recreate");
  TTree tree("waveform", "Waveform");

  // Tree branches
  uint32_t bx_count{};
  uint8_t channel{};
  uint8_t sampa{};
  uint8_t fec_id{};
  long timestamp{};
  std::vector<short> words{};

  tree.Branch("bx_count", &bx_count, "bx_counter/i");
  tree.Branch("channel", &channel, "channel/b");
  tree.Branch("sampa", &sampa, "sampa/b");
  tree.Branch("fec_id", &fec_id, "fec_id/b");
  tree.Branch("timestamp", &timestamp, "timestamp/L");
  tree.Branch("words", &words);

  // TODO: Replace this with a proper tree writer
  auto save_event = [&](Event &&event) {
    ++n_events;

    for (size_t waveform = 0; waveform < event.waveform_count(); ++waveform) {
      const auto header = event.get_header(waveform);
      bx_count = header.bx_count();
      channel = header.channel_addr();
      sampa = header.sampa_addr();
      fec_id = event.fec_id;
      timestamp = event.timestamp;
      words = event.copy_waveform(waveform);
      tree.Fill();
    }

    // Print events info
    // fmt::print("Bx_count {:7d} - Channels {:3d}\n", event.bx_count,
    //            event.waveform_count());

    // Print header info
    // for (auto hit : event.hits) {
    //   std::string line;
    //   if (hit.pk() == Hit::HEADER) {
    //     line =
    //         fmt::format("Pk {} Queue {:2d} Bx_count {} Word_count {} "
    //                     "Ch_addr {:2d} sampa_addr {:2d}\n",
    //                     hit.pk(), hit.queue(), hit.bx_count(),
    //                     hit.word_count(), hit.channel_addr(),
    //                     hit.sampa_addr());
    //     std::cout << line;
    //   } else {
    //     auto parity = hit.compute_data_parity();
    //     line = fmt::format("Pk {} Queue {:2d} Words {:4d} {:4d} {:4d} {:4d} "
    //                        "{:4d} Full {:d}\n",
    //                        hit.pk(), hit.queue(), hit.word0(), hit.word1(),
    //                        hit.word2(), hit.word3(), hit.word4(),
    //                        hit.full());
    //   }
    // }
  };

  EventSorter sorter(save_event);

  if (read_binary) {
    std::cout << "Reading raw file\n";
    std::ifstream input_file(file_name, std::ios::binary);
    if (!input_file) {
      std::cerr << "Unable to open file\n";
      return 1;
    }
    const size_t payload_size = 1032;
    payload_data data(payload_size, 0);

    while (!input_file.eof()) {
      auto payload = Payload::read(input_file);
      sorter.process(payload);
    }
  } else {
    std::cout << "Reading pcap file\n";
    FileSniffer sniffer(file_name);
    size_t processed_bytes = 0;
    auto start = std::chrono::high_resolution_clock::now();

    auto sniffer_callback = [&](Packet &packet) {
      Payload payload(std::move(packet));
      processed_bytes += Payload::size;
      sorter.process(payload);
      return true;
    };
    sniffer.sniff_loop(sniffer_callback);

    auto dt = std::chrono::duration<float, std::milli>(
                  std::chrono::high_resolution_clock::now() - start)
                  .count();
    std::cout << "Duration " << dt << " ms\n";
    std::cout << "Size " << processed_bytes << " Bytes\n";
    std::cout << ((float)processed_bytes / 1024.f / 1024.f) / dt * 1000
              << " MB/s\n";
  }
  out_file.Write();
  std::cout << "Valid events " << n_events << "\n";
  std::cout << "Total events " << sorter.get_processed_events() << "\n";
};
