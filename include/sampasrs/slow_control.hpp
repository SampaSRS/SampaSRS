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

struct Command {
  std::string info {};
  explicit Command(const std::string& _info)
      : info(_info)
  {
  }

  virtual Requests make(const std::vector<uint32_t>& args) const = 0;

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
    std::string command_word {};
    line >> command_word;

    std::vector<uint32_t> args;
    std::copy(std::istream_iterator<uint32_t>(line), std::istream_iterator<uint32_t>(), std::back_inserter(args));

    // Try to find command word
    auto idx = m_command_list.find(command_word);
    if (idx == m_command_list.end()) {
      return false;
    }

    try {
      auto requests = idx->second->make(args);

      return std::all_of(requests.begin(), requests.end(), [&](const auto& request) {
        bool ok = send_check(request.port, request.payload);
        if (command_word == "reset_fec" || command_word == "reset_sampas") {
          // it seams we need to give some time for the fec to reset
          std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        }
        return ok;
      });
    } catch (const std::exception& e) {
      fmt::print(std::cerr, "{}: {}\n", command_word, e.what());
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
  const CommandList m_command_list;
};

namespace commands {

  uint32_t sampa_reg(unsigned char sampa_id, unsigned char reg)
  {
    constexpr unsigned char reg_size = 6;
    constexpr unsigned char sampa_id_size = 2;
    return 0xf4f00000 + ((bit_mask<uint32_t>(sampa_id_size) & sampa_id) << 6u) + (bit_mask<uint32_t>(reg_size) & reg);
  }

  struct FixCommand : Command {
    Request cmd;
    FixCommand(int port, Request::SubAddress subaddress, Request::Type type, uint32_t cmd_info, const std::vector<uint32_t>& data = {}, const std::string& info = "")
        : Command(info)
        , cmd(port, subaddress, type, cmd_info, data)
    {
    }

    Requests make(const std::vector<uint32_t>& /*args*/) const override { return {cmd}; }
  };

  struct TriggerUDP : Command {
    TriggerUDP()
        : Command("Constant trigger with at selected freq")
    {
    }

    Requests make(const std::vector<uint32_t>& args) const override
    {
      if (args.size() != 1) {
        throw std::invalid_argument("Expects 1 argument");
      }
      constexpr uint64_t cycles_per_second = static_cast<uint64_t>(6000000) * 33;
      constexpr uint64_t min_freq = std::numeric_limits<uint32_t>::max() / cycles_per_second;
      auto freq = args[0];

      if (freq < min_freq || freq > cycles_per_second) {
        throw std::domain_error("Invalid frequency range");
      }

      auto cycles = static_cast<uint32_t>(cycles_per_second / freq);
      return {{6041, Request::SubAddress::Zero, Request::Type::WritePairs, 0, std::vector<uint32_t> {0x00000000, 0x00000010, 0x00000002, cycles}}};
    }
  };

  struct WordLength : Command {
    WordLength()
        : Command("Set number for aquisition words")
    {
    }

    Requests make(const std::vector<uint32_t>& args) const override
    {
      if (args.size() != 1) {
        throw std::invalid_argument("Expects 1 argument");
      }

      auto low_byte = get_bit_range<uint32_t, 0, 8>(args[0]);
      auto high_byte = get_bit_range<uint32_t, 8, 16>(args[0]);

      const std::vector<uint32_t> data {
          0xf4f00007, low_byte,
          0xf4f00008, high_byte,
          0xf4f00047, low_byte,
          0xf4f00048, high_byte,
          0xf4f00087, low_byte,
          0xf4f00088, high_byte,
          0xf4f000c7, low_byte,
          0xf4f000c8, high_byte};

      return {{6024, Request::SubAddress::Full, Request::Type::WritePairs, 0, data}};
    }
  };

  struct ZeroSuppression : Command {
    explicit ZeroSuppression()
        : Command("Set the same zero suppression threshold for all channels")
    {
    }

    Requests make(const std::vector<uint32_t>& args) const override
    {
      if (args.size() != 1) {
        throw std::invalid_argument("Expects 1 argument");
      }

      // Zero suppression uses 2 bit resolution
      auto val = args[0] << 2u;
      auto low_byte = get_bit_range<uint32_t, 0, 8>(val);
      auto high_byte = get_bit_range<uint32_t, 8, 16>(val);

      const std::vector<uint32_t> data {0x09, low_byte, high_byte, 0x60};

      Requests out {};
      for (unsigned char sampa = 0; sampa < 4; ++sampa) {
        out.emplace_back(6024, Request::SubAddress::Full, Request::Type::WriteBurst, sampa_reg(sampa, 0x17), data);
      }
      return out;
    }
  };

  struct SampaBroadcast : Command {
    std::optional<uint32_t> has_reg {};

    explicit SampaBroadcast(std::optional<uint32_t> reg = {}, const std::string& info = {})
        : Command(info)
        , has_reg {reg}
    {
    }

    Requests make(const std::vector<uint32_t>& args) const override
    {
      size_t expected_args = has_reg ? 1 : 2;

      if (args.size() != expected_args) {
        throw std::invalid_argument(fmt::format("Expects {expected_args} arguments"));
      }

      uint32_t reg = has_reg.value_or(args[0]);
      uint32_t val = has_reg ? args[0] : args[1];

      const unsigned char sampa_count = 4;
      std::vector<uint32_t> data {};
      for (unsigned char i = 0; i < sampa_count; ++i) {
        data.push_back(sampa_reg(i, reg));
        data.push_back(val);
      }

      return {{6024, Request::SubAddress::Full, Request::Type::WritePairs, 0, data}};
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
  commands["start"]            = std::make_unique<FixCommand>(6600, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x0000000f, 0x00000001});
  commands["stop"]             = std::make_unique<FixCommand>(6600, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x0000000f, 0x00000000});
  commands["reset_fec"]        = std::make_unique<FixCommand>(6007, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0xffffffff, 0xffff0001});
  commands["reset_sampas"]     = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0xffffffff, 0x00000400});
  commands["trigger_1hz"]      = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x00000000, 0x00000002});
  commands["trigger_2.5khz"]   = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x0000000f, 0x00000800});
  commands["trigger_external"] = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x00000000, 0x00000008});
  commands["trigger_freq"]     = std::make_unique<TriggerUDP>();
  commands["word_length"]      = std::make_unique<WordLength>();
  commands["zero_suppression"] = std::make_unique<ZeroSuppression>();
  commands["set_all_sampas"]   = std::make_unique<SampaBroadcast>();
  commands["pretrigger"]       = std::make_unique<SampaBroadcast>(0x06u, "Number of pre-samples (Pre-trigger delay), max 192");
  commands["sampa_config"]     = std::make_unique<SampaBroadcast>(0x0du, "Various configuration settings");
  // clang-format on

  return commands;
}

} // namespace sampasrs