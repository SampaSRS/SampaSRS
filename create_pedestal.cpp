
#include <cmath>
#include <math.h>
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
    std::cerr << "Usage: create_pedestal <Pedestal_file.root>\n";
    return 1;
  }
  const char* input_name = argv[1];

  auto input_path = std::filesystem::path(input_name);
  bool read_raw = input_path.extension().string() == ".root";
  auto rootfname = input_path.replace_extension("PedFile.txt").string();

  size_t n_events = 0;
  std::cout << "Generating pedestal file: " << rootfname << "\n";
  std::ofstream TxtOutFile(rootfname);

  TFile file(input_name, "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");

  std::vector<double> sum(128);
  std::vector<double> count(128);
  std::vector<double> sumsq(128);
  std::vector<double> mean(128);
  std::vector<double> Var(128);
  std::vector<double> Chn(128);
  std::vector<double> Sampa(128);
  int gl_chn = 0;

  int const NumEvts = 10; // Number of events for pedestal file

  int event_id = 0;
  while (reader.Next() || event_id < NumEvts) {
    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) {
      gl_chn = (sampa[i] - 8) * 32 + channel[i];
      Chn[gl_chn] = channel[i];
      Sampa[gl_chn] = sampa[i];
      // std::cout<<gl_chn<<std::endl;
      for (size_t j = 2; j < event_words[i].size(); ++j) {
        sum[gl_chn] += event_words[i][j];
        sumsq[gl_chn] += pow(event_words[i][j], 2);
        count[gl_chn]++;
        // if(sampa[i]==9&&channel[i]==16)std::cout<<event_words[i][j]<<std::endl;
      }
    }
    event_id++;
  }

  for (int k = 0; k < 128; k++) {
    if (count[k] == 0) {
      // TxtOutFile <<Sampa[k]<<" "<<Chn[k]<<" "<< -1 <<" "<<-1<<std::endl; //colocar qual valor? negativo não manda nada
    } else {
      mean[k] = sum[k] / count[k];
      Var[k] = sumsq[k] / count[k] - pow(sum[k] / count[k], 2);
      if (Var[k] == 0) {
        // TxtOutFile <<Sampa[k]<<" "<<Chn[k]<<" "<<mean[k]<<" "<<-1<<std::endl;
      } else {
        // TxtOutFile <<Sampa[k]<<" "<<Chn[k]<<" "<<mean[k]<<" "<<sqrt(Var[k])<<std::endl;
        TxtOutFile << k << " " << mean[k] << " " << sqrt(Var[k]) << std::endl;
      }
    }
  }

  TxtOutFile.close();
  return 0;
}
