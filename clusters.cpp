#include <sampasrs/clusters.hpp>

// Implementation of the mergeEntries function
std::vector<MergedEntry> mergeEntries(const std::vector<int>& CSizeX, const std::vector<double>& TClstX,
                                      const std::vector<double>& xcm, const std::vector<double>& Ex, 
                                      const std::vector<int>& CSizeY, const std::vector<double>& TClstY, 
                                      const std::vector<double>& ycm, const std::vector<double>& Ey,
                                      double valueThreshold) {
    std::vector<MergedEntry> merged;
    std::vector<bool> isXUsed(TClstX.size(), false); // Tracks whether an entry has been merged
    std::vector<bool> isYUsed(TClstY.size(), false); // Tracks whether an entry has been merged

    for (size_t i = 0; i < TClstX.size(); ++i) {
        if (isXUsed[i]) continue; // Skip if already merged
        for (size_t j = 0; j < TClstY.size(); ++j) {
            if (isYUsed[j]) continue; // Skip if already merged
            if (std::abs(TClstX[i] - TClstY[j]) <= valueThreshold) {
                MergedEntry entry;
                entry.CSizex = CSizeX[i];
                entry.CSizey = CSizeY[j];
                entry.TClstX = TClstX[i];
                entry.TClstY = TClstY[j];
                entry.Ex = Ex[i];
                entry.Ey = Ey[j];
                entry.TClstt = (TClstX[i] * Ex[i] + TClstY[j] * Ey[j]) / (Ex[i] + Ey[j]); // Weighted average
                entry.Et = Ex[i] + Ey[j];
                entry.xcm = xcm[i];
                entry.ycm = ycm[j];
                // Include the entry only if both clusters have valid (non-negative) values
                if (TClstX[i] >= 0 && TClstY[j] >= 0) {
                    merged.push_back(entry);
                }
                // Mark entries as merged
                isXUsed[i] = true;
                isYUsed[j] = true;
                break; // Stop further merges for the current `i`
            }
        }
    }
    return merged;
}

bool sort_by_chn(const Hits_evt& a, const Hits_evt& b)
{
    // smallest comes first
    return a.x_pos < b.x_pos;   
}

std::pair<int, int> GetMaxWord(std::vector <Hits_evt> hits)
{

  int max_word=0;
  int max_channel=0;
  int Last_word = 0;
  for (int i = 0; i< hits.size(); i++)
  {
    for(int j = 0; j<hits[i].time.size(); j++)
    {
      if(hits[i].word[j]>Last_word)
      {
        max_word=j;
        max_channel=i;
        Last_word = hits[i].word[j];
      }
    }
  }
  return std::make_pair(max_channel, max_word);
}

void Make_1D_Strip_Cluster(std::vector <Hits_evt> hits, std::vector <int> &ClustSize,std::vector <double> &ClustTime, 
std::vector <double> &ClustPos,std::vector <double> &ClustEnergy)

{  
  double pitch = 0.390625; //pitch real = 0.390625
  const int max_time_window = 4; //maximum time difference before the maximum to be checked at 20MSps, 1 = 50ns
  const int Min_Number_words = 4; //Minimum number of ADC samples to consider a cluster valid  
  double xcm=0;
  double E_total=0;
  int ClstID=0;
  int MaxNClust=0;
  double ClstTime = 0;
  std::vector <double> del_index = {};
  std::vector <double> del_words = {};
  int word_count = 0;
  while(hits.size()>0)
  {

    auto max_idx = GetMaxWord(hits);

      for (int i = 0; i< hits.size(); i++)
      {
        for(int j = 0; j<hits[i].time.size(); j++)
        {
        if( abs(hits[max_idx.first].x_pos-hits[i].x_pos) <= 2*pitch )
          {
            if(abs( hits[max_idx.first].time[max_idx.second]-hits[i].time[j]) <= max_time_window/2)      //maximum time separation
            {
              xcm += hits[i].x_pos*hits[i].word[j];
              E_total += hits[i].word[j];
              ClstTime += hits[i].time[j]*hits[i].word[j];
              del_words.push_back(j);
              word_count++;
            }

          }
        }

        for (unsigned k = del_words.size(); k-- > 0; )
          {
            hits[i].word.erase(hits[i].word.begin()+del_words.at(k));
            hits[i].time.erase(hits[i].time.begin()+del_words.at(k));        
          }
            del_words.clear();
      }


  for(int i = 0; i<hits.size(); i++)
  {
    if(hits[i].word.size()==0)
    del_index.push_back(i);
  }

  for (unsigned k = del_index.size(); k-- > 0; )
  {
    hits.erase(hits.begin()+del_index.at(k));  
  }
  del_index.clear();

  if(word_count>=Min_Number_words)
  {
    ClustSize.push_back(word_count);
    ClustPos.push_back(xcm/E_total);
    ClustEnergy.push_back(E_total);
    ClustTime.push_back(ClstTime/E_total);
  }

  MaxNClust++;
  xcm = 0;
  E_total = 0;
  ClstTime = 0;
  word_count = 0;

  }
}

