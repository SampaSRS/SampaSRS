#include <algorithm>
#include <iostream>
#include <fstream>
#include <cstring> 
#include <cstdio>
#include <vector>

namespace sampasrs {


#include <iostream>
#include <string>
 
inline void Mapping_strips(std::unordered_map<int, std::pair<double, double>> &my_map)
{
    // Create an unordered_map of three values glchn(sampa*32+Chn)    
    std::ifstream mapfile;
    int glchn=0;
    double xcm=0;
    double ycm=0;
    std::string str;
    mapfile.open("../mapping_files/Mapping_strips.txt");
    mapfile >> str;
    mapfile >> str;
    mapfile >> str;
    for(int i=0;i<128;i++)
    {
        mapfile >> glchn;
        mapfile >> xcm;
        mapfile >> ycm;
        // std::cout << glchn<<" "<<xcm<<" "<<ycm<<std::endl;
        my_map[i] = {xcm, ycm};
    }
    mapfile.close();
}

}