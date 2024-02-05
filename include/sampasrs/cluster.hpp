#include <cstring> 
#include <cstdio>
#include <vector>
#include <iostream>



void Make_Cluster(std::vector<std::tuple<int, double, int, int >> hit,std::vector <int> &ClustSize,std::vector <double> &ClustTime, 
std::vector <double> &ClustPos,std::vector <double> &ClustEnergy)

{  
  double pitch = 0.390625; //pitch real = 0.390625
  double max_timediff_Emax = 2;

  double x_pos=0;
  double E_total=0;
  double ClstTime = 0;
  int ClstID=0;
  int ClstSize=1;

  int CurrentTime =0, LastTime = 0;
  double CurrentPosition =0, LastPosition = 0;
  double CurrentEnergy = 0, LastEnergy = 0;
  int ValidCluster = 0, TrueClst = 0;
  
  std::vector <double> del_index = {};


  for(int i = 0; i<hit.size(); i++){
    // std::cout <<"Size of hit"<< hit.size() <<std::endl;
    LastTime = std::get<0>(hit[i]); 
    LastPosition=std::get<1>(hit[i]);
    LastEnergy=std::get<2>(hit[i]);
    TrueClst=std::get<3>(hit[i]);

    ClstSize = 1;
    x_pos += LastPosition*LastEnergy;
    E_total += LastEnergy;
    ClstTime += LastTime*LastEnergy;


    // std::cout<<"Starting here: " <<std::get<0>(hit[i]) << " "<< std::get<1>(hit[i]) <<" "<< std::get<2>(hit[i]) <<" "<<std::get<3>(hit[i]) <<std::endl;

    for(int j=i+1; j<hit.size() ; j++ ){
      // std::cout<<"Next " <<std::get<0>(hit[j]) << " "<< std::get<1>(hit[j]) <<" "<< std::get<2>(hit[j]) <<" "<<std::get<3>(hit[j]) <<std::endl;
      CurrentTime = std::get<0>(hit[j]); 
      CurrentPosition = std::get<1>(hit[j]); 
      CurrentEnergy = std::get<2>(hit[j]); 
      ValidCluster = std::get<3>(hit[j]); 
      if( abs(LastPosition-CurrentPosition) <= 2*pitch ){
        if( abs(LastTime-CurrentTime) <= max_timediff_Emax ){
          TrueClst += ValidCluster;
          x_pos += CurrentPosition*CurrentEnergy;
          E_total += CurrentEnergy;
          ClstTime += CurrentTime*CurrentEnergy;
          ClstSize++;
          LastTime = CurrentTime; 
          LastPosition = CurrentPosition;
          // std::cout <<"---yes----"<<std::endl;
          del_index.push_back(j);
        }
      //   else{
      //   std::cout <<"---no----"<<std::endl;
      //   }
      // }
      // else{
      //   // std::cout <<"---no----"<<std::endl;
        
        
      }
    }
    for (unsigned k = del_index.size(); k-- > 0; ){
      // std::cout << k <<"-------------------------"<< del_index.at(k) <<std::endl;
      hit.erase(hit.begin()+del_index.at(k));
    }
    del_index.clear();
    if(TrueClst){
      ClustSize.push_back(ClstSize);
      ClustPos.push_back(x_pos/E_total);
      ClustEnergy.push_back(E_total);
      ClustTime.push_back(ClstTime/E_total);
    }
    x_pos = 0;
    E_total = 0;
    ClstTime = 0;
    ClstSize = 1;
    TrueClst = 0;
  }

      

}

