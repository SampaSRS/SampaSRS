#include "sampasrs/acquisition.hpp"

#include "ImGuiFileDialog.h"
#include "boost/histogram.hpp"
#include "hello_imgui/hello_imgui.h"
#include "imgui.h"
#include "implot.h"
#include "misc/cpp/imgui_stdlib.h"

#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>
#include <TEnv.h>

using namespace sampasrs;

using HistAxis = boost::histogram::axis::integer<int, boost::histogram::use_default, boost::histogram::axis::option::none_t>; // no under-overflow
using Hist = boost::histogram::histogram<std::tuple<HistAxis>>;

// Grow histogram limits if needed
static void resize(Hist& hist, int min, int max)
{
  using namespace boost::histogram;
  auto tmp = make_histogram(HistAxis(min, max));
  for (auto&& bin : indexed(hist)) {
    tmp(bin.bin(0).center(), weight(*bin));
  }
  hist = std::move(tmp);
}

class Graphs {
  public:
  Graphs()
  {
    m_waveform.reserve(1003);
  }

  const std::vector<short>& get_waveform() const { return m_waveform; }
  Hist& get_energy_hist() { return m_energy_hist; }
  Lock<Hist> get_channel_hist() { return {m_channel_hist, m_channel_lock}; }
  void reset()
  {
    m_channel_hist.reset();
    m_energy_hist.reset();
    m_waveform.clear();
  }

  void event_handle(Event&& event)
  {
    if (!event.valid()) {
      return;
    }

    // Find channel and value of the highest adc measurement for this event
    size_t selected_waveform = 0; // event channel waveform with highest adc value
    short energy = 0;             // highest event adc value
    int min_channel = std::numeric_limits<int>::max();
    int max_channel = 0;

    for (size_t waveform = 0; waveform < event.waveform_count(); ++waveform) {
      for (size_t word = 1; word < event.word_count(waveform); ++word) { //start from 1 since the word 0 this the number of words
        const int channel = event.get_header(selected_waveform).global_channel();
        min_channel = std::min(min_channel, channel);
        max_channel = std::max(max_channel, channel + 1);

        auto signal = event.get_word(waveform, word);
        if (energy < signal) {
          selected_waveform = waveform;
          energy = signal;
        }
      }
    }

    // Update histograms
    if (energy > 0) {
      const auto header = event.get_header(selected_waveform);
      const int channel = header.global_channel();

      // Adjust number of channels in the histogram
      if (m_channel_hist.size() == 0) {
        m_channel_hist = boost::histogram::make_histogram(HistAxis(min_channel, max_channel));
      } else if (
          m_channel_hist.axis(0).begin()->lower() > min_channel
          || m_channel_hist.axis(0).end()->lower() < max_channel) {
        //  Grow the channel histogram, the lock is needed to prevent data races during resize
        auto lock_hist = get_channel_hist();
        resize(lock_hist.item, min_channel, max_channel);
      }

      m_channel_hist(channel);
      m_energy_hist(energy);

      if (m_waveform_timer) {
        const auto size = std::min(static_cast<size_t>(header.word_count()), m_waveform.capacity()); // this min will prevent reallocations
        m_waveform.resize(size);
        for (size_t i = 0; i < size; ++i) {
          m_waveform[i] = event.get_word(selected_waveform, i);
        }
      }
    }
  }

  private:
  Timer m_waveform_timer {std::chrono::milliseconds(250)};
  Hist m_energy_hist {boost::histogram::make_histogram(HistAxis(0, 1024))};
  Hist m_channel_hist {};
  std::vector<short> m_waveform {};
  std::mutex m_channel_lock;
};

template <typename Number>
void gui_info(const char* label, Number value, const char* suffix = "",
    const ImVec4& color = {1, 1, 1, 1})
{
  ImGui::Text("%s:", label);
  ImGui::SameLine();
  if constexpr (std::is_same<Number, float>::value || std::is_same<Number, double>::value) {
    ImGui::TextColored(color, "%.2f", value);
  } else if constexpr (std::is_same<Number, int>::value) {
    ImGui::TextColored(color, "%d", value);
  } else if constexpr (std::is_same<Number, size_t>::value) {
    ImGui::TextColored(color, "%zu", value);
  } else {
    ImGui::TextColored(color, "%d", static_cast<int>(value));
  }

  if (suffix[0] != '\0') {
    ImGui::SameLine();
    ImGui::Text("%s", suffix);
  }
}

