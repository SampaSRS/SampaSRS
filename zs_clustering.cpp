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

  std::vector <int> CSize ={};
  std::vector <double> ClstPosX ={};
  std::vector <double> ClstEnergy ={};

  

  std::vector <std::pair <double, std::pair<int, int>>> hit ={};

  bool evt_ok=false;
 
int event_id = 0;
  while (reader.Next()) 
  {
    std::cout << "_________new evt__________" << std::endl;
    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) 
    {
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
            std::cout<<event_words[i][j]<<" "; 
          }
          std::cout <<" ] ";
       j++;
      }
      // if(E_max>1 && E_max<1024){ //checking values and filling vectors
      // // std::cout<<event_id<<" "<<evt_ok<<" " << x[i] <<" "<<E_max<<std::endl;
      // hit.push_back(std::make_pair(x[i], std::make_pair(E_max, evt_ok)));
      // }
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
    if(event_id % 10000==0)
    {
      std::cout << event_id <<" events analyzed" <<std::endl;

    }
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
