#include <TFile.h>
#include <TTree.h>
#include <fmt/core.h>
#include <sampasrs/aquisition.h>
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

  std::string file_name = "/home/badaro/Downloads/Run49.pcapng";
  if (argc > 1) {
    file_name = argv[1];
  }

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
  std::vector<short> words{};

  tree.Branch("bx_count", &bx_count);
  tree.Branch("channel", &channel);
  tree.Branch("sampa", &sampa);
  tree.Branch("fec_id", &fec_id);
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
      input_file.read(reinterpret_cast<char *>(data.data()),
                      static_cast<long>(data.size()));
      Payload payload(std::move(data));
      sorter.process(payload);
    }
  } else {
    std::cout << "Reading pcap file\n";
    FileSniffer sniffer(file_name);
    size_t processed_bytes = 0;
    auto start = std::chrono::high_resolution_clock::now();

    auto sniffer_callback = [&](PDU &pdu) {
      auto &data = pdu.rfind_pdu<RawPDU>().payload();
      processed_bytes += data.size();
      sorter.process(Payload(std::move(data)));
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
