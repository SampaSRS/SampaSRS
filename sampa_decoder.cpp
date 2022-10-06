#include <sampasrs/root_fix.hpp>

#include <sampasrs/acquisition.hpp>
#include <sampasrs/decoder.hpp>
#include <sampasrs/mapping.hpp>

#include <TFile.h>
#include <TTree.h>
#include <tins/tins.h>

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

int main(int argc, const char* argv[])
{
  using namespace Tins;
  using namespace sampasrs;

  if (argc < 2) {
    std::cerr << "Usage: sampa_decoder <input files>\n";
    return 1;
  }
  const char* input_name = argv[1];

  auto input_path = std::filesystem::path(input_name);
  const auto rootfname = input_path.replace_extension(".root").string();

  size_t n_events = 0;
  std::cout << "generating root file: " << rootfname << "\n";
  TFile out_file(rootfname.c_str(), "recreate");
  TTree tree("waveform", "Waveform");

  // mapping pair creation
  std::unordered_map<int, std::pair<double, double>> map_of_strips = {};

  Mapping_strips(map_of_strips); // change the mapping on mapping.hpp for a diferent detector

  // Tree branches
  uint32_t bx_count {};
  uint8_t fec_id {};
  long timestamp {};
  std::vector<short> channel {};
  std::vector<short> sampa {};
  std::vector<int> PadID {};
  std::vector<double> x {};
  std::vector<double> y {};
  std::vector<std::vector<short>> words {};

  tree.Branch("bx_count", &bx_count, "bx_counter/i");
  tree.Branch("fec_id", &fec_id, "fec_id/b");
  tree.Branch("timestamp", &timestamp, "timestamp/L");
  tree.Branch("channel", &channel);
  tree.Branch("sampa", &sampa);
  tree.Branch("PadID", &PadID);
  tree.Branch("x", &x);
  tree.Branch("y", &y);
  tree.Branch("words", &words);

  size_t output_bytes = 0;
  // TODO: Replace this with a proper tree writer
  auto save_event = [&](Event&& event) {
    ++n_events;
    output_bytes += event.hits.size() * sizeof(Hit);

    fec_id = event.fec_id;
    timestamp = event.timestamp;
    bx_count = event.bx_count;

    for (size_t waveform = 0; waveform < event.waveform_count(); ++waveform) {
      const auto header = event.get_header(waveform);
      channel.push_back((int)header.channel_addr());
      sampa.push_back((int)header.sampa_addr());
      x.push_back(map_of_strips[32 * ((int)header.sampa_addr() - 8) + (int)header.channel_addr()].first);  // only works using sampa from 8 to 11
      y.push_back(map_of_strips[32 * ((int)header.sampa_addr() - 8) + (int)header.channel_addr()].second); // only works using sampa from 8 to 11
      words.push_back(event.copy_waveform(waveform));
    }

    tree.Fill();
    channel.clear();
    sampa.clear();
    x.clear();
    y.clear();
    words.clear();

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
  sorter.enable_remove_caca = true;
  sorter.enable_header_fix = false;

  size_t input_bytes = 0;
  auto start = std::chrono::high_resolution_clock::now();

  for (int i = 1; i < argc; ++i) {
    const auto file_name = std::filesystem::path(argv[i]);
    const auto file_extension = file_name.extension().string();

    std::cout << "Processing file: " << file_name;
    if (file_extension == ".raw") {
      std::cout << "Reading raw file\n";
      std::ifstream input_file(file_name, std::ios::binary);
      if (!input_file) {
        std::cerr << "Unable to open file\n";
        return 1;
      }

      while (!input_file.eof()) {
        auto payload = Payload::read(input_file);
        input_bytes += payload.byte_size();
        sorter.process(payload);
      }
    } else if (file_extension == ".rawevent") {
      std::cout << "Reading raw events file\n";
      std::ifstream input_file(file_name, std::ios::binary);
      if (!input_file) {
        std::cerr << "Unable to open file\n";
        return 1;
      }

      while (!input_file.eof()) {
        auto event = Event::read(input_file);
        input_bytes += event.byte_size();
        save_event(std::move(event));
      }
    } else {
      std::cout << "Reading pcap file\n";
      FileSniffer input_file(file_name);

      auto sniffer_callback = [&](Packet& packet) {
        Payload payload(std::move(packet));
        input_bytes += payload.byte_size();
        sorter.process(payload);
        return true;
      };
      input_file.sniff_loop(sniffer_callback);
    }
  }

  out_file.Write();

  auto duration = std::chrono::duration<float, std::milli>(std::chrono::high_resolution_clock::now() - start).count();

  const auto ibytes = static_cast<float>(input_bytes);
  const auto obytes = static_cast<float>(output_bytes);
  std::cout << "Duration " << duration << " ms\n";
  std::cout << (ibytes / 1024.f / 1024.f) / duration * 1000 << " MB/s\n";
  std::cout << "Input size  " << ibytes / 1024.f / 1024.f << " MB\n";
  std::cout << "Output size " << obytes / 1024.f / 1024.f << " MB\n";
  std::cout << "Valid events " << n_events << "\n";
  std::cout << "Total events " << sorter.get_processed_events() << "\n";
};
