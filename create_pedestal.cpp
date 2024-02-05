#include <sampasrs/root_fix.hpp>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include <TFile.h>
#include <TCanvas.h>
#include <TLatex.h>
#include <TStyle.h>
#include <TH1D.h>
#include <TTreeReader.h>
#include <TTreeReaderArray.h>

void make_plot(const char *filename)
    {    
    
    gStyle->SetOptStat(0);    //Printa os parametros do fit no gráfico
    gStyle->SetTitleFont(132, "xyz");
    gStyle->SetTitleFont(132, " ");
    gStyle->SetLabelFont(132, "xyz");

    int bin;   
    double mean=0;
    double sigma=0;
    double meany;
    double sigmay;
    TH1D* meanHist =new TH1D("Mean", "Mean", 128, 0, 128);  //Cria um histograma com eixo indo de -5 a 5 e 30 canais
    TH1D* sigmaHist =new TH1D("Sigma", "Sigma", 128, 0, 128);  //Cria um histograma com eixo indo de -5 a 5 e 30 canais

    TH1D* meanHist2 =new TH1D("Mean2", "Mean2", 128, 0, 1024);  //Cria um histograma com eixo indo de -5 a 5 e 30 canais
    TH1D* sigmaHist2 =new TH1D("Sigma2", "Sigma2", 128, 0, 1024);  //Cria um histograma com eixo indo de -5 a 5 e 30 canais

    std::ifstream infile;
    infile.open (filename);
    std::cout<< "Producing pedestal plot" <<std::endl ;

    const char* input_name = filename;
    auto input_path = std::filesystem::path(input_name);
    auto imgfname = input_path.replace_extension(".png").string();

    while(!infile.eof())
	{
    infile>>bin;
    infile>>mean;
    infile>>sigma;
    meanHist->SetBinContent(bin+1,mean);
    sigmaHist->SetBinContent(bin+1,sigma);

    meanHist2->Fill(mean);
    sigmaHist2->Fill(sigma);
    
    //  std::cout << bin << " " << mean << " " <<sigma << std::endl;  
    }

    infile.close();

    auto *c1 = new TCanvas("c1","c1",1600, 800);
    c1->Divide(2,1);
    c1->cd(1);
    meanHist->Draw();
    meany=meanHist2->GetMean();
    sigmay=meanHist2->GetRMS();
    meanHist->SetTitle("Baseline; Channel; Mean value (ADC channels)");

    std::cout<<"Baseline mean: "<< meany <<"/baseline standard deviation: "<<sigmay<<" "<<std::endl;
    
    // TLatex latex;   
    // latex.SetTextSize(0.05);
    // latex.SetTextAlign(13);  //align at top
    // latex.SetTextFont(12);

    // char* buf = new char[100];
	  // sprintf(buf,"Mean = %4.1f", meany);
    // latex.DrawLatex(61,150,buf); //Escreve sigma na imagem
    // sprintf(buf,"#sigma = %4.1f", sigmay);
    // latex.DrawLatex(61,140,buf); //Escreve sigma na imagem

    c1->cd(2);
    sigmaHist->Draw();
    sigmaHist->SetTitle("Noise; Channel; Std value (ADC channels)");
    meany=sigmaHist2->GetMean();
    sigmay=sigmaHist2->GetRMS();

    std::cout<<"Noise mean: "<< meany <<"/Noise standard deviation: "<<sigmay<<" "<<std::endl;
    
    // latex.SetTextSize(0.05);
    // latex.SetTextAlign(13);  //align at top
    // sprintf(buf,"Mean = %4.1f",meany);
    // latex.DrawLatex(60,14,buf); //Escreve sigma na imagem
    // sprintf(buf,"#sigma = %4.1f",sigmay);
    // latex.DrawLatex(60,13,buf); //Escreve sigma na imagem

    c1->SaveAs(imgfname.c_str());

    }




