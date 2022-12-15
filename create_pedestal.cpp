#include <sampasrs/root_fix.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <TFile.h>
#include <TTreeReader.h>
#include <TTreeReaderArray.h>

int main(int argc, const char* argv[])
{
  if (argc < 2) {
    std::cerr << "Usage: create_pedestal <Pedestal_file.root>\n";
    return 1;
  }
  const char* input_name = argv[1];

  auto input_path = std::filesystem::path(input_name);
  auto rootfname = input_path.replace_extension("PedFile.txt").string();

  TFile file(input_name, "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");

  struct Pedestal {
    short sampa = -1;
    short channel = -1;
    size_t count = 0;
    double sum = 0;
    double sum_squared = 0;
  };

  std::map<int, Pedestal> channels {};

  int const NumEvts = 10; // Number of events for pedestal file

  int event_id = 0;
  while (reader.Next() && event_id < NumEvts) {
    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) {
      const int global_channel = (sampa[i] - 8) * 32 + channel[i];
      auto& pedestal = channels[global_channel];

      // Fill info on first occurrence
      if (pedestal.count == 0) {
        pedestal.sampa = sampa[i];
        pedestal.channel = channel[i];
      }

      for (size_t j = 2; j < event_words[i].size(); ++j) {
        pedestal.sum += event_words[i][j];
        pedestal.sum_squared += std::pow(event_words[i][j], 2);
        ++pedestal.count;
      }
    }
    event_id++;
  }

  std::cout << "Generating pedestal file: " << rootfname << "\n";
  std::ofstream TxtOutFile(rootfname);

  // TxtOutFile << "sampa channel global_channel mean std\n";

  for (auto& [global_channel, pedestal] : channels) {
    const double mean = pedestal.sum / static_cast<double>(pedestal.count);
    const double var = pedestal.sum_squared / static_cast<double>(pedestal.count) - std::pow(mean, 2);
    TxtOutFile << global_channel << " " << mean << " " << std::sqrt(var) << "\n";
    // TxtOutFile << pedestal.sampa << " " << pedestal.channel << " " << global_channel << " " << mean << " " << std::sqrt(var) << "\n";
  }

  TxtOutFile.close();
  return 0;
}
