#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <filesystem>
#include <bits/stdc++.h> 

#include <sampasrs/mapping.hpp>
#include <sampasrs/cluster.hpp>


#include "TFile.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"


using namespace std;



int main(int argc, char *argv[])
{
  time_t start = 0;
  time_t end =0;
  time(&start);

  if(argc != 3)
  {
    std::cout << "Usage =: ./clustering <pedestal_file.txt> <data_file.root>" << std::endl;
    return 0;
  }

  std::string pedestal_file = argv[1];
  std::string file_name = argv[2];
   
 if (file_name.empty() || pedestal_file.empty()) {
    std::cout <<"Empty files?" << std::endl;
    return 0; // just a precaution
  }


  auto input_path = std::filesystem::path(file_name);
  auto Clstrootfname = input_path.replace_extension("Clst.root").string();

  TFile file(file_name.data(), "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");
  TTreeReaderArray<double> x(reader, "x");
  TTreeReaderArray<double> y(reader, "y");
  

  TFile* hfile = new TFile(Clstrootfname.c_str(),"RECREATE");
    
  std::cout << "Generating the Clustered file: " << Clstrootfname << std::endl;

  TTree *MyTree = new TTree("evt","evt");

  
  double Ex=0;
  double xcm=0;
  double Ey=0;
  double ycm=0;
  double Et=0;
  double TClstX=0;
  double TClstY=0;
  int ClstSizeX=0;
  int ClstSizeY=0;
  int ClstID=0;
  unsigned int trgID=0;

  std::vector <int> time_hit;
  std::vector <int> word_hit;
  bool bad_event=false;
  int num_bad_evt = 0;

  MyTree->Branch("trgID",&trgID,"trgID/i");
  MyTree->Branch("ClstID",&ClstID,"ClstID/I");
  MyTree->Branch("ClstSizeX",&ClstSizeX,"ClstSizeX/I");
  MyTree->Branch("ClstSizeY",&ClstSizeY,"ClstSizeY/I");
  MyTree->Branch("TClstX",&TClstX,"TClstX/D");
  MyTree->Branch("TClstY",&TClstY,"TClstY/D");
  MyTree->Branch("Ex",&Ex,"Ex/D");
  MyTree->Branch("Ey",&Ey,"Ey/D");
  MyTree->Branch("xcm",&xcm,"xcm/D");
  MyTree->Branch("ycm",&ycm,"ycm/D");
  MyTree->Branch("Et",&Et,"Et/D");


  std::unordered_map<int, std::pair<double, double>> map_of_pedestals = {};
  

  Map_pedestal(pedestal_file, map_of_pedestals); // change the mapping on mapping.hpp for a diferent detector
  
  double MaxtimeseparationXY = 1.0;

  int gl_chn = 0; 

  std::vector <int> CSizeX ={};
  std::vector <int> CSizeY ={};
  std::vector <double> ClstPosX ={};
  std::vector <double> ClstEnergyX ={};
  std::vector <double> ClstPosY ={};
  std::vector <double> ClstEnergyY ={};
  std::vector <double> ClstTimeX ={};
  std::vector <double> ClstTimeY ={};
  std::array<double, 1024> n_chns={};
  std::array<double, 1024> sum_cm={};

  int Entries;
  Entries = reader.GetEntries();

  std::vector <Hits_evt> hitsx; 
  std::vector <Hits_evt> hitsy; 
  bool evt_ok=false;
 
  int event_id = 0;

  while ( reader.Next() )  
  {
    std::fill( std::begin( sum_cm ), std::end( sum_cm ), 0 );
    std::fill( std::begin( n_chns ), std::end( n_chns ), 0 );
    
    auto& event_words = *words;
    
    //calculation of the common mode for later correction

    for (size_t i = 0; i < event_words.size(); ++i) {
      for (size_t j = 2; j < event_words[i].size(); ++j) {
        gl_chn = 32*(sampa[i])+channel[i];
        if(event_words[i][j] < map_of_pedestals[gl_chn].first+3*map_of_pedestals[gl_chn].second) {
          sum_cm[j] += event_words[i][j]-map_of_pedestals[gl_chn].first;
          n_chns[j] ++;
        }
      }
    }


    for (size_t i = 0; i < event_words.size(); ++i) 
    {
      // std::cout << channel[i] <<" "<<sampa[i]<<std::endl;
      gl_chn = 32*(sampa[i])+channel[i];
      for (size_t j = 2; j < event_words[i].size(); ++j) 
      { 
        if(event_words[i][j] >= 0 && event_words[i][j]<1024)
        {
          if(event_words[i][j] > map_of_pedestals[gl_chn].first+2*map_of_pedestals[gl_chn].second+sum_cm[j]/n_chns[j])
          { 
            time_hit.push_back(j);  //The sampa structure is [Number of samples, Initial time, words ....] so K must be reduced by 1 
            word_hit.push_back(event_words[i][j]-map_of_pedestals[gl_chn].first-sum_cm[j]/n_chns[j]);
            // std::cout <<gl_chn<<" "<<j<<" "<<event_words[i][j]-map_of_pedestals[gl_chn].first-sum_cm[j]/n_chns[j]<<" "<< x[i]<<" "<<y[i]<<std::endl;
          }
        }
        else
        {
          //discard event
          bad_event=true;
          break; //bad value encountered
        }

        if(bad_event) 
          {
            bad_event=false;
            num_bad_evt++;
            break;
          }

        if(time_hit.size() >0)
        {
          if(x[i]>=0)
            {
              hitsx.push_back({gl_chn, time_hit, word_hit, x[i]});
            }
          if(y[i]>=0)
            {
              hitsy.push_back({gl_chn, time_hit, word_hit, y[i]});
            }

        }
        time_hit.clear();
        word_hit.clear();  
        
      }
      
      
      
            
    }

    ++event_id;
    if(event_id % 500==0)
    {
      std::cout << "Progress: "<<(double)(event_id*100/Entries)<<"% - "<< event_id <<" events analyzed -- " <<num_bad_evt<<" bad events." <<std::endl;

    }

    if(hitsx.size()>0)
    {
      std::sort(hitsx.begin(), hitsx.end(), sort_by_chn);
      Make_1D_Strip_Cluster(hitsx, CSizeX, ClstTimeX, ClstPosX, ClstEnergyX);
    }

    if(hitsy.size()>0)
    {
      std::sort(hitsy.begin(), hitsy.end(), sort_by_chn);
      Make_1D_Strip_Cluster(hitsy, CSizeY, ClstTimeY, ClstPosY, ClstEnergyY);
    }


    hitsx.clear();
    hitsy.clear();


    std::vector<MergedEntry> merged = mergeEntries(CSizeX, ClstTimeX, ClstPosX, ClstEnergyX, CSizeY, ClstTimeY, ClstPosY, ClstEnergyY, MaxtimeseparationXY);

    int k=0;
 
    for (const auto& entry : merged)
    {
      ClstID = k;
      trgID = event_id;
      ClstSizeX = entry.CSizex;
      ClstSizeY = entry.CSizey;
      TClstX = entry.TClstX;
      TClstY = entry.TClstY;
      Ex = entry.Ex;
      Ey = entry.Ey;
      xcm = entry.xcm;
      ycm = entry.ycm;
      Et = entry.Et;
      k++;
      MyTree->Fill();
    }

    CSizeX.clear();
    CSizeY.clear();
    ClstTimeX.clear();
    ClstTimeY.clear();
    ClstEnergyX.clear();
    ClstEnergyY.clear();
    ClstPosX.clear();
    ClstPosY.clear();
    merged.clear();

  }

    //Escrever a nova TTree-----------------------------------------------
    MyTree->Write();
    delete hfile;

    time(&end);
    double time_taken = double(end - start);
    std::cout << "Time taken by program is : " << std::fixed
         << time_taken << " sec " << std::endl;



}
