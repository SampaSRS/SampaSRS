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
      my_map[glchn] = {baseline,sigma};
      // std::cout << glchn << " " << baseline <<" "<<sigma<<std::endl;
      if( mapfile.eof() ) 
      {
        break;
      }
    }
    mapfile.close();
}


void Make_Cluster(std::vector<std::pair<double, std::pair<int,int>>>hit,std::vector <int> &ClustSize,
std::vector <double> &ClustPosX,std::vector <double> &ClustEnergy)

{  
  double pitch = 0.390625; //pitch real = 0.390625
  double x_pos=0;
  double E_total=0;
  int ClstID=0;
  int ClstSize=1;
  // std::cout<<0<<" "<< hit[0].first<<" "<<hit[0].second.first<<" "<<hit[0].second.second<<std::endl;
  
  bool NewCluster=hit.at(0).second.second;
  double LastPosition=hit.at(0).first;
  double LastEnergy=hit.at(0).second.first;
  
  x_pos+=LastPosition*LastEnergy;
  E_total+= LastEnergy;

  for(int i=1; i<hit.size() ; i++ )
  {
    // std::cout<<i<<" "<< hit[i].first<<" "<<hit[i].second.first<<" "<<hit[i].second.second<<std::endl;
    //checa se existe um cluster em aberto
    // if(NewCluster)
    // {
      // std::cout <<"diff: "<<abs(hit[i].first/pitch-LastPosition) <<std::endl;
      //se a distancia atual é maior que um pitch da ultima distancia marcada
      if(abs(hit[i].first-LastPosition)>pitch)
        {
          //fecha o cluster atual e guarda a distancia atual como a ultima
          // NewCluster=false;
          if(NewCluster)
          {
            ClustSize.push_back(ClstSize);
            ClustPosX.push_back(x_pos/E_total);
            ClustEnergy.push_back(E_total);
            // std::cout << "ClustID: "<<ClstID<< " ClustSize: "<< ClstSize << " " <<x_pos/E_total << " " << E_total << std::endl; 
          }
          LastPosition=hit[i].first;
          LastEnergy=hit[i].second.first;
          ClstSize=1;
          x_pos=0;
          E_total=0;
          NewCluster=false;
          x_pos+=hit[i].first*hit[i].second.first;
          E_total+=hit[i].second.first;
          if(hit[i].second.second)
          {
            NewCluster=true;
          }
          ClstID++;
          // std::cout <<"here1"<<std::endl;
        }

    else
    {
      // NewCluster=true;
      x_pos+=hit[i].first*hit[i].second.first;
      E_total+=hit[i].second.first;
      LastPosition=hit[i].first;
      LastEnergy=hit[i].second.first;
      if(hit[i].second.second)
      {
        NewCluster=true;
      }
      ClstSize++;
      // std::cout <<"here3"<<std::endl;
    }
    

  }
  // std::cout<<"here4"<<std::endl;
  if(abs(hit.back().first-LastPosition)>pitch)
        {
          //fecha o cluster atual e guarda a distancia atual como a ultima
          ClstSize=1;
          x_pos=0;
          E_total=0;
          x_pos+=hit.back().first*hit.back().second.first;
          E_total+=hit.back().second.first;
          if(hit.back().second.second)
          {
            NewCluster=true;
          }
          if(NewCluster)
          {
            ClustSize.push_back(ClstSize);
            ClustPosX.push_back(x_pos/E_total);
            ClustEnergy.push_back(E_total);
            // std::cout << "ClustID: "<<ClstID<< " ClustSize: "<< ClstSize << " " <<x_pos/E_total << " " << E_total << std::endl; 
          }
          NewCluster=false;
          // std::cout <<"here1"<<std::endl;
        }
      //se a distancia atual é menor que um pitch da ultima distancia marcada
      else 
      {
        // x_pos+=hit.back().first/pitch*hit.back().second.first;
        // E_total+=hit.back().second.first;
        if(NewCluster)
        {
          ClustSize.push_back(ClstSize);
          ClustPosX.push_back(x_pos/E_total);
          ClustEnergy.push_back(E_total);
          // std::cout << "ClustID: "<<ClstID<< " ClustSize: "<< ClstSize << " " <<x_pos/E_total << " " << E_total << std::endl;  
        }
      }

}

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


  std::unordered_map<int, std::pair<double, double>> map_of_pedestals = {};

  Map_pedestal(pedestal_file, map_of_pedestals); // change the mapping on mapping.hpp for a diferent detector

  int gl_chn=0;
  int max_word=0;
  int E_max=0;
  int E_int=0;
  int T_max=0;

  std::vector <int> CSize ={};
  std::vector <double> ClstPosX ={};
  std::vector <double> ClstEnergy ={};

  

  std::vector <std::pair <double, std::pair<int, int>>> hit ={};

  bool evt_ok=false;
 
int event_id = 0;
  while (reader.Next()) 
  {
    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) 
    {
      // std::cout <<event_words.size()<<std::endl;
      E_max=0;
      T_max=0;
      // std::cout << channel[i] <<" "<<sampa[i]<<std::endl;
      gl_chn = 32*(sampa[i]-8)+channel[i];
      // for (size_t j = 2; j < event_words[i].size(); ++j) {
      for (size_t j = 2; j < 25; ++j) 
      { //pico está +- entre 0 e 25   
        if(event_words[i][j] > map_of_pedestals[gl_chn].first+3*map_of_pedestals[gl_chn].second)
        { //first threshold
          // E_int += event_words[i][j]-map_of_pedestals[gl_chn].first;
          if(event_words[i][j]-map_of_pedestals[gl_chn].first>E_max)
          {
            E_max = event_words[i][j]-map_of_pedestals[gl_chn].first;
            T_max = j;
            if(event_words[i][j] > map_of_pedestals[gl_chn].first+5*map_of_pedestals[gl_chn].second)
            { //validation threshold not yet implemented in the clustering
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

    // std::cout << event_id <<std::endl;
    if(!hit.empty())
    {
      Make_Cluster(hit, CSize, ClstPosX, ClstEnergy);
    }

    for(int j = 0; j<ClstPosX.size(); j++)
    {
      ClstID = j;
      trgID = event_id;
      ClstSize = CSize.at(j);
      xcm = ClstPosX.at(j);
      E = ClstEnergy.at(j);
      // std::cout << ClstID <<" "<<ClstSize<<" "<<xcm<<" "<<E<<std::endl;
      MyTree->Fill();
    }

    
    CSize.clear();
    ClstEnergy.clear();
    ClstPosX.clear();
    hit.clear();
    
    evt_ok=false;
    
    E_int = 0;
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
