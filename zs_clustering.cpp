#include <RtypesCore.h>
#include <cstddef>
#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <filesystem>


#include "TFile.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"


void Map_pedestal(std::string const& pedestal_file, std::unordered_map<int, std::pair<double, double>> &my_map)
{
    // Create an unordered_map of three values glchn(sampa*32+Chn)    
    std::ifstream mapfile;
    int glchn=0;
    double baseline=0;
    double sigma=0;
    mapfile.open(pedestal_file);
    while (true) 
    { 
      mapfile >> glchn;
      mapfile >> baseline;
      mapfile >> sigma;
      if(sigma==0)
      {
        my_map[glchn] = {baseline,1023}; //canais vem com baseline com todos valores igual sigma==0/ matar esse canal
      }
      else
      {
        my_map[glchn] = {baseline,sigma};
      }
      // std::cout << glchn << " " << my_map[glchn].first <<" "<<my_map[glchn].second<<std::endl;
      

      if( mapfile.eof() ) 
      {
        break;
      }
    }
    mapfile.close();
  
    // for(int k=0;k<my_map.size();k++) {std::cout << my_map[k].first<<" "<<my_map[k].second <<std::endl;}
}


// void Make_Cluster(std::vector<std::pair<double, std::pair<int,int>>>hit,std::vector <int> &ClustSize,
// std::vector <double> &ClustPosX,std::vector <double> &ClustEnergy)

// { 
//   int max_time_duration = 10; //10 time bins ->correspodent to 1us at 10 MSPs or 0.5us at 20MSPs 
//   int max_time_separation = 50; //50 time bins ->correspodent to 5us at 10 MSPs or 2.5us at 20MSPs 
// }

int main(int argc, char *argv[])
{
  time_t start = 0;
  time_t end =0;
  time(&start);

if(argc != 2)
{
  std::cout << "Usage =: ./zs_clustering.cpp <data_file.root>" << std::endl;
  return 0;
}

std::string file_name = argv[1];
   
if (file_name.empty()) {
  std::cout <<"Empty files?" << std::endl;
  return 0; // just a precaution
}
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


std::vector <std::pair <double, std::pair<int, int>>> hit ={};

bool evt_ok=false;
bool bad_event=false;
int num_bad_evt = 0;
 
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
      std::cout <<"gl_chn: ["<<gl_chn<<"] ";

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
                E_int+=event_words[i][j];
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
        E_int=0;
        j++;
      }

      // std::cout<<event_id<<" "<<evt_ok<<" " << x[i] <<" "<<E_max<<std::endl;
      hit.push_back(std::make_pair(x[i], std::make_pair(E_max, evt_ok)));

      evt_ok=false;
    }
    std::sort(hit.begin(), hit.end());    
    // if(evt_ok==true)
    // {

    // std::cout << event_id <<std::endl;
    // if(!hit.empty())
    // {
    //   Make_Cluster(hit, CSize, ClstPosX, ClstEnergy);
    // }

    // for(int j = 0; j<ClstPosX.size(); j++)
    // {
    //   ClstID = j;
    //   trgID = event_id;
    //   ClstSize = CSize.at(j);
    //   xcm = ClstPosX.at(j);
    //   E = ClstEnergy.at(j);
    //   // std::cout << ClstID <<" "<<ClstSize<<" "<<xcm<<" "<<E<<std::endl;
    //   MyTree->Fill();
    // }

    
    // CSize.clear();
    // ClstEnergy.clear();
    // ClstPosX.clear();
    // hit.clear();
    
    evt_ok=false;
    
    E_int = 0;
    ++event_id;
    // if(event_id % 10000==0)
    // {
      std::cout << event_id <<" events analyzed -- " <<num_bad_evt<<" bad events." <<std::endl;

    // }
    std::cout<<std::endl;
  } 

    //Escrever a nova TTree-----------------------------------------------
    MyTree->Write();
    delete hfile;

    time(&end);
    double time_taken = double(end - start);
    std::cout << "Time taken by program is : " << std::fixed
         << time_taken << " sec " << std::endl;



}
