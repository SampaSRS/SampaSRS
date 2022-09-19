#include <iostream>
#include <numeric>
#include <string_view>
#include <vector>

#include "TFile.h"
#include "TTreeReader.h"

void read_tree(std::string_view file_name = "Run47.root")
{
  if (file_name.empty()) {
    return; // just a precaution
  }

  TFile file(file_name.data(), "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype

int event_id = 0;
  while (reader.Next() && event_id < 10) { //getting only 10 evts
    auto& event_words = *words;
    long sum = 0;
    for (size_t i = 0; i < event_words.size(); ++i) { //acessing waveform i
      for (size_t j = 0; j < event_words[i].size(); ++j) { //acessing word j of waveform i
        sum += event_words[i][j];
      }
    }

    std::cout << event_id << " " << sum << "\n";
    ++event_id;
  }

}
