
#include <sampasrs/root_fix.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <string_view>
#include <vector>

#include <TFile.h>
#include <TTreeReader.h>
#include <TTreeReaderArray.h>

int main(int argc, const char* argv[])
{
  if (argc < 2) {
    std::cerr << "Usage: sampa_decoder <input file.raw>\n";
    return 1;
  }
  const char* input_name = argv[1];

  auto input_path = std::filesystem::path(input_name);
  bool read_raw = input_path.extension().string() == ".root";
  auto rootfname = input_path.replace_extension("_Clst.root").string();

  size_t n_events = 0;
  std::cout << "generating Clustered file: " << rootfname << "\n";
  TFile out_file(rootfname.c_str(), "recreate");

  TFile file(input_name, "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");

  int event_id = 0;
  while (reader.Next() && event_id < 10) {
    auto& event_words = *words;
    long sum = 0;
    for (size_t i = 0; i < event_words.size(); ++i) {
      std::cout << channel[i] << " " << sampa[i] << std::endl;
      for (size_t j = 0; j < event_words[i].size(); ++j) {
        sum += event_words[i][j];
      }
    }

    std::cout << event_id << " " << sum << "\n";
    ++event_id;
  }

  return 0;
}
