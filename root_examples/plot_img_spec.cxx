#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>

#include "TFile.h"
#include "TCanvas.h"
#include "TH1I.h"
#include "TH1D.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"


void Map_pedestal(std::unordered_map<int, std::pair<double, double>> &my_map)
{
    // Create an unordered_map of three values glchn(sampa*32+Chn)    
    std::ifstream mapfile;
    int glchn=0;
    double baseline=0;
    double sigma=0;
    mapfile.open("/home/geovaneg/Documents/Aquisicao-SampaSRS/Run6x_pedestal_antestubo-0000.PedFile.txt");
    while (true) 
    { 
      mapfile >> glchn;
      mapfile >> baseline;
      mapfile >> sigma;
      my_map[glchn] = {baseline,sigma};
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

  std::unordered_map<int, std::pair<double, double>> map_of_pedestals = {};

  Map_pedestal(map_of_pedestals); // change the mapping on mapping.hpp for a diferent detector

  TCanvas* c1 = new TCanvas("c1","c1",0,0,1600,800);
  c1->Divide(2,1);
  c1->cd(1);

  int gl_chn=0;
  int max_word=0;
  int E_max=0;
  int E_total=0;
  double x_pos=0;

  bool evt_ok=false;

  TH1D * h_pos = new TH1D("h_pos","h_pos", 256, 0, 50);
  TH1D * h_energy = new TH1D("h_energy","h_energy", 1000, 0, 8000);


  TFile file(file_name.data(), "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");
  TTreeReaderArray<double> x(reader, "x");
 
int event_id = 0;
  while (reader.Next()) {
    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) {
      // std::cout <<event_words.size()<<std::endl;
      E_max=0;
      // std::cout << channel[i] <<" "<<sampa[i]<<std::endl;
      gl_chn = 32*(sampa[i]-8)+channel[i];
      // for (size_t j = 2; j < event_words[i].size(); ++j) {
      for (size_t j = 2; j < 25; ++j) { //pico está +- entre 0 e 25   
        if(event_words[i][j] > map_of_pedestals[gl_chn].first+5*map_of_pedestals[gl_chn].second){
          if(event_words[i][j]-map_of_pedestals[gl_chn].first>E_max){
            E_max = event_words[i][j]-map_of_pedestals[gl_chn].first;
            evt_ok=true;
          }

        }
        
      }
      if(E_max>1 && E_max<1024){
      x_pos = x_pos+(x[i]*E_max);
      E_total = E_total+E_max;
      }
    }
    // if(evt_ok==true && E_total>500)
    if(evt_ok==true && 45>x_pos/E_total && x_pos/E_total>40)
    {
      h_pos->Fill(x_pos/E_total);
      h_energy->Fill(E_total);
    }
    evt_ok=false;
    
    x_pos=0;
    E_total=0;
   
    ++event_id;
    if(event_id % 10000==0)
    {
      std::cout << event_id <<" events analyzed" <<std::endl;

    }
    
  }
  h_pos->Draw();
  c1->cd(2);
  h_energy->Draw();  
}
