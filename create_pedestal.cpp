
#include <cmath>
#include <sampasrs/root_fix.hpp>

#include <sampasrs/acquisition.hpp>
#include <tins/tins.h>

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
  using namespace Tins;
  using namespace sampasrs;

  if (argc < 2) {
    std::cerr << "Usage: create_pedestal <Pedestal_file.raw>\n";
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


  std::vector<double>sum(128);
  std::vector<double>count(128);
  std::vector<double>mean(128);
  std::vector<double>Var(128);
  std::vector<double>Chn(128);
  std::vector<double>Sampa(128);
  int gl_chn=0;

  int const NumEvts = 10; //Number of events for pedestal file


  int event_id = 0;
  while (reader.Next() || event_id<NumEvts) 
  {
    auto& event_words = *words;
    for (size_t i = 2; i < event_words.size(); ++i) 
    {
      gl_chn = (sampa[i]-8)*32+channel[i];
      // std::cout<<gl_chn<<std::endl;
      for (size_t j = 0; j < event_words[i].size(); ++j) 
      {
        sum[gl_chn] += event_words[i][j];
        count[gl_chn]++;
        Chn[gl_chn]=channel[i];
        Sampa[gl_chn]=sampa[i];
      }
    }
    event_id++;
  }

  for(int k = 0; k<128; k++)
  {
    if(count[k] == 0)
    {
      mean[k]=-1;
    }
    else
    {
      mean[k] = sum[k]/count[k];
    }
  }



  event_id=0;
  reader.SetEntry(0);
  while (reader.Next() || event_id<NumEvts) 
  {
    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) 
    {
      gl_chn = (sampa[i]-8)*32+channel[i];
      // std::cout<<gl_chn<<std::endl;
      for (size_t j = 2; j < event_words[i].size(); ++j)  //partir do 2 para eliminar o 0 e numero de palavras do inicio
      {
        Var[gl_chn] += pow((event_words[i][j] - mean[gl_chn]),2);
        // std::cout <<event_words[i][j] <<" "<< mean[gl_chn]<<std::endl;
      }
    }
    event_id++;
  }

  for(int k = 0; k<128; k++)
  {
    if(count[k] == 0)
    {
    std::cout <<Sampa[k]<<" "<<Chn[k]<<" "<< -1 <<" "<<-1<<std::endl; //colocar qual valor? negativo não manda nada  
    }
    else
    {
    mean[k] = sum[k]/count[k];
    std::cout <<Sampa[k]<<" "<<Chn[k]<<" "<<mean[k]<<" "<<std::sqrt(Var[k]/count[k])<<std::endl;  
    }
  }


  TxtOutFile.close();
  return 0;
}