template <typename Number>
void gui_info_colored(const char* label, Number value, float min, float max,
    const char* suffix = "")
{
  static constexpr float min_map = 0.3;
  static constexpr float max_map = 0.85;
  float map = (static_cast<float>(value) - min) * (max_map - min_map) / (max - min) + min_map;
  map = std::max(min_map, std::min(map, max_map));

  auto color = ImPlot::SampleColormap(map, ImPlotColormap_Jet);

  gui_info(label, value, suffix, color);
}

static ImPlotPoint hist_data_getter(int idx, void* _hist)
{
  const auto* hist = static_cast<const Hist*>(_hist);
  return {hist->axis(0).value(idx), hist->at(idx)};
}

class App {
  public:
  void run(const char* fconf = "AcqConfig.conf")
  {
    start_acquisition(fconf);
    info();
    graphs();
  }

  void start_acquisition(const char* fconf)
  {
    // Acquisition configs
    TEnv env(fconf);
    
    static constexpr bool save_raw = true;
    static constexpr bool process_events = true;
    //static const std::string fec_address = "10.0.0.2";
    static const std::string fec_address = env.GetValue("fec_address","");
    static const auto event_handler = [&](Event&& event) { m_graphs.event_handle(std::move(event)); };

    // Style constants
    static const ImVec4 red {1, 0, 0, 1};
    static const ImVec4 green {0, 0.5, 0, 1};
    static const ImVec4 gray {0.5, 0.5, 0.5, 1};
    static const ImVec2 start_button_size {80, 50};

    // GUI state
    static bool save_to_file = true;
    static unsigned char acquisition_error = Acquisition::Stop;
    //static std::string file_prefix = "~/data/SAMPA/24";
    static std::string file_prefix = env.GetValue("file_prefix","");

    ImGui::PushStyleColor(ImGuiCol_ButtonActive, gray);
    if (m_acquisition) {
      // Acquisition in progress
      ImGui::PushStyleColor(ImGuiCol_Button, red);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, red);

      if (ImGui::Button("Stop", start_button_size)) {
        // Stop if requested
        m_acquisition.reset();
        m_graphs.reset();
        acquisition_error = Acquisition::Stop;
        std::cout << "Acquisition stoped as requested\n";
      } else {
        // Check for errors
        acquisition_error = m_acquisition->get_state();
        if (acquisition_error != Acquisition::Run) {
          m_acquisition.reset();
          m_graphs.reset();
          std::cerr << "Acquisition stoped by error\n";
        }
      }

      ImGui::PopStyleColor(3);

      ImGui::SameLine();
      if (save_to_file) {
        ImGui::Text("Writing to: %s-*.raw", file_prefix.data());
      } else {
        ImGui::TextColored(red, "Warning: not saving data to disk");
      }

    } else {
      // Acquisition not started
      ImGui::PushStyleColor(ImGuiCol_Button, green);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, green);

      if (ImGui::Button("Start", start_button_size)) {
        if (save_to_file) {
          m_acquisition = std::make_unique<Acquisition>(
              file_prefix,
              save_raw,
              event_handler,
              fec_address);
        } else {
          m_acquisition = std::make_unique<Acquisition>(
              event_handler,
              fec_address);
        }
      }
      ImGui::PopStyleColor(3);

      ImGui::SameLine();
      ImGui::Checkbox("Save to file", &save_to_file);

      if (save_to_file) {
        ImGui::SameLine();
        ImGui::InputText("File name", &file_prefix);
        ImGui::SameLine();
        file_dialog(file_prefix);
      }

