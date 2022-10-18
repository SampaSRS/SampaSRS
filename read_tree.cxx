#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>

#include "TFile.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"


void Map_pedestal(std::unordered_map<int,double> &my_map)
{
    // Create an unordered_map of three values glchn(sampa*32+Chn)    
    std::ifstream mapfile;
    int glchn=0;
    double baseline=0;
    double sigma=0;
    mapfile.open("/home/geovaneg/Documents/Aquisicao-SampaSRS/Run6x_pedestal-0000.PedFile.txt");
    while (true) 
    { 
      mapfile >> glchn;
      mapfile >> baseline;
      mapfile >> sigma;
      my_map[glchn] = {baseline+3*sigma};
      // std::cout << glchn << " " << baseline <<" "<<sigma<<std::endl;
      if( mapfile.eof() ) 
      {
        break;
      }
    }
    mapfile.close();
}



void read_tree(std::string_view file_name = "/home/geovaneg/Documents/Aquisicao-SampaSRS/Run60-0000.root")
{
  if (file_name.empty()) {
    return; // just a precaution
  }

  std::unordered_map<int, double> map_of_pedestals = {};

  Map_pedestal(map_of_pedestals); // change the mapping on mapping.hpp for a diferent detector

  for(int i =0; i<128; i++)
  {
    std::cout << map_of_pedestals[i] <<std::endl;
  }
//   TFile file(file_name.data(), "READ");
//   TTreeReader reader("waveform", &file);
//   TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
//   TTreeReaderArray<short> sampa(reader, "sampa");
//   TTreeReaderArray<short> channel(reader, "channel");

// int event_id = 0;
//   while (reader.Next() && event_id < 10) {
//     auto& event_words = *words;
//     long sum = 0;
//     for (size_t i = 0; i < event_words.size(); ++i) {
//       std::cout << channel[i] <<" "<<sampa[i]<<std::endl;
//       for (size_t j = 0; j < event_words[i].size(); ++j) {
//         sum += event_words[i][j];
//       }
//     }

//     std::cout << event_id << " " << sum << "\n";
//     ++event_id;
//   }

}
