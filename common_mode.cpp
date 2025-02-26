#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <filesystem>


#include "TFile.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"
#include "TGraph.h"

void Map_pedestal(std::string const& pedestal_file, std::unordered_map<int, std::pair<double, double>> &my_map)
{
    // Create an unordered_map of three values glchn(sampa*32+Chn)    
    std::ifstream mapfile;
    int glchn=0;
    double baseline=0;
    double sigma=0;
    mapfile.open(pedestal_file);
    while (true) 
    { 
      mapfile >> glchn;
      mapfile >> baseline;
      mapfile >> sigma;
      if(sigma==0)
      {
        my_map[glchn] = {baseline,1000}; //canais vem com baseline com todos valores igual sigma==0/ matar esse canal
      }
      else
      {
        my_map[glchn] = {baseline,sigma};
      }
      // std::cout << glchn << " " << my_map[glchn].first <<" "<<my_map[glchn].second<<std::endl;
      

      if( mapfile.eof() ) 
      {
        break;
      }
    }
    mapfile.close();
  
    // for(int k=0;k<my_map.size();k++) {std::cout << my_map[k].first<<" "<<my_map[k].second <<std::endl;}
}




int main(int argc, char *argv[])
{
  time_t start = 0;
  time_t end =0;
  time(&start);

  if(argc != 3)
  {
    std::cout << "Usage =: ./common_mode <pedestal_file.txt> <data_file.root>" << std::endl;
    return 0;
  }

  std::string pedestal_file = argv[1];
  std::string file_name = argv[2];
   
 if (file_name.empty() || pedestal_file.empty()) {
    std::cout <<"Empty files?" << std::endl;
    return 0; // just a precaution
  }

  auto input_path = std::filesystem::path(file_name);
  auto rootfname = input_path.replace_extension("CM.txt").string();
  auto rootfname2 = input_path.replace_extension("Pedestal2.txt").string();


  TFile file(file_name.data(), "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");
  TTreeReaderArray<double> x(reader, "x");


  std::unordered_map<int, std::pair<double, double>> map_of_pedestals = {};

  Map_pedestal(pedestal_file, map_of_pedestals); // change the mapping on mapping.hpp for a diferent detector

  int gl_chn=0;
  int E_max=0;

  std::vector<double> time_wave (501);   // 1000 zero-initialized elements

  int evt=0;

  struct Pedestal {
    short sampa = -1;
    short channel = -1;
    size_t count = 0;
    double sum = 0;
    double sum_squared = 0;
  };

  std::map<int, Pedestal> channels {};


  std::cout << "Generating pedestal file: " << rootfname << "\n";
  std::ofstream TxtOutFile(rootfname);
  std::ofstream TxtOutPedestal2(rootfname2);

 
int event_id = 0;
  while (reader.Next()) 
  {
    auto& event_words = *words;
    // std::cout<< event_words.size() <<std::endl;
    for (size_t i = 0; i < event_words.size(); ++i) //loop nos canais
    {
      
      gl_chn = 32*(sampa[i])+channel[i];
      auto& pedestal = channels[gl_chn];
      
    // Fill info on first occurrence
      if (pedestal.count == 0) 
      {
        pedestal.sampa = sampa[i];
        pedestal.channel = channel[i];
      }

      for (size_t j = 2; j < event_words[i].size(); ++j) 
      {
        pedestal.sum += event_words[i][j]-map_of_pedestals[gl_chn].first;
        pedestal.sum_squared += std::pow(event_words[i][j]-map_of_pedestals[gl_chn].first, 2);
        ++pedestal.count;
      }


      for (size_t j = 2; j < event_words[i].size(); ++j) //loop nas palavras
      {
        if(event_words[i][j]!=0 && map_of_pedestals[gl_chn].first!=1000) //remove os zeros(canais com defeito) remove pedestais problematicos
        {
          time_wave.at(j-2) += (event_words[i][j]-map_of_pedestals[gl_chn].first)/(double)event_words.size();  //cria um vetor temporal e adiciona em cada coordenada de tempo valor-baseline
          // std::cout <<gl_chn<<" "<<j<<" "<< event_words[i][j]<<" "<<map_of_pedestals[gl_chn].first<<" "<<event_words[i][j]-map_of_pedestals[gl_chn].first<<std::endl;
        }
      }
    }
    for(int i=0; i<time_wave.size();i++)
    {
      std::cout <<i+500*evt <<" " <<time_wave.at(i) <<std::endl;
      TxtOutFile << i+500*evt << " " << time_wave.at(i) << "\n";
    }
    time_wave.clear();
    time_wave.resize(501);  
    evt++;
  } 

    for (auto& [global_channel, pedestal] : channels) {
    const double mean = pedestal.sum / static_cast<double>(pedestal.count);
    const double var = pedestal.sum_squared / static_cast<double>(pedestal.count) - std::pow(mean, 2);
    TxtOutPedestal2 << global_channel << " " << mean << " " << std::sqrt(var) << "\n";
    // TxtOutFile << pedestal.sampa << " " << pedestal.channel << " " << global_channel << " " << mean << " " << std::sqrt(var) << "\n";
  }

    time(&end);
    double time_taken = double(end - start);
    std::cout << "Time taken by program is : " << std::fixed
         << time_taken << " sec " << std::endl;

    TxtOutFile.close();
    TxtOutPedestal2.close();

}
