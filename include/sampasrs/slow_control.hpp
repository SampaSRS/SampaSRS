#pragma once

#include "sampasrs/utils.hpp"

#include <boost/endian/conversion.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
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

struct Command {
  std::string info {};
  explicit Command(const std::string& _info)
      : info(_info)
  {
  }

  virtual Request make(const std::vector<uint32_t>& args) const = 0;

  // Special Member functions
  virtual ~Command() = default;
  Command(const Command&) = delete;
  Command& operator=(const Command& x) = delete;
  Command(Command&& other) = delete;
  Command& operator=(Command&& other) = delete;
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

    auto command = idx->second->make(args);
    // m_sender.send(fec_address, command.port, command.payload);
    // return true;
    return send_check(command.port, command.payload);
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
    if (payload.size() != m_response_payload.size()) {
      return false;
    }
    bool equal_header = std::equal(payload.begin() + 1, payload.begin() + 11,
        m_response_payload.begin() + 1);

    const int offset = 20;
    bool equal_value = std::equal(payload.begin() + offset, payload.begin() + offset + 4,
        m_response_payload.begin() + offset);

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

  struct FixCommand : Command {
    Request cmd;
    FixCommand(int port, Request::SubAddress subaddress, Request::Type type, uint32_t cmd_info, const std::vector<uint32_t>& data = {}, const std::string& info = "")
        : Command(info)
        , cmd(port, subaddress, type, cmd_info, data)
    {
    }

    Request make(const std::vector<uint32_t>& /*args*/) const override { return cmd; }
  };

  struct TriggerUDP : Command {
    TriggerUDP()
        : Command("Constant trigger with at selected freq")
    {
    }

    Request make(const std::vector<uint32_t>& args) const override
    {
      if (args.size() != 1) {
        throw std::invalid_argument("Expects 1 argument");
      }
      constexpr uint64_t cycles_per_second = static_cast<uint64_t>(6000000) * 33;
      constexpr uint64_t min_freq = std::numeric_limits<uint32_t>::max() / cycles_per_second;
      auto freq = args[1];

      if (freq < min_freq || freq > cycles_per_second) {
        throw std::domain_error("Invalid frequency range");
      }

      auto cycles = static_cast<uint32_t>(cycles_per_second / freq);
      return {6041, Request::SubAddress::Zero, Request::Type::WritePairs, 0, std::vector<uint32_t> {0x00000000, 0x00000010, 0x00000002, cycles}};
    }
  };

  struct WordLength : Command {
    WordLength()
        : Command("Set number for aquisition words")
    {
    }

    Request make(const std::vector<uint32_t>& args) const override
    {
      if (args.size() != 1) {
        throw std::invalid_argument("Expects 1 argument");
      }

      auto low_byte = get_bit_range<uint32_t, 0, 8>(args[0]);
      auto high_byte = get_bit_range<uint32_t, 8, 16>(args[0]);

      const std::vector<uint32_t> data {
          0xf4f00007,
          low_byte,
          0xf4f00008,
          high_byte,
          0xf4f00047,
          low_byte,
          0xf4f00048,
          high_byte,
          0xf4f00087,
          low_byte,
          0xf4f00088,
          high_byte,
          0xf4f000c7,
          low_byte,
          0xf4f000c8,
          high_byte,
      };

      return {6024, Request::SubAddress::Full, Request::Type::WritePairs, 0, data};
    }
  };

} // namespace commands

inline CommandList get_commands()
{
  using namespace commands;

  CommandList commands {};
  using Type = Request::Type;
  using SubAddress = Request::SubAddress;

  // clang-format off
  commands["start"]          = std::make_unique<FixCommand>(6600, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x0000000f, 0x00000001});
  commands["stop"]           = std::make_unique<FixCommand>(6600, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x0000000f, 0x00000000});
  commands["reset_fec"]      = std::make_unique<FixCommand>(6007, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0xffffffff, 0xffff0001});
  commands["reset_sampas"]   = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0xffffffff, 0x00000400});
  commands["trigger_1hz"]    = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x00000000, 0x00000002});
  commands["trigger_2.5khz"] = std::make_unique<FixCommand>(6041, SubAddress::Zero, Type::WritePairs, 0, std::vector<uint32_t> {0x0000000f, 0x00000800});
  commands["trigger_freq"]   = std::make_unique<TriggerUDP>();
  commands["word_length"]    = std::make_unique<WordLength>();
  // clang-format on

  return commands;
}

} // namespace sampasrs