#ifndef MERGE_ENTRIES_HPP
#define MERGE_ENTRIES_HPP

#include <vector>
#include <cmath> 

struct MergedEntry {
    int CSizex;
    int CSizey;
    double TClstX;
    double TClstY;
    double Ex;
    double Ey;
    double TClstt;
    double Et;
    double xcm;
    double ycm;
};

struct Hits_evt {
  int gl_chn;
  std::vector <int> time;
  std::vector <int> word;
  double x_pos;
};


std::vector<MergedEntry> mergeEntries(const std::vector<int>& CSizeX, const std::vector<double>& TClstX,
                                      const std::vector<double>& xcm, const std::vector<double>& Ex, 
                                      const std::vector<int>& CSizeY, const std::vector<double>& TClstY, 
                                      const std::vector<double>& ycm, const std::vector<double>& Ey,
                                      double valueThreshold);

bool sort_by_chn(const Hits_evt& a, const Hits_evt& b);

std::pair<int, int> GetMaxWord(std::vector <Hits_evt> hits);


void Make_1D_Strip_Cluster(std::vector <Hits_evt> hits, std::vector <int> &ClustSize,std::vector <double> &ClustTime, 
std::vector <double> &ClustPos,std::vector <double> &ClustEnergy);




#endif // MERGE_ENTRIES_HPP