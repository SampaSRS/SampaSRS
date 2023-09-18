#pragma once

#include "sampasrs/utils.hpp"

#include <boost/endian/conversion.hpp>
#include <fmt/core.h>
#include <fmt/ostream.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sampasrs {

struct Request {
  int port {};
  std::vector<uint8_t> payload {};

  static constexpr uint32_t request_id = 0x80000000;

  enum SubAddress : uint32_t {
    Zero = 0x00000000,
    Full = 0xffffffff
  };

  enum Type : uint32_t {
    WritePairs = 0xaaaaffff,
    WriteBurst = 0xaabbffff,
    ReadBurst = 0xbbbbffff,
    ReadList = 0xbbaaffff
  };

  Request() = default;
  Request(int _port, SubAddress subaddress, Type type, uint32_t cmd_info, const std::vector<uint32_t>& data = {})
      : port(_port)
  {
    push_back(request_id);
    push_back(static_cast<uint32_t>(subaddress));
    push_back(static_cast<uint32_t>(type));
    push_back(cmd_info);
    for (auto x : data) {
      push_back(x);
    }
  }

  void push_back(uint32_t x)
  {
    const auto previous_size = payload.size();
    payload.resize(previous_size + sizeof(x));
    boost::endian::native_to_big_inplace(x);
    std::memcpy(&payload[previous_size], &x, sizeof(x));
  }
};

using Requests = std::vector<Request>;

  // Helper function to parse space separeted data from a string
  // \param input string
  // \param args output variables
  template <typename... T>
  bool parse_args(const std::string& input, T&&... args) {
      std::istringstream parser(input);
      (parser >> ... >> args);
      bool ok = !parser.fail();
      
      // check for leftovers
      std::string leftovers {};
      parser >> leftovers;
      std::cout << leftovers << "#\n";

      return ok && leftovers.empty();
  }

struct Command {
  std::string info {};
  explicit Command(const std::string& _info)
      : info(_info)
  {
  }

  virtual Requests parse(const std::string& args) const = 0;

  // Special Member functions
  virtual ~Command() = default;
  Command(const Command&) = delete;
  Command& operator=(const Command&) = delete;
  Command(Command&&) = delete;
  Command& operator=(Command&&) = delete;
};

using CommandList = std::unordered_map<std::string, std::unique_ptr<Command>>;
inline CommandList get_commands();

class SlowControl {
  public:
  SlowControl()
      : m_command_list {get_commands()}
  {
  }

  bool send_command(const std::string& command_line)
  {
    if (command_line.empty()) {
      return true;
    }

    if (command_line[0] == '#') {
      return true;
    }

    std::istringstream line(command_line);
    std::string command_name {};
    line >> command_name;

    const auto args = line.str();

    // Try to find command word
    auto idx = m_command_list.find(command_name);
    if (idx == m_command_list.end()) {
      return false;
    }

    try {
      auto requests = idx->second->parse(args);

      return std::all_of(requests.begin(), requests.end(), [&](const auto& request) {
        bool ok = send_check(request.port, request.payload);
        if (command_name == "reset_fec" || command_name == "reset_sampas") {
          // it seams we need to give some time for the fec to reset
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        return ok;
      });
    } catch (const std::exception& e) {
      fmt::print(std::cerr, "{}: {}\n", command_name, e.what());
      return false;
    }
  }

  bool send_check(int port, const std::vector<uint8_t>& payload)
  {
    m_sender.send_receive(fec_address, port, payload, m_response_payload,
        std::chrono::milliseconds(receive_timeout));

    // Debug output

    // std::cout << "Sent ";
    // for (auto byte : payload) {
    //   std::cout << std::setfill('0') << std::setw(2) << std::hex
    //             << (unsigned int)byte << " ";
    // }
    // std::cout << "\n";

    // std::cout << "Resp ";
    // for (auto byte : m_response_payload) {
    //   std::cout << std::setfill('0') << std::setw(2) << std::hex
    //             << (unsigned int)byte << " ";
    // }
    // std::cout << "\n";

    // check echo response
    if (m_response_payload.size() < 12) {
      return false;
    }

    bool equal_header = std::equal(payload.begin() + 1, payload.begin() + 12,
        m_response_payload.begin() + 1);

    // const int offset = 20;
    // bool equal_value = std::equal(payload.begin() + offset, payload.begin() + offset + 4,
    //     m_response_payload.begin() + offset);
    bool equal_value = true;

    std::this_thread::sleep_for(std::chrono::milliseconds(send_delay));
    return equal_header && equal_value;
  }

  std::string fec_address = "10.0.0.2";
  int receive_timeout = 1000; // in milliseconds
  int send_delay = 100;       // in milliseconds

  private:
  std::vector<uint8_t> m_response_payload {};
  PacketSender m_sender {};
  CommandList m_command_list;
};

namespace commands {
  enum SampaRegister : unsigned char {
    HWADD = 0x00,
    TRCNTL = 0x01,
    TRCNTH = 0x02,
    BXCNTLL = 0x03,
    BXCNTLH = 0x04,
    BXCNTHL = 0x05,
    PRETRG = 0x06,
    TWLENL = 0x07,
    TWLENH = 0x08,
    ACQSTARTL = 0x09,
    ACQSTARTH = 0x0A,
    ACQENDL = 0x0B,
    ACQENDH = 0x0C,
    VACFG = 0x0D,
    CMD = 0x0E,
    NBCFG = 0x0F,
    ADCDEL = 0x10,
    ADCTRIM = 0x11,
    SOCFG = 0x12,
    SODRVST = 0x13,
    ERRORS = 0x14,
    PMADDL = 0x15,
    PMADDH = 0x16,
    CHRGADD = 0x17,
    CHRGWDATL = 0x18,
    CHRGWDATH = 0x19,
    CHRGCTL = 0x1A,
    CHRGRDATL = 0x1B,
    CHRGRDATH = 0x1C,
    CHORDAT = 0x1D,
    CHORDCTL = 0x1E,
    BYPASS = 0x1F,
    SERCHSEL = 0x20,
    RINGCNT = 0x21,
    CLKCONF = 0x22,
    BOUNDARY = 0x23,
    CHEN0 = 0x24,
    CHEN1 = 0x25,
    CHEN2 = 0x26,
    CHEN3 = 0x27
  };

