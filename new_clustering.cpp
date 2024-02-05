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



bool sort_second(tuple<int, double, int, int >& p,
         tuple<int, double, int, int >& q){
            return get<1>(p)>get<1>(q);
         }


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

  std::vector <double> InterpolX ={};
  std::vector <double> InterpolY ={};

  std::vector <std::tuple<int, double, int, int> > hit={}; 

  bool evt_ok=false;
 
int event_id = 0;
  while (reader.Next())  
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
      // std::cout <<event_words.size()<<std::endl;
      // std::cout <<event_words[i].size()<<std::endl;
      E_max=0;
      T_max=0;
      E_int=0;
      evt_ok=false;
      // std::cout << channel[i] <<" "<<sampa[i]<<std::endl;
      gl_chn = 32*(sampa[i]-8)+channel[i];
      // for (size_t j = 2; j < event_words[i].size(); ++j) {
      for (size_t j = 2; j < event_words[i].size(); ++j) 
      { //pico está +- entre 0 e 25   
        if(event_words[i][j] > map_of_pedestals[gl_chn].first+3*map_of_pedestals[gl_chn].second+sum_cm[j]/n_chns[j])
        { //first threshold
          // std::cout << event_words[i][j] <<std::endl;
          E_int += event_words[i][j]-map_of_pedestals[gl_chn].first-sum_cm[j]/n_chns[j];
          if(event_words[i][j]-map_of_pedestals[gl_chn].first>E_max)
          {
            E_max = event_words[i][j]-map_of_pedestals[gl_chn].first-sum_cm[j]/n_chns[j];
            T_max = j;
            if(event_words[i][j] > map_of_pedestals[gl_chn].first+4*map_of_pedestals[gl_chn].second+sum_cm[j]/n_chns[j])
            { 
              evt_ok=true;
            }
          }
        }
      }
      
      
      
      if(E_max>0 && E_max<1024)
      { //checking values and filling vectors
      hit.push_back(make_tuple(T_max, x[i], E_int, evt_ok)); 
      }
            
    }

    // std::cout <<"=============begin====================="<<std::endl;
    //  for(int y=0; y<hit.size() ; y++ )
    //  {
    //   std::cout <<get<0>(hit[y]) << " "<< get<1>(hit[y]) <<" "<< get<2>(hit[y]) <<" "<<get<3>(hit[y]) <<std::endl;
    //  }
    // std::cout <<"=============sorted====================="<<std::endl;
     
    std::sort(hit.begin(),hit.end(), sort_second);
    // std::sort(hit.begin(), hit.end());
    

    //  for(int y=0; y<hit.size() ; y++ )
    //  {
    //   std::cout <<get<0>(hit[y]) << " "<< get<1>(hit[y]) <<" "<< get<2>(hit[y]) <<" "<<get<3>(hit[y]) <<std::endl;
    //  }
    // std::cout <<"=============end====================="<<std::endl;

  
    if(!hit.empty())
    {
      Make_Cluster(hit, CSize, ClstTime, ClstPosX, ClstEnergy);
    }
//  std::cout <<"=============end-clustering====================="<<std::endl;
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

    ClstTime.clear();
    CSize.clear();
    ClstEnergy.clear();
    ClstPosX.clear();
    hit.clear();
    
    evt_ok=false;
    

    ++event_id;
    if(event_id % 10000==0)
    {
      std::cout << event_id <<" events analyzed" <<std::endl;

    }
  }

    //Escrever a nova TTree-----------------------------------------------
    MyTree->Write();
    delete hfile;

    time(&end);
    double time_taken = double(end - start);
    std::cout << "Time taken by program is : " << std::fixed
         << time_taken << " sec " << std::endl;



}
