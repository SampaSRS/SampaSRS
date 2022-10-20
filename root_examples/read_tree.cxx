#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>

#include "TFile.h"
#include "TCanvas.h"
#include "TH1I.h"
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

  // for(int i =0; i<128; i++)
  // {
  //   std::cout << map_of_pedestals[i] <<std::endl;
  // }

  TCanvas* cwaves = new TCanvas("cwaves","cwaves",0,0,800,600);

  int gl_chn=0;
  int max_word=0;

  char* buf = new char[20];

  TFile file(file_name.data(), "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");

int event_id = 0;
  while (reader.Next() && event_id<26) {
    auto& event_words = *words;
    long sum = 0;
      TH1I** hwave = new TH1I*[event_words.size()*10];
    for (size_t i = 0; i < event_words.size(); ++i) {

    sprintf(buf,"h%00d",i);
    hwave[i] = new TH1I(buf,buf,50,0,50);
      std::cout << channel[i] <<" "<<sampa[i]<<std::endl;
      gl_chn = 32*(sampa[i]-8)+channel[i];
      std::cout <<"here0"<<std::endl;
      for (size_t j = 2; j < event_words[i].size(); ++j) {
          std::cout <<"here00"<<std::endl;
          hwave[i]->SetBinContent(j,event_words[i][j]);
          std::cout <<"here000"<<std::endl;
          
          std::cout <<"here"<<std::endl;
        // if(event_words[i][j] > map_of_pedestals[gl_chn]){
           std::cout << j << " " << event_words[i][j]-map_of_pedestals[gl_chn] << std::endl;       
        // }
        
      }
      std::cout <<"here2"<<std::endl;
    hwave[i]->SetLineColor(i+1);
    hwave[i]->GetYaxis()->SetRangeUser(0,1024);
    hwave[i]->Draw("SAME");
    }
    std::cout <<"here3"<<std::endl;
    // std::cout << event_id << " " << sum << "\n";
    ++event_id;
  }
  cwaves->SaveAs("wave.png");
}