  enum ChannelRegister : unsigned char {
    K1 = 0x00,
    K2 = 0x01,
    K3 = 0x02,
    K4 = 0x03,
    L1 = 0x04,
    L2 = 0x05,
    L3 = 0x06,
    L4 = 0x07,
    L30 = 0x08,
    ZSTHR = 0x09,
    ZSOFF = 0x0A,
    ZSCFG = 0x0B,
    FPD = 0x0C,
    VPD = 0x0D,
    BC2BSL = 0x0E,
    BC3BSL = 0x0F,
    PMDATA = 0x10,
    BC2LTHRREL = 0x11,
    BC2HTHRREL = 0x12,
    BC2LTHRBSL = 0x13,
    BC2HTHRBSL = 0x14,
    BC2CFG = 0x15,
    BC2RSTVAL = 0x16,
    BC2RSTCNT = 0x17,
    DPCFG = 0x18,
    BC1THRL = 0x19,
    BC1THRH = 0x1A,
    BC1CFG = 0x1B,
    BC1RSTCNT = 0x1C,
    BC3SLD = 0x1D,
    BC3SLU = 0x1E
  };

  std::pair<uint32_t, uint32_t> low_high_bytes(uint32_t x)
  {
    return {get_bit_range<uint32_t, 0, 8>(x), get_bit_range<uint32_t, 8, 16>(x)};
  }

  struct FixCommand : Command {
    Request cmd;
    FixCommand(int port, Request::SubAddress subaddress, Request::Type type, uint32_t cmd_info, const std::vector<uint32_t>& data = {}, const std::string& info = "")
        : Command(info)
        , cmd(port, subaddress, type, cmd_info, data)
    {
    }

    Requests parse(const std::string& /*args*/) const override { return {cmd}; }
  };

  struct TriggerUDP : Command {
    TriggerUDP()
        : Command("Constant trigger with at selected freq")
    {
    }

    Requests parse(const std::string& args) const override
    {
      uint32_t freq {};
      if (!parse_args(args, freq)) {
        throw std::invalid_argument("Expects 1 argument");
      }
      constexpr uint64_t cycles_per_second = static_cast<uint64_t>(6000000) * 33;
      constexpr uint64_t min_freq = std::numeric_limits<uint32_t>::max() / cycles_per_second;

      if (freq < min_freq || freq > cycles_per_second) {
        throw std::domain_error("Invalid frequency range");
      }

      auto cycles = static_cast<uint32_t>(cycles_per_second / freq);
      return {{6041, Request::SubAddress::Zero, Request::Type::WritePairs, 0, std::vector<uint32_t> {0x00000000, 0x00000010, 0x00000002, cycles}}};
    }
  };

  inline uint32_t sampa_reg(unsigned char sampa, unsigned char reg)
  {
    return 0xf4f00000 + (sampa << 6u) + get_bit_range<uint32_t, 0, 6>(reg);
  }

  static constexpr int sampa_port = 6024;
  static constexpr unsigned char sampa_count = 4;

  inline Request sampa_request(Request::Type type, uint32_t cmd_info, const std::vector<uint32_t>& data)
  {
    return {sampa_port, Request::SubAddress::Full, type, cmd_info, data};
  }

