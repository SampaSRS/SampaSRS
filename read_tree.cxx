#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>

#include "TFile.h"
#include "TCanvas.h"
#include "TGraph2D.h"
#include "TH2D.h"
#include <TROOT.h>
#include <TStyle.h>

#include "TTreeReader.h"
#include "TTreeReaderArray.h"


void read_tree(std::string_view file_name = "/home/geovaneg/Documents/aquisicoes/Run6x_pedestal_antestubo-0000.root")
{
  if (file_name.empty()) {
    return; // just a precaution
  }


  //  gStyle->SetOptStat(0);
  gStyle->SetTitleFont(132, "xyz");
  gStyle->SetTitleFont(132, " ");
  gStyle->SetLabelFont(132, "xyz");
  gStyle->SetPalette(55);

  TFile file(file_name.data(), "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");

  std::vector<double> baseline(128);
  std::vector<double> sigma(128);

  int gl_chn=0;
  int count=0;
  int point=0;


  TGraph2D *gr1 = new TGraph2D();

int event_id = 0;
  while (reader.Next()) 
  {
    for(int k=0; k< 128; k++) 
    { 
    baseline[k]=0;
    }

    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) 
    {
      gl_chn = (sampa[i]-8)*32+channel[i];
      for (size_t j = 0; j < event_words[i].size(); ++j) 
      {
        // sum += event_words[i][j];
        // std::cout << event_words[i][j]<<std::endl;
        baseline[gl_chn]+= event_words[i][j];
        count++;
      }
      gr1->SetPoint(point, event_id, gl_chn, baseline[gl_chn]/count);
      point++;
      std::cout << gl_chn <<" "<<count<<" "<<baseline[gl_chn]<<" "<<baseline[gl_chn]/count<<std::endl;
      count=0;
    }
    event_id++;
  }

TCanvas *c1 = new TCanvas("c1","c1",800,800);
gr1->SetTitle("Baseline overtime; Trigger_ID; gl_chn; Baseline Mean");
gr1->GetYaxis()->SetRangeUser(0,128);
gr1->Draw("P");

}
