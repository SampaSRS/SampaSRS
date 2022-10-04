#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstring> 
#include <cstdio>
#include <vector>

namespace sampasrs {


#include <iostream>
#include <string>
#include <unordered_map>
 
int main()
{
    // Create an unordered_map of three strings (that map to strings)
    std::unordered_map<int, std::pair<double, double>> mapping_strips = {};

    x = mapping_strips[0].first;
    
    std::ifstream mapfile;
    int padID=0;
    int glchn=0;
    double xcm=0;
    double ycm=0;
    std::string str;
    mapfile.open("configure_file_dir/MAPPING_Strips.txt");
    mapfile >> str;
    mapfile >> str;
    mapfile >> str;
    mapfile >> str;
    for(int i=0;i<128;i++){
        mapfile >> glchn;
        mapfile >> padID;
        mapfile >> xcm;
        mapfile >> ycm;
        mapping_strips[i] = {x, y};


      PadVec[glchn] = padID;
    	xVec[glchn] = xcm;
    	yVec[glchn] = ycm;
    }
    mapfile.close();



}























inline void FillMap(std::vector<int>& SampaVector)
{
  std::ifstream mapfile;
  int sindex=0;
  int sampaID=0;
  std::string str;
  mapfile.open("configure_file_dir/MAPPING_SAMPA.txt");
  mapfile >> str;
  mapfile >> str;
  for(int i=0;i<16;i++){
    mapfile >> sampaID;
    mapfile >> sindex;
    SampaVector[sampaID] = sindex;
  }

  mapfile.close();
}

inline void FillPadMap(std::vector<int>& PadVec, std::vector<double>& xVec, std::vector<double>& yVec)
{
  std::ifstream mapfile;
  int padID=0;
  int glchn=0;
  double xcm=0;
  double ycm=0;
  std::string str;
  mapfile.open("configure_file_dir/MAPPING_Strips.txt");
  mapfile >> str;
  mapfile >> str;
  mapfile >> str;
  mapfile >> str;
  for(int i=0;i<128;i++){
    mapfile >> glchn;
    mapfile >> padID;
	mapfile >> xcm;
    mapfile >> ycm;
    PadVec[glchn] = padID;
	xVec[glchn] = xcm;
	yVec[glchn] = ycm;
  }
  mapfile.close();
}

}