#include <iostream>
#include <numeric>
#include <string_view>
#include <sys/types.h>
#include <vector>
#include <unordered_map>
#include <fstream>

#include "TFile.h"
#include "TCanvas.h"
#include "TGraph.h"
#include "TMultiGraph.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"





void view_bx_count(std::string_view file_name = "/home/geovaneg/Documents/SampaSRS/build/RunV2_Board1_003-0000.root")
{
  if (file_name.empty()) {
    return; // just a precaution
  }

  int gl_chn=0;
  int max_word=0;
  int E_max=0;
  int E_total=0;
  double x_pos=0;

  bool evt_ok=false;

  int s0=0;
  int s1=0; 
  int s2=0;
  int s3=0;

  TGraph* gsampa0 = new TGraph();
  TGraph* gsampa1 = new TGraph();
  TGraph* gsampa2 = new TGraph();
  TGraph* gsampa3 = new TGraph();
  


  TFile file(file_name.data(), "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");
  TTreeReaderValue <unsigned int> bx_count(reader,"bx_count");
  TTreeReaderArray<double> x(reader, "x");
 
int event_id = 0;
  while (reader.Next()) 
  {
    auto& event_words = *words;
  
    for (size_t i = 0; i < event_words.size(); ++i) 
    {
      auto& bunch_cross = *bx_count;
      std::cout << sampa[i]-8<<" "<<bunch_cross << std::endl;
      // std::cout <<event_words.size()<<std::endl;
      E_max=0;
      // std::cout << channel[i] <<" "<<sampa[i]<<std::endl;
      gl_chn = 32*(sampa[i]-8)+channel[i];

      if(sampa[i]-8 == 0)
      {
      // gsampa0->SetPoint(s0,channel[i],bunch_cross);
      gsampa0->SetPoint(s0,event_id,bunch_cross);
      s0++;
      }
      
      if(sampa[i]-8 == 1)
      {
      gsampa1->SetPoint(s1,channel[i],bunch_cross);
      gsampa1->SetPoint(s1,event_id,bunch_cross);
      s1++;
      }

      if(sampa[i]-8 == 2)
      {
      gsampa2->SetPoint(s2,channel[i],bunch_cross);
      gsampa2->SetPoint(s2,event_id,bunch_cross);
      s2++;
      }

      if(sampa[i]-8 == 3)
      {
      gsampa3->SetPoint(s3,channel[i],bunch_cross);
      gsampa3->SetPoint(s3,event_id,bunch_cross);
      s3++;  
      }

      for (size_t j = 2; j < event_words[i].size(); ++j) 
      {   
        

      }
        
      }
    ++event_id;
    }

   auto mg = new TMultiGraph();


    TCanvas *c1 = new TCanvas("c1","c1",1800,1800);
    gsampa0->SetMarkerStyle(20);  
    gsampa1->SetMarkerStyle(21);
    gsampa2->SetMarkerStyle(22);
    gsampa3->SetMarkerStyle(34);

    

    
    gsampa1->SetMarkerColor(2);

    gsampa2->SetMarkerColor(3);

    gsampa3->SetMarkerColor(4);


   mg->Add(gsampa0,"p");
   mg->Add(gsampa1,"p");
   mg->Add(gsampa2,"p");
   mg->Add(gsampa3,"p");
      
   mg->Draw("ap");

    
}


