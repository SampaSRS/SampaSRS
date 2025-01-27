#include <RtypesCore.h>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <string_view>
#include <utility>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <filesystem>

#include <sampasrs/mapping.hpp>
#include <sampasrs/clusters.hpp>

#include "TFile.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"

const int max_time_window = 10; //maximum time difference before the maximum to be checked at 20MSps, 1 = 50ns
const int Min_Number_words = 5; //Minimum number of ADC samples to consider a cluster valid  





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
TTreeReaderArray<double> y(reader, "y");

TFile* hfile = new TFile(Clstrootfname.c_str(),"RECREATE");
  
std::cout << "Generating the Clustered file: " << Clstrootfname << std::endl;

TTree *MyTree = new TTree("evt","evt");
double Ex=0;
double Ey=0;
double Et=0;
double xcm=0;
double ycm=0;
double TClstX=0;
double TClstY=0;
int ClstSizeX=0;
int ClstSizeY=0;
int ClstID=0;
unsigned int trgID=0;

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

int Num_words=0;
int gl_chn=0;
int max_word=0;
int E_max=0;
int E_int=0;
int T_max=0;
int j = 0;
int T_0 = 0;

std::vector <int> CSizeX ={};
std::vector <int> CSizeY ={};
std::vector <double> ClstPosX ={};
std::vector <double> ClstEnergyX ={};
std::vector <double> ClstPosY ={};
std::vector <double> ClstEnergyY ={};
std::vector <double> ClstTimeX ={};
std::vector <double> ClstTimeY ={};

std::vector <int> time_hit;
std::vector <int> word_hit;

double MaxtimeseparationXY = 1.0;


bool bad_event=false;
int num_bad_evt = 0;

std::vector <Hits_evt> hitsx; 
std::vector <Hits_evt> hitsy; 
int event_id = 0;
  while ( reader.Next() ) 
  {

    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) 
    {
      E_int=0;
      E_max=0;
      T_max=0;
      j=0;
      gl_chn = 32*(sampa[i])+channel[i];
      if(map_of_pedestals[gl_chn].first !=0 && map_of_pedestals[gl_chn].second != 1023)
      {
        // std::cout <<"gl_chn: ["<<gl_chn<<"] {"<<map_of_pedestals[gl_chn].first<<"} ";

        while(j < event_words[i].size()) {
          Num_words = event_words[i][j];

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

                    if(Num_words>5)
                    {
                      time_hit.push_back(T_0+k-1);  //The sampa structure is [Number of samples, Initial time, words ....] so K must be reduced by 1 
                      word_hit.push_back(event_words[i][j]-map_of_pedestals[gl_chn].first);
                    }
                  }
                  else
                  {
                    //discard event
                    bad_event=true;
                    break; //bad value encountered
                  }
                }

            }
          if(bad_event) {
            bad_event=false;
            num_bad_evt++;
            break;
          }
          // std::cout <<" ] ---* ";

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
          j++;
        }

      }
    }
    

    
    
    ++event_id;
    if(event_id % 10000==0)
    {
      std::cout << event_id <<" events analyzed -- " <<num_bad_evt<<" bad events." <<std::endl;

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
