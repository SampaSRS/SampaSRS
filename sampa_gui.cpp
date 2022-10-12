#include "sampasrs/acquisition.hpp"

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
      for (size_t word = 0; word < event.word_count(waveform); ++word) {
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
  void run()
  {
    start_acquisition();
    info();
    graphs();
  }

  void start_acquisition()
  {
    static bool save_raw = true;
    static std::string file_prefix = "sampasrs";
    static std::string fec_address = "10.0.0.2";
    static constexpr bool process_events = true;
    static bool prefix_not_available = false;

    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4 {0.5, 0.5, 0.5, 1});
    if (m_acquisition) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {1, 0, 0, 1});
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {1, 0, 0, 1});
      if (ImGui::Button("Stop", ImVec2 {80, 50})) {
        m_acquisition.reset();
        m_graphs.reset();
      }
      ImGui::SameLine();
      if (save_raw) {
        ImGui::Text("Writing to: %s-*.raw", file_prefix.data());
      } else {
        ImGui::Text("Writing to: %s-*.rawev", file_prefix.data());
      }

    } else {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4 {0, 0.5, 0, 1});
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4 {0, 0.5, 0, 1});
      if (ImGui::Button("Start", ImVec2 {80, 50})) {
        try {
          m_acquisition = std::make_unique<Acquisition>(
              file_prefix,
              save_raw,
              [&](Event&& event) { m_graphs.event_handle(std::move(event)); },
              fec_address);

          prefix_not_available = false;
        } catch (const std::runtime_error& e) {
          prefix_not_available = true;
        }
      }

      ImGui::SameLine();
      ImGui::InputText("File prefix", &file_prefix);
      ImGui::SameLine();
      ImGui::Checkbox("Save raw data", &save_raw);

      if (prefix_not_available) {
        ImGui::TextColored(ImVec4 {1, 0, 0, 1}, "File prefix already in use");
      }
    }
    ImGui::PopStyleColor(3);
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

    static float rratios[] = {1};
    static float cratios[] = {1, 1, 1};
    if (ImPlot::BeginSubplots("", 1, 3, ImVec2 {-1, -1},
            ImPlotSubplotFlags_None, rratios, cratios)) {

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

int main(int /*unused*/, char* /*unused*/[])
{
  App app {};
  auto* implotContext = ImPlot::CreateContext();
  HelloImGui::Run([&app]() { app.run(); }, {1000.f, 600.f}, "SampaSRS");
  ImPlot::DestroyContext(implotContext);
  return 0;
}