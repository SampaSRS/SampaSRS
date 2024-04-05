#include <RtypesCore.h>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <filesystem>

#include <sampasrs/mapping.hpp>
#include <sampasrs/cluster.hpp>

#include "TFile.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"

struct Hits_evt {
  int gl_chn;
  std::vector <int> time;
  std::vector <int> word;
  double x_pos;
  int E_tot;
};


void Make_Cluster(std::vector <Hits_evt> hits, std::vector <int> &ClustSize,
std::vector <double> &ClustPosX, std::vector <double> &ClustEnergy)

{  
  double pitch = 0.390625; //pitch real = 0.390625
  double x_pos=0;
  double E_total=0;
  int ClstID=0;
  int ClstSize=1;


  for (int i = 0; i< hits.size(); i++)
  {
    std::cout <<  "----begin---" << std::endl;
    std::cout << hits[i].gl_chn<<" "<<hits[i].x_pos <<  std::endl;

    for(int j = 0; j<hits[i].time.size(); j++)
    {
      std::cout<< hits[i].time[j] <<" ";
    }
    std::cout << std::endl;
      for (int j =0; j< hits[i].word.size(); j++)
    {
      std::cout<< hits[i].word[j] <<" ";
    }
    std::cout << std::endl;
    std::cout <<"----end---"<<std::endl;
    
  }
}





int main(int argc, char *argv[])
{
  time_t start = 0;
  time_t end =0;
  time(&start);

if(argc != 3)
{
  std::cout << "Usage =: ./zs_clustering <file_pedestal.txt> <data_file.root>" << std::endl;
  return 0;
}

std::string pedestal_file = argv[1];
std::string file_name = argv[2];
   
if (file_name.empty() || pedestal_file.empty()) {
  std::cout <<"Empty files?" << std::endl;
  return 0; // just a precaution
}

std::unordered_map<int, std::pair<double, double>> map_of_pedestals = {};
Map_pedestal(pedestal_file, map_of_pedestals); // change the mapping on mapping.hpp for a diferent detector

auto input_path = std::filesystem::path(file_name);
auto Clstrootfname = input_path.replace_extension("ZS_Clst.root").string();
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
int ClstSize=0;
int ClstID=0;
unsigned int trgID=0;
MyTree->Branch("trgID",&trgID,"trgID/i");
MyTree->Branch("ClstID",&ClstID,"ClstID/I");
MyTree->Branch("ClstSize",&ClstSize,"ClstSize/I");
MyTree->Branch("xcm",&xcm,"xcm/D");
MyTree->Branch("E",&E,"E/D");
int Num_words=0;
int gl_chn=0;
int max_word=0;
int E_max=0;
int E_int=0;
int T_max=0;
int j = 0;
int T_0 = 0;
std::vector <int> CSize ={};
std::vector <double> ClstPosX ={};
std::vector <double> ClstEnergy ={};


std::vector <int> time_hit;
std::vector <int> word_hit;

bool evt_ok=false;
bool bad_event=false;
int num_bad_evt = 0;

std::vector <Hits_evt> hits; 
int event_id = 0;
  while (reader.Next()) 
  {
    std::cout << "_________new evt__________" << std::endl;
    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) 
    {
      E_int=0;
      E_max=0;
      T_max=0;
      j=0;
      gl_chn = 32*(sampa[i]-8)+channel[i];
      if(map_of_pedestals[gl_chn].first !=0 && map_of_pedestals[gl_chn].second != 1023)
      {
        std::cout <<"gl_chn: ["<<gl_chn<<"] {"<<map_of_pedestals[gl_chn].first<<"} ";

        while(j < event_words[i].size()) {
          Num_words = event_words[i][j];
          std::cout << Num_words <<" [ ";
            for(size_t k = 0; k<= Num_words; k++) {
              j++;
              if(k == 0)
                {
                  T_0 = event_words[i][j];
                }
              else
                {
                  if(event_words[i][j] > 0 && event_words[i][j]<1024)
                  {
                    if(event_words[i][j]-map_of_pedestals[gl_chn].first>2)
                    {
                      time_hit.push_back(T_0+k-1);  //The sampa structure is [Number of samples, Initial time, words ....] so K must be reduced by 1 
                      word_hit.push_back(event_words[i][j]-map_of_pedestals[gl_chn].first);
                      E_int+=event_words[i][j]-map_of_pedestals[gl_chn].first;
                    }
                  }
                  else
                  {
                    //discard event
                    bad_event=true;
                    break; //bad value encountered
                  }
                }
              std::cout<<event_words[i][j]<<" "; 
            }
          if(bad_event) {
            bad_event=false;
            num_bad_evt++;
            break;
          }
          std::cout <<" ] ---* ";
          std::cout <<T_0<<" "<< E_int <<"*----"<<std::endl;
          if(time_hit.size() >0 && word_hit.size() >0)
          {
            hits.push_back({gl_chn, time_hit, word_hit, x[i], E_int});
          }
          time_hit.clear();
          word_hit.clear();
          E_int=0;
          j++;
        }
        evt_ok=false;
      }
    }
    
    evt_ok=false;
    
    E_int = 0;
    ++event_id;
    // if(event_id % 10000==0)
    // {
      std::cout << event_id <<" events analyzed -- " <<num_bad_evt<<" bad events." <<std::endl;

    // }
    std::cout<<std::endl;
    if(hits.size()>0)
    {
      Make_Cluster(hits, CSize, ClstPosX, ClstEnergy);
    }
    hits.clear();
  } 

    //Escrever a nova TTree-----------------------------------------------
    MyTree->Write();
    delete hfile;

    time(&end);
    double time_taken = double(end - start);
    std::cout << "Time taken by program is : " << std::fixed
         << time_taken << " sec " << std::endl;



}