  // sampa = -1 : broadcast to all sampas
  inline Requests sampa_write_burst(char sampa, unsigned char reg, const std::vector<uint32_t>& data)
  {
    if (sampa > 0) {
      return {sampa_request(Request::Type::WriteBurst, sampa_reg(sampa, reg), data)};
    }
    // broadcast to all sampas
    Requests requests;
    for (unsigned char i = 0; i < sampa_count; ++i) {
      requests.push_back(sampa_request(Request::Type::WriteBurst, sampa_reg(i, reg), data));
    }
    return requests;
  }

  // sampa = -1 : broadcast to all sampas
  inline Request sampa_write_pairs(char sampa, const std::vector<std::pair<unsigned char, uint32_t>>& reg_val)
  {
    std::vector<uint32_t> data;
    for (auto [reg, val] : reg_val) {
      if (sampa > 0) {
        data.push_back(sampa_reg(sampa, reg));
        data.push_back(val);
      } else {
        for (unsigned char i = 0; i < sampa_count; ++i) {
          data.push_back(sampa_reg(i, reg));
          data.push_back(val);
        }
      }
    }
    return sampa_request(Request::Type::WritePairs, 0, data);
  }

  // sampa = -1 : broadcast to all sampas
  // channel = -1 : broadcast to all channels
  // reg = -1 to skip channel register assignment, use the previous one
  inline Requests channel_write(char sampa, char channel, char reg, uint32_t value, bool increment_pedestal_addr = false)
  {
    auto [low_byte, high_byte] = low_high_bytes(value);

    uint32_t channel_control = 1U << 6U; // Enable write
    if (channel > 0) {
      channel_control |= get_bit_range<uint32_t, 0, 5>(channel); // set channel number
    } else {
      channel_control |= 1U << 5U; // broadcast to all channels
    }

    if (increment_pedestal_addr) {
      channel_control |= 1U << 7U;
    }

    if (reg > 0) {
      return sampa_write_burst(sampa, SampaRegister::CHRGADD, {static_cast<uint32_t>(reg), low_byte, high_byte, channel_control});
    }
    return sampa_write_burst(sampa, SampaRegister::CHRGWDATL, {low_byte, high_byte, channel_control});
  }

  struct SampaBroadcastPairs : Command {
    std::optional<uint32_t> has_reg {};

    explicit SampaBroadcastPairs(std::optional<uint32_t> reg = {}, const std::string& info = {})
        : Command(info)
        , has_reg {reg}
    {
    }

    Requests parse(const std::string& args) const override
    {
      uint32_t reg {};
      uint32_t val {};

      if (has_reg) {
        if (!parse_args(args, val)) {
          throw std::invalid_argument("Expects 1 argument");
        }
        reg = has_reg.value();
      } else {
        if (!parse_args(args, reg, val)) {
          throw std::invalid_argument("Expects 2 arguments");
        }
      }
      
      std::vector<std::pair<unsigned char, uint32_t>> data {};
      for (unsigned char i = 0; i < sampa_count; ++i) {
        data.emplace_back(reg, val);
      }

      return {sampa_write_pairs(-1, data)};
    }
  };

  struct SampaBroadcastBurst : Command {
    std::optional<uint32_t> has_reg {};

    explicit SampaBroadcastBurst(std::optional<uint32_t> reg = {}, const std::string& info = {})
        : Command(info)
        , has_reg {reg}
    {
    }

    Requests parse(const std::string& args) const override
    {
      size_t min_args = has_reg ? 1 : 2;

      std::vector<uint32_t> values;
      std::istringstream parser(args);
      std::copy(std::istream_iterator<uint32_t>(parser), std::istream_iterator<uint32_t>(), std::back_inserter(values));

      if (values.size() != min_args) {
        throw std::invalid_argument(fmt::format("Expects at least {} arguments", min_args));
      }

      unsigned char reg = has_reg ? has_reg.value() : args[0];
      std::vector<uint32_t> data;
      std::copy(args.begin() + (has_reg ? 0 : 1), args.end(), std::back_inserter(data));

      return sampa_write_burst(-1, reg, data);
    }
  };

  struct WordLength : Command {
    WordLength()
        : Command("Set number for acquisition words")
    {
    }

    Requests parse(const std::string& args) const override
    {
      uint32_t length {};
      if (!parse_args(args, length)) {
        throw std::invalid_argument("Expected arguments: 1 integer");
      }

      auto [low_byte, high_byte] = low_high_bytes(args[0]);
      return {sampa_write_burst(-1, SampaRegister::TWLENL, {low_byte, high_byte})};
    }
  };

