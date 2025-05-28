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



void plot_wave(std::string_view file_name = "~/Downloads/Run71-0000.root")
{
  if (file_name.empty()) {
    return; // just a precaution
  }


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
   while (reader.Next() && event_id<20) //loop over the first 20 events
  {
    
    std::cout <<"New event:"<<event_id <<" "<<std::endl;
    auto& event_words = *words;
    TH1I** hwave = new TH1I*[event_words.size()*100]; 
    std::cout <<event_words.size() <<std::endl;
    for (size_t i = 0; i < event_words.size(); ++i) //loop over the active channels within an event
    {
      sprintf(buf,"h%0zu",i);
      hwave[i] = new TH1I(buf,buf,200,0,200);
      std::cout <<"ID: "<<event_id <<" Channel: <"<<i<<"> ";
      gl_chn = 32*(sampa[i])+channel[i];
      for (size_t j = 2; j < event_words[i].size(); ++j) //loop to get each sample of the waveform array. We start from zero because the first entry is the number of samples in the packet followed by a zero (This is the standard for SAMPA)
      {   
        std::cout << event_words[i][j] << " ";
        hwave[i]->SetBinContent(j,event_words[i][j]);
      }
      std::cout<<std::endl;

      hwave[i]->SetLineColor(i+1);
      hwave[i]->GetYaxis()->SetRangeUser(0,1024);
      hwave[i]->Draw("SAME");
    }
    ++event_id;
  
  }
  cwaves->SaveAs("wave.png");
}