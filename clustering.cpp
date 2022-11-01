#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>

#include "TFile.h"
#include "TCanvas.h"
#include "TH1I.h"
#include "TH1D.h"
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
      my_map[glchn] = {baseline,sigma};
      // std::cout << glchn << " " << baseline <<" "<<sigma<<std::endl;
      if( mapfile.eof() ) 
      {
        break;
      }
    }
    mapfile.close();
}

void Make_Cluster(std::vector<std::pair<double, std::pair<int,int>>>hit,std::vector <double> &ClustPosX,std::vector <double> &ClustEnergy)
{  
  //pitch strips == 0.390625 
  double pitch = 0.390625;
  double x_pos=0;
  double E_total=0;
  bool NewFlag=false;
  int ClstID=0;
  bool evt_ok=false;

  for(int i=0; i<hit.size() ; i++ )
  {

    if(hit.at(i).second.second == 1)
    {
      NewFlag=true;
      if(i>0)
      {
        x_pos += hit.at(i-1).first*hit.at(i).second.first;
        E_total += hit.at(i-1).second.first;
      }
    }
  

    std::cout << hit.at(i).first<<" "<<hit.at(i).second.first<<" "<<hit.at(i).second.second<<std::endl;
    // x_pos += hit.at(i).first*hit.at(i).second.first;
    // E_total += hit.at(i).second.first;
    if(i>0 && NewFlag == hit.at(i).second.second && abs(hit.at(i).first - hit.at(i-1).first<pitch+0.1))
    {
      x_pos += hit.at(i).first*hit.at(i).second.first;
      E_total += hit.at(i).second.first;
      evt_ok=true;
    }
    else if(evt_ok)
    {
      std::cout <<"Um Cluster ID: "<< ClstID <<" "<<x_pos/E_total<< " " <<E_total<<std::endl;
      x_pos=0;
      E_total=0;
      NewFlag=false;
      evt_ok=false;
      ClstID++;
    }

    // if( i+1<hit.size() && abs(hit.at(i).first - hit.at(i+1).first)<pitch+0.1 )
    // {
    //   std::cout <<"End"<<std::endl;
    // }
      // std::cout << hit.at(i).first <<" "<<hit.at(i).second.first<<" "<< hit.at(i).second.second<< std::endl;

  }
}  




int main(int argc, char *argv[])
{
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


  std::unordered_map<int, std::pair<double, double>> map_of_pedestals = {};

  Map_pedestal(pedestal_file, map_of_pedestals); // change the mapping on mapping.hpp for a diferent detector

  int gl_chn=0;
  int max_word=0;
  int E_max=0;
  int E_total=0;
  int E_int=0;
  double x_pos=0;
  std::vector <double> ClustPosX ={};
  std::vector <double> ClustEnergy ={};

  

  std::vector <std::pair <double, std::pair<int, int>>> hit ={};

  bool evt_ok=false;


  TFile file(file_name.data(), "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");
  TTreeReaderArray<double> x(reader, "x");
 
int event_id = 0;
  while (reader.Next() && event_id<=1e4) {
    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) {
      // std::cout <<event_words.size()<<std::endl;
      E_max=0;
      // std::cout << channel[i] <<" "<<sampa[i]<<std::endl;
      gl_chn = 32*(sampa[i]-8)+channel[i];
      // for (size_t j = 2; j < event_words[i].size(); ++j) {
      for (size_t j = 2; j < 25; ++j) { //pico está +- entre 0 e 25   
        if(event_words[i][j] > map_of_pedestals[gl_chn].first+2*map_of_pedestals[gl_chn].second){ //first threshold
          // E_int += event_words[i][j]-map_of_pedestals[gl_chn].first;
          if(event_words[i][j]-map_of_pedestals[gl_chn].first>E_max){
            E_max = event_words[i][j]-map_of_pedestals[gl_chn].first;
            if(event_words[i][j] > map_of_pedestals[gl_chn].first+5*map_of_pedestals[gl_chn].second){ //validation threshold
              evt_ok=true;
            }
          }

        }
        
      }
      if(E_max>1 && E_max<1024){ //checking values and filling vectors
      // std::cout<<event_id<<" "<<evt_ok<<" " << x[i] <<" "<<E_max<<std::endl;
      hit.push_back(std::make_pair(x[i], std::make_pair(E_max, evt_ok)));
      }
      evt_ok=false;
    }

    std::sort(hit.begin(), hit.end());    
    // if(evt_ok==true)
    // {

    std::cout << event_id <<std::endl;
    Make_Cluster(hit, ClustPosX, ClustEnergy);

      // for(int k=0; k<hit.size();k++)
      // {
      // std::cout<<event_id<<" " << hit[k].first<<" "<<hit[k].second.first<<" "<<hit[k].second.second<<std::endl;
      // }
      //fazer o clustering
      // h_pos->Fill(x_pos/E_total);
      // h_energy->Fill(E_total);
    // }
    hit.clear();
    evt_ok=false;
    
    x_pos = 0;
    E_total = 0;
    E_int = 0;
    ++event_id;
    if(event_id % 10000==0)
    {
      std::cout << event_id <<" events analyzed" <<std::endl;

    }
    
  } 
}