int main(int argc, const char* argv[])
{
  if (argc < 2) {
    std::cerr << "Usage: create_pedestal <Pedestal_file.root>\n";
    return 1;
  }
  bool PlotPedestal=true;
  bool Gen_ZS_file=true;
  const char* input_name = argv[1];
  std::string input_name_str = input_name;

  std::string rootfname=input_name_str.substr(0,input_name_str.find_last_of('.'))+"_pedestal.txt";
  std::string zsfname=input_name_str.substr(0,input_name_str.find_last_of('.'))+"_ZSconfig.txt";
  

  TFile file(input_name, "READ");
  TTreeReader reader("waveform", &file);
  TTreeReaderValue<std::vector<std::vector<short>>> words(reader, "words"); // template type must match datatype
  TTreeReaderArray<short> sampa(reader, "sampa");
  TTreeReaderArray<short> channel(reader, "channel");

  struct Pedestal {
    short sampa = -1;
    short channel = -1;
    size_t count = 0;
    double sum = 0;
    double sum_squared = 0;
  };

  std::map<int, Pedestal> channels {};

  int const NumEvts = 500; // Number of events for pedestal file

  int event_id = 0;
  while (reader.Next() && event_id < NumEvts) {
    auto& event_words = *words;
    for (size_t i = 0; i < event_words.size(); ++i) {
      const int global_channel = (sampa[i] - 8) * 32 + channel[i];
      auto& pedestal = channels[global_channel];

      // Fill info on first occurrence
      if (pedestal.count == 0) {
        pedestal.sampa = sampa[i];
        pedestal.channel = channel[i];
      }

      for (size_t j = 2; j < event_words[i].size(); ++j) {
        pedestal.sum += event_words[i][j];
        pedestal.sum_squared += std::pow(event_words[i][j], 2);
        ++pedestal.count;
      }
    }
    event_id++;
  }

  const size_t SIZE = 128; // However many elements you want in the vector.  
  const bool initial_value = false; // All elements will be set to this value
  std::vector<bool> Has_pedestal_vec(SIZE, initial_value);

  std::cout << "Generating pedestal file: " << rootfname << "\n";
  std::ofstream TxtOutFile(rootfname);
  std::ofstream ZSOutFile(zsfname);

  ZSOutFile << "stop" << "\n";
  ZSOutFile << "reset_fec" << "\n";
  ZSOutFile << "reset_sampas" << "\n";
  ZSOutFile << "trigger_external" << "\n";
  ZSOutFile << "pretrigger 25" << "\n";

  for (auto& [global_channel, pedestal] : channels) {
    const double mean = pedestal.sum / static_cast<double>(pedestal.count);
    const double var = pedestal.sum_squared / static_cast<double>(pedestal.count) - std::pow(mean, 2);
    TxtOutFile << global_channel << " " << mean << " " << std::sqrt(var) << "\n";
    //ZS cut = baseline+3sigma ATTENTION: SAMPA index is sent from 8 to 11 but needs to be written from 0 to 4 (see sampa-8).
    if(var==0){ //channel locked - suppress it at maximum
      ZSOutFile <<"set_zero_suppression "<< pedestal.sampa-8 << " " << pedestal.channel << " " << 1023 << "\n"; 
    }
    else {
      ZSOutFile <<"set_zero_suppression "<< pedestal.sampa-8 << " " << pedestal.channel << " " << static_cast<int>(mean+3*std::sqrt(var) )<< "\n";
    }
    Has_pedestal_vec[32*(pedestal.sampa-8)+pedestal.channel]=true; //fill the channels with true
  }

  for(int i=0; i< Has_pedestal_vec.size(); i++)
    {
      if(!Has_pedestal_vec[i]){
        ZSOutFile <<"set_zero_suppression "<< (int) i/32 << " " << i%32 << " " << 1023 << "\n";  //If the channel has no entry, suppress it at maximum (just in case)
      }
    }   
  TxtOutFile.close();
  ZSOutFile.close();
  if(PlotPedestal)
  {
    make_plot(rootfname.c_str());
  }

  return 0;
}
// 