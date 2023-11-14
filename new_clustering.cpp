#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <filesystem>
#include <bits/stdc++.h> 

#include "TFile.h"
#include "TTreeReader.h"
#include "TTreeReaderArray.h"

const int window_duration = 30; //window duration to seek for a peak in triggered mode 3us (each timebin is approx 100ns)
const int max_time_separation = 30; //maximum time separation between signals to split a cluster in time 3us
const int max_signal_duration=6;

using namespace std;

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
        my_map[glchn] = {baseline,1023}; //supress the channels with sigma == 0 / are frozen
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
  

}

bool sort_second(tuple<int, double, int, int >& p,
         tuple<int, double, int, int >& q){
            return get<1>(p)>get<1>(q);
         }

void Make_Cluster(std::vector<std::tuple<int, double, int, int >> hit,std::vector <int> &ClustSize,std::vector <double> &ClustTime, 
std::vector <double> &ClustPosX,std::vector <double> &ClustEnergy)

{  
  double pitch = 0.390625; //pitch real = 0.390625
  double x_pos=0;
  double E_total=0;
  double ClstTime = 0;
  int ClstID=0;
  int ClstSize=1;

  int CurrentTime = 0;
  double CurrentPos = 0;
  double CurrentEnergy = 0;
  bool ValidCluster;

  double max_timediff_Emax = 2;

  int LastTime = get<0>(hit[0]); 
  double LastPosition=get<1>(hit[0]);
  double LastEnergy=get<2>(hit[0]);
  bool NewCluster=get<3>(hit[0]);
  

  x_pos+=LastPosition*LastEnergy;
  E_total+= LastEnergy;
  ClstTime+=LastEnergy*LastTime;

  for(int i=1; i<hit.size() ; i++ )
  {
    CurrentTime = get<0>(hit[i]); 
    CurrentPos = get<1>(hit[i]); 
    CurrentEnergy = get<2>(hit[i]);
    ValidCluster = get<3>(hit[i]);

      //If the time difference between nighboors is greater than max_timediff_Emax
        if(abs(CurrentTime-LastTime)>max_timediff_Emax)
        {
          //If the cluster is valid close the cluster.
          if(NewCluster)
          {
            ClustSize.push_back(ClstSize);
            ClustTime.push_back(ClstTime/E_total);
            ClustPosX.push_back(x_pos/E_total);
            ClustEnergy.push_back(E_total);
            // std::cout << "ClustID: "<<ClstID<< " ClustSize: "<< ClstSize << " " << " ClustTime: "<< ClstTime/E_total << " "<<x_pos/E_total << " " << E_total << std::endl; 
          }
          LastPosition=CurrentPos;
          LastTime = CurrentTime;
          LastEnergy=CurrentEnergy;
          ClstSize=1;
          x_pos=0;
          ClstTime=0;
          E_total=0;
          NewCluster=false;
          x_pos+=CurrentEnergy*CurrentPos;
          ClstTime+=CurrentEnergy*CurrentTime;
          E_total+=CurrentEnergy;
          if(ValidCluster)
          {
            NewCluster=true;
          }
          ClstID++;

        }
        else
        {
        if(abs(CurrentPos-LastPosition)>pitch)
        {
          if(NewCluster)
          {
            ClustSize.push_back(ClstSize);
            ClustTime.push_back(ClstTime/E_total);
            ClustPosX.push_back(x_pos/E_total);
            ClustEnergy.push_back(E_total);
            // std::cout << "ClustID: "<<ClstID<< " ClustSize: "<< ClstSize << " " << " ClustTime: "<< ClstTime/E_total << " "<<x_pos/E_total << " " << E_total << std::endl; 
          }
          LastPosition = CurrentPos;
          LastTime = CurrentTime;
          LastEnergy = CurrentEnergy;
          ClstSize = 1;
          x_pos = 0;
          ClstTime = 0;
          E_total = 0;
          NewCluster = false;
          x_pos += CurrentEnergy*CurrentPos;
          ClstTime += CurrentEnergy*CurrentTime;
          E_total += CurrentEnergy;
          if(ValidCluster)
          {
            NewCluster=true;
          }
          ClstID++;
        }
        else{
        ClstTime+=CurrentEnergy*CurrentTime;
        x_pos+=CurrentPos*CurrentEnergy;
        E_total+=CurrentEnergy;
        LastPosition=CurrentPos;
        LastEnergy=CurrentEnergy;
        LastTime = CurrentTime;
        if(ValidCluster)
          {
          NewCluster=true;
          }
        ClstSize++;
        }
      }

  }

  //in case the cluster has a single element or it reached the end
  if(NewCluster)
    {
    ClustSize.push_back(ClstSize);
    ClustTime.push_back(ClstTime/E_total);
    ClustPosX.push_back(x_pos/E_total);
    ClustEnergy.push_back(E_total);
    // std::cout << ">>>>>>>>>ClustID: "<<ClstID<< " ClustSize: "<< ClstSize << " " << " ClustTime: "<< ClstTime/E_total << " "<<x_pos/E_total << " " << E_total << std::endl;  
    }
      

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

  std::vector <int> CSize ={};
  std::vector <double> ClstPosX ={};
  std::vector <double> ClstEnergy ={};
  std::vector <double> ClstTime ={};
  std::array<double, 1024> n_chns={};
  std::array<double, 1024> sum_cm={};



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
      E_max=0;
      T_max=0;
      E_int=0;
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
      
      if(E_max>1 && E_max<1024){ //checking values and filling vectors
      hit.push_back(make_tuple(T_max, x[i], E_int, evt_ok)); 
      }

      evt_ok=false;
    }
    // std::cout <<"=============begin====================="<<std::endl;
    //  for(int y=0; y<hit.size() ; y++ )
    //  {
    //   std::cout <<get<0>(hit[y]) << " "<< get<1>(hit[y]) <<" "<< get<2>(hit[y]) <<" "<<get<3>(hit[y]) <<std::endl;
    //  }
    // std::cout <<"=============sorted====================="<<std::endl;
     
    std::sort(hit.begin(),hit.end(), sort_second);
    std::sort(hit.begin(), hit.end());
    

  //    for(int y=0; y<hit.size() ; y++ )
  //    {
  //     std::cout <<get<0>(hit[y]) << " "<< get<1>(hit[y]) <<" "<< get<2>(hit[y]) <<" "<<get<3>(hit[y]) <<std::endl;
  //    }
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
      // std::cout << ClstID <<" "<<ClstSize<<" "<<xcm<<" "<<E<<std::endl;
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