      // Inform errors
      if ((acquisition_error & Acquisition::ReadError) != 0) {
        ImGui::TextColored(red, "Unable to read raw socket, try running as root");
      } else if ((acquisition_error & Acquisition::WriteErrorFileExists) != 0) {
        ImGui::TextColored(red, "File exists");
      } else if ((acquisition_error & Acquisition::WriteErrorDirDontExists) != 0) {
        ImGui::TextColored(red, "Directory don't exists");
      } else if ((acquisition_error & Acquisition::WriteErrorOpenFile) != 0) {
        ImGui::TextColored(red, "Unable to create output file");
      }
    }
  }

  static void file_dialog(std::string& file_name)
  {
    // open Dialog Simple
    if (ImGui::Button("Browse...")) {
      ImGuiFileDialog::Instance()->OpenDialog("ChooseFileDlgKey", "Choose File", "", ".");
    }

    // display
    if (ImGuiFileDialog::Instance()->Display("ChooseFileDlgKey")) {
      // action if OK
      if (ImGuiFileDialog::Instance()->IsOk()) {
        file_name = ImGuiFileDialog::Instance()->GetFilePathName();
      }

      // close
      ImGuiFileDialog::Instance()->Close();
    }
  }

  void info()
  {
    if (!m_acquisition) {
      return;
    }

    const auto stats = m_acquisition->get_stats();

    gui_info("Total Events", stats.valid_events);
    ImGui::SameLine();
    gui_info("Events/s", stats.valid_event_rate);
    ImGui::SameLine();
    gui_info_colored("Invalid events", stats.invalid_event_ratio, 0, 10, "%");
    ImGui::SameLine();
    gui_info_colored("Decoder load", stats.decode_load, 0, 100, "%");

    gui_info_colored("Net speed", stats.read_speed, 0, 125, "MB/s");
    ImGui::SameLine();
    gui_info_colored("Net Buffer usage", stats.read_buffer_use, 0, 100, "%");
    ImGui::SameLine();
    gui_info_colored("Sniffer load", stats.read_load, 0, 100, "%");

    gui_info_colored("Write speed", stats.write_speed, 0, 80, "MB/s");
    ImGui::SameLine();
    gui_info_colored("File buffer usage", stats.write_buffer_use, 0, 100, "%");
    ImGui::SameLine();
    gui_info_colored("Write load", stats.write_load, 0, 100, "%");
  }

  void graphs()
  {
    if (!m_acquisition) {
      return;
    }

    if (ImGui::Button("Reset Graphs")) {
      m_graphs.reset();
    }

    static std::array<float, 1> row_ratios = {1};
    static std::array<float, 3> col_ratios = {1, 1, 1};
    if (ImPlot::BeginSubplots("", row_ratios.size(), col_ratios.size(), {-1, -1},
            ImPlotSubplotFlags_None, row_ratios.data(), col_ratios.data())) {

      static const int fit_flags = ImPlotAxisFlags_AutoFit | ImPlotAxisFlags_RangeFit;
      if (ImPlot::BeginPlot("Energy Spectrum")) {
        ImPlot::SetupAxes("ADC", "Events", fit_flags, fit_flags);
        auto& hist = m_graphs.get_energy_hist();
        ImPlot::PlotStairsG("", hist_data_getter, &hist, static_cast<int>(hist.size()));
        ImPlot::EndPlot();
      }

      if (ImPlot::BeginPlot("Channel Hits")) {
        ImPlot::SetupAxes("Channel", "Events", fit_flags, fit_flags);
        auto hist = m_graphs.get_channel_hist();
        ImPlot::PlotStairsG("", hist_data_getter, &hist.item, static_cast<int>(hist.item.size()));
        ImPlot::EndPlot();
      }

      if (ImPlot::BeginPlot("Waveform")) {
        ImPlot::SetupAxes("Time", "ADC", fit_flags);
        ImPlot::SetupAxesLimits(0, 1000, 0, 1024);
        const auto& waveform = m_graphs.get_waveform();
        const auto size = static_cast<int>(waveform.size());
        if (size != 0) {
          ImPlot::PlotLine("", waveform.data(), size);
        } else {
          static constexpr std::array<short, 2> placeholder {0, 0};
          ImPlot::PlotLine("", placeholder.data(), static_cast<int>(placeholder.size()));
        }
        ImPlot::EndPlot();
      }
      ImPlot::EndSubplots();
    }
  }

  private:
  Graphs m_graphs {};
  std::unique_ptr<Acquisition> m_acquisition;
};


int main(int argc, char* argv[])
{
  App app {};
  auto* implotContext = ImPlot::CreateContext();
  if(argc==2)
    HelloImGui::Run([&app,argv]() { app.run(argv[1]); }, "SampaSRS",true);
  else
    HelloImGui::Run([&app]() { app.run(); }, "SampaSRS",true);
  ImPlot::DestroyContext(implotContext);
  return 0;
}
