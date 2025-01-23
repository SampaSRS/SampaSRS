#include <cstring> 
#include <cstdio>
#include <vector>
#include <iostream>


// void Make_1D_Strip_Cluster(std::vector <Hits_evt> hits, std::vector <int> &ClustSize,std::vector <double> &ClustTime, 
// std::vector <double> &ClustPos,std::vector <double> &ClustEnergy)

// {  
//   double pitch = 0.390625; //pitch real = 0.390625
//   const int max_time_window = 8; //maximum time difference before the maximum to be checked at 20MSps, 1 = 50ns
//   const int Min_Number_words = 5; //Minimum number of ADC samples to consider a cluster valid  
//   double xcm=0;
//   double E_total=0;
//   int ClstID=0;
//   int MaxNClust=0;
//   double ClstTime = 0;
//   std::vector <double> del_index = {};
//   std::vector <double> del_words = {};
//   int word_count = 0;
//   while(hits.size()>0)
//   {

//     auto max_idx = GetMaxWord(hits);

//       for (int i = 0; i< hits.size(); i++)
//       {
//         for(int j = 0; j<hits[i].time.size(); j++)
//         {
//         if( abs(hits[max_idx.first].x_pos-hits[i].x_pos) <= 2*pitch )
//           {
//             if(abs( hits[max_idx.first].time[max_idx.second]-hits[i].time[j]) <= max_time_window/2)      //maximum time separation
//             {
//               xcm += hits[i].x_pos*hits[i].word[j];
//               E_total += hits[i].word[j];
//               ClstTime += hits[i].time[j]*hits[i].word[j];
//               del_words.push_back(j);
//               word_count++;
//             }

//           }
//         }

//         for (unsigned k = del_words.size(); k-- > 0; )
//           {
//             hits[i].word.erase(hits[i].word.begin()+del_words.at(k));
//             hits[i].time.erase(hits[i].time.begin()+del_words.at(k));        
//           }
//             del_words.clear();
//       }


//   for(int i = 0; i<hits.size(); i++)
//   {
//     if(hits[i].word.size()==0)
//     del_index.push_back(i);
//   }

//   for (unsigned k = del_index.size(); k-- > 0; )
//   {
//     hits.erase(hits.begin()+del_index.at(k));  
//   }
//   del_index.clear();

//   if(word_count>=Min_Number_words)
//   {
//     ClustSize.push_back(word_count);
//     ClustPos.push_back(xcm/E_total);
//     ClustEnergy.push_back(E_total);
//     ClustTime.push_back(ClstTime/E_total);
//   }

//   MaxNClust++;
//   xcm = 0;
//   E_total = 0;
//   ClstTime = 0;
//   word_count = 0;

//   }
// }


// std::vector<MergedEntry> mergeEntries(const std::vector<int>& CSizeX, const std::vector<double>& TClstX,
//                                       const std::vector<double>& xcm, const std::vector<double>& Ex, const std::vector<int>& CSizeY,
//                                       const std::vector<double>& TClstY, const std::vector<double>& ycm, const std::vector<double>& Ey,
//                                       double valueThreshold) {
//     std::vector<MergedEntry> merged;
//     std::vector<bool> isXUsed(TClstX.size(), false); // Tracks whether an entry has been merged
//     std::vector<bool> isYUsed(TClstY.size(), false); // Tracks whether an entry has been merged

//     for (size_t i = 0; i < TClstX.size(); ++i) {
//         if (isXUsed[i]) continue; // Skip if already merged
//         for (size_t j = 0; j < TClstY.size() ; ++j) {
//           if (isYUsed[j]) continue; // Skip if already merged
//             if (std::abs(TClstX[i] - TClstY[j]) <= valueThreshold ) 
//             {
//                 MergedEntry entry;
//                 entry.CSizex = CSizeX[i];
//                 entry.CSizey = CSizeY[j];
//                 entry.TClstX = TClstX[i];
//                 entry.TClstY = TClstY[j];
//                 entry.Ex = Ex[i];
//                 entry.Ey = Ey[j];
//                 entry.TClstt = (TClstX[i]*Ex[i]+TClstX[j]*Ex[j]) / Ex[i]+Ex[j]; 
//                 entry.Et = Ex[i]+Ey[j];
//                 entry.xcm = xcm[i];
//                 entry.ycm = ycm[j];
//                 merged.push_back(entry);
//                 isXUsed[i] = true; // Mark both entries as merged
//                 isYUsed[j] = true;
//                 break; // Stop further merges for the current `i`
//             }
//         }
//     }
//     return merged;
// }



void Make_Cluster_Old(std::vector<std::tuple<int, double, int, int >> hit,std::vector <int> &ClustSize,std::vector <double> &ClustTime, 
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
      if( abs(LastPosition-CurrentPosition) <= 2*pitch )
      {
        if( abs(LastTime-CurrentTime) <= max_timediff_Emax )
        {
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