  struct ZeroSuppressionBroadcast : Command {
    explicit ZeroSuppressionBroadcast()
        : Command("Broadcast the same zero suppression threshold for all channels")
    {
    }
    Requests parse(const std::string& args) const override
    {
      uint32_t val {};
      if (!parse_args(args, val)) {
        throw std::invalid_argument("Expects 1 argument");
      }

      // Zero suppression uses 2 bit resolution
      val <<= 2u;

      return {channel_write(-1, -1, ChannelRegister::ZSTHR, val)};
    }
  };

  struct ZeroSuppression : Command {
    explicit ZeroSuppression()
        : Command("Set the zero suppression threshold for a single channel")
    {
    }

    Requests parse(const std::string& args) const override
    {
      char sampa {};
      char channel {};
      uint32_t val {};
      if (!parse_args(args, sampa, channel, val)) {
        throw std::invalid_argument("Expects 3 arguments");
      }

      // Zero suppression uses 2 bit resolution
      val <<= 2u;

      return {channel_write(sampa, channel, ChannelRegister::ZSTHR, val)};
    }
  };

  struct ZeroSuppressionFile : Command {
    explicit ZeroSuppressionFile()
        : Command("Set the zero suppression threshold for each channels from a file")
    {
    }

    Requests parse(const std::string& filename) const override
    {
      // File: sampa channel value
      std:std::ifstream file(filename);
      if (!file) {
        throw std::invalid_argument(fmt::format("Unable to open zero suppression file: {}", filename));
      }
      std::string line;
      Requests requests {};

      while (std::getline(file, line)) {
        if (line.empty()) { continue; }
        if (line[0] == '#') { continue; }
        std::istringstream parser(line);
        
        char sampa {};
        char channel {};
        uint32_t value {};
        
        if (!(parser >> sampa >> channel >> value)) {
          throw std::invalid_argument(fmt::format("Unable to parse zero suppression file: {}", filename));
        }
        
        // Zero suppression uses 2 bit resolution
        value <<= 2u;
        
        auto tmp = channel_write(sampa, channel, ChannelRegister::ZSTHR, value);
        requests.insert(requests.end(), tmp.begin(), tmp.end());
      }

      return requests;
    }
  };

  struct PedestalCliff : Command {
    explicit PedestalCliff()
        : Command("Fake waveform with increasing value starting from zero")
    {
    }

    Requests parse(const std::string& args) const override
    {
      if (!args.empty()) {
        throw std::invalid_argument("Expects no argument");
      }

      Requests requests;
      const auto enable_write = sampa_write_burst(-1, SampaRegister::PMADDL, {0, 0, ChannelRegister::PMDATA});
      std::copy(enable_write.begin(), enable_write.end(), std::back_inserter(requests));

      for (uint32_t x = 0; x < 1024; ++x) {
        const auto pedestal_value = channel_write(-1, -1, -1, x, true);
        std::copy(pedestal_value.begin(), pedestal_value.end(), std::back_inserter(requests));
      }

      const auto pedestal_mode = channel_write(-1, -1, ChannelRegister::DPCFG, 0x0a);
      std::copy(pedestal_mode.begin(), pedestal_mode.end(), std::back_inserter(requests));

      return requests;
    }
  };

} // namespace commands

inline CommandList
get_commands()
{
  using namespace commands;
  using Type = Request::Type;
  using SubAddress = Request::SubAddress;

  CommandList commands {};

  // clang-format off
  commands["start"]                = std::make_unique<FixCommand>(6600, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x0000000f, 0x00000001});
  commands["stop"]                 = std::make_unique<FixCommand>(6600, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x0000000f, 0x00000000});
  commands["reset_fec"]            = std::make_unique<FixCommand>(6007, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0xffffffff, 0xffff0001});
  commands["reset_sampas"]         = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0xffffffff, 0x00000400});
  commands["trigger_1hz"]          = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x00000000, 0x00000002});
  commands["trigger_2.5khz"]       = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x0000000f, 0x00000800});
  commands["trigger_external"]     = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x00000000, 0x00000008});
  commands["trigger_freq"]         = std::make_unique<TriggerUDP>();
  commands["word_length"]          = std::make_unique<WordLength>();
  commands["zerosup"]              = std::make_unique<ZeroSuppression>();
  commands["zerosup_broadcast"]    = std::make_unique<ZeroSuppressionBroadcast>();
  commands["zerosup_file"]         = std::make_unique<ZeroSuppressionFile>();
  commands["set_all_sampas"]       = std::make_unique<SampaBroadcastPairs>();
  commands["pretrigger"]           = std::make_unique<SampaBroadcastPairs>(SampaRegister::PRETRG, "Number of pre-samples (Pre-trigger delay), max 192");
  commands["sampa_config"]         = std::make_unique<SampaBroadcastPairs>(SampaRegister::VACFG, "Various configuration settings");
  commands["pedestal_cliff"]       = std::make_unique<PedestalCliff>();
  // clang-format on

  return commands;
}

} // namespace sampasrs