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
    std::cout << "Usage =: ./new_clustering <pedestal_file.txt> <data_file.root>" << std::endl;
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
  

  TFile* hfile = new TFile(Clstrootfname.c_str(),"RECREATE");
    
  std::cout << "Generating the Clustered file: " << Clstrootfname << std::endl;

  TTree *MyTree = new TTree("evt","evt");

  
  double E=0;
  double xcm=0;
  double TClst=0;
  int ClstSize=0;
  int ClstID=0;
  unsigned int trgID=0;

  std::vector <int> time_hit;
  std::vector <int> word_hit;
  bool bad_event=false;
  int num_bad_evt = 0;

  MyTree->Branch("trgID",&trgID,"trgID/i");
  MyTree->Branch("ClstID",&ClstID,"ClstID/I");
  MyTree->Branch("ClstSize",&ClstSize,"ClstSize/I");
  MyTree->Branch("TClst",&TClst,"TClst/D");
  MyTree->Branch("xcm",&xcm,"xcm/D");
  MyTree->Branch("E",&E,"E/D");


  std::unordered_map<int, std::pair<double, double>> map_of_pedestals = {};
  

  Map_pedestal(pedestal_file, map_of_pedestals); // change the mapping on mapping.hpp for a diferent detector
  


  int gl_chn = 0;
  int max_word = 0;
  int E_max = 0;
  int E_int = 0;
  int T_max = 0;
  int T_0 = 0;
  int signal_length = 0;
  double T_rec = 0;

  std::vector <int> CSize ={};
  std::vector <double> ClstPosX ={};
  std::vector <double> ClstEnergy ={};
  std::vector <double> ClstTime ={};
  std::array<double, 1024> n_chns={};
  std::array<double, 1024> sum_cm={};

  int Entries;
  Entries = reader.GetEntries();

  std::vector <Hits_evt> hits; 
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
        gl_chn = 32*(sampa[i]-8)+channel[i];
        if(event_words[i][j] < map_of_pedestals[gl_chn].first+3*map_of_pedestals[gl_chn].second) {
          sum_cm[j] += event_words[i][j]-map_of_pedestals[gl_chn].first;
          n_chns[j] ++;
        }
      }
    }


    for (size_t i = 0; i < event_words.size(); ++i) 
    {

      E_max=0;
      T_max=0;
      E_int=0;

      // std::cout << channel[i] <<" "<<sampa[i]<<std::endl;
      gl_chn = 32*(sampa[i]-8)+channel[i];
      for (size_t j = 2; j < event_words[i].size(); ++j) 
      { 
        if(event_words[i][j] >= 0 && event_words[i][j]<1024)
        {
          if(event_words[i][j] > map_of_pedestals[gl_chn].first+4*map_of_pedestals[gl_chn].second+sum_cm[j]/n_chns[j])
          { 
            time_hit.push_back(j);  //The sampa structure is [Number of samples, Initial time, words ....] so K must be reduced by 1 
            word_hit.push_back(event_words[i][j]-map_of_pedestals[gl_chn].first-sum_cm[j]/n_chns[j]);

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
          hits.push_back({gl_chn, time_hit, word_hit, x[i]});
        }
        time_hit.clear();
        word_hit.clear();  

      }
      
      
      
            
    }

    ++event_id;
    if(event_id % 50000==0)
    {
      std::cout << "Progress: "<<(double)(event_id*100/Entries)<<"% - "<< event_id <<" events analyzed -- " <<num_bad_evt<<" bad events." <<std::endl;

    }

    if(hits.size()>0)
    {
      std::sort(hits.begin(), hits.end(), sort_by_chn);
      Make_1D_Strip_Cluster(hits, CSize, ClstTime, ClstPosX, ClstEnergy);
    }
    hits.clear();

    for(int j = 0; j<ClstPosX.size(); j++)
    {
      ClstID = j;
      trgID = event_id;
      ClstSize = CSize.at(j);
      xcm = ClstPosX.at(j);
      E = ClstEnergy.at(j);
      TClst = ClstTime.at(j);
      // std::cout <<"Cluster: "<< ClstID <<" "<<ClstSize<<" "<<TClst<<" "<<xcm<<" "<<E<<std::endl;
      MyTree->Fill();
    }

    CSize.clear();
    ClstTime.clear();
    CSize.clear();
    ClstEnergy.clear();
    ClstPosX.clear();
    

  }

    //Escrever a nova TTree-----------------------------------------------
    MyTree->Write();
    delete hfile;

    time(&end);
    double time_taken = double(end - start);
    std::cout << "Time taken by program is : " << std::fixed
         << time_taken << " sec " << std::endl;



}
