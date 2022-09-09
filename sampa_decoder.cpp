#include <sampasrs/root_fix.hpp>

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

  if (argc < 3) {
    std::cerr << "Usage: sampa_decoder <input file> <output file>\n";
    return 1;
  }
  std::string input_name = argv[1];
  std::string output_name = argv[2];

  // Read binary or pcap file
  auto file_extension = std::filesystem::path(input_name).extension().string();
  bool read_binary = file_extension == ".raw";

  size_t n_events = 0;
  TFile out_file(output_name.c_str(), "recreate");
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

  size_t output_bytes = 0;
  // TODO: Replace this with a proper tree writer
  auto save_event = [&](Event &&event) {
    ++n_events;
    output_bytes += event.hits.size() * sizeof(Hit);

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
    //   std::cout << hit.to_string() << "\n";
    // }
  };

  EventSorter sorter(save_event);
  sorter.process_invalid_events = false;
  sorter.do_remove_caca = true;

  size_t input_bytes = 0;
  auto start = std::chrono::high_resolution_clock::now();

  if (read_binary) {
    std::cout << "Reading raw file\n";
    std::ifstream input_file(input_name, std::ios::binary);
    if (!input_file) {
      std::cerr << "Unable to open file\n";
      return 1;
    }
    payload_data data(Payload::size, 0);

    while (!input_file.eof()) {
      input_bytes += Payload::size;
      auto payload = Payload::read(input_file);
      sorter.process(payload);
    }
  } else {
    std::cout << "Reading pcap file\n";
    FileSniffer input_file(input_name);

    auto sniffer_callback = [&](Packet &packet) {
      Payload payload(std::move(packet));
      input_bytes += Payload::size;
      sorter.process(payload);
      return true;
    };
    input_file.sniff_loop(sniffer_callback);
  }

  auto duration = std::chrono::duration<float, std::milli>(
                      std::chrono::high_resolution_clock::now() - start)
                      .count();

  std::cout << "Duration " << duration << " ms\n";
  std::cout << ((float)input_bytes / 1024.f / 1024.f) / duration * 1000
            << " MB/s\n";
  std::cout << "Input size  " << (float)input_bytes / 1024.f / 1024.f
            << " MB\n";
  std::cout << "Output size " << (float)output_bytes / 1024.f / 1024.f
            << " MB\n";
  out_file.Write();
  std::cout << "Valid events " << n_events << "\n";
  std::cout << "Total events " << sorter.get_processed_events() << "\n";
};
