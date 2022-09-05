#pragma once

#include <algorithm>
#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/endian/conversion.hpp>

#include <chrono>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace sampasrs {

// Adapted from:
// https://www.boost.org/doc/libs/1_80_0/doc/html/boost_asio/example/cpp11/timeouts/blocking_udp_client.cpp
class PacketSender {
  using udp = boost::asio::ip::udp;

public:
  PacketSender() : m_socket(m_io_context, udp::endpoint(udp::v4(), 6007)) {}

  udp::endpoint resolve_endpoint(const std::string &address, int port) {
    udp::resolver resolver(m_io_context);
    return *resolver.resolve(udp::v4(), address, std::to_string(port)).begin();
  }

  void send(const std::string &address, int port,
            const std::vector<uint8_t> &payload) {
    send(resolve_endpoint(address, port), payload);
  }

  bool send_receive(const std::string &address, int port,
                    const std::vector<uint8_t> &payload,
                    std::vector<uint8_t> &response,
                    std::chrono::steady_clock::duration timeout) {
    auto endpoint = resolve_endpoint(address, port);
    send(endpoint, payload);

    boost::system::error_code error;
    auto received_bytes =
        receive_from(endpoint, boost::asio::buffer(m_buffer), timeout, error);

    response.resize(received_bytes);
    std::copy(m_buffer.begin(), m_buffer.begin() + received_bytes,
              response.begin());

    if (error) {
      std::cout << "Receive error: " << error.message() << "\n";
      return false;
    }

    return true;
  }

private:
  void send(const udp::endpoint &endpoint, const uint8_t *ptr, size_t size) {
    try {
      m_socket.send_to(boost::asio::buffer(ptr, size), endpoint);
    } catch (std::exception &e) {
      std::cerr << e.what() << "\n";
    }
  }

  void send(const udp::endpoint &endpoint,
            const std::vector<uint8_t> &payload) {
    send(endpoint, payload.data(), payload.size());
  }

  std::size_t receive_from(udp::endpoint &endpoint,
                           const boost::asio::mutable_buffer &buffer,
                           std::chrono::steady_clock::duration timeout,
                           boost::system::error_code &error) {

    std::size_t length = 0;
    m_socket.async_receive_from(boost::asio::buffer(buffer), endpoint,
                                [&error, &length](auto &&PH1, auto &&PH2) {
                                  return PacketSender::handle_receive(
                                      std::forward<decltype(PH1)>(PH1),
                                      std::forward<decltype(PH2)>(PH2), &error,
                                      &length);
                                });

    run(timeout);

    return length;
  }
  void run(std::chrono::steady_clock::duration timeout) {
    m_io_context.restart();
    m_io_context.run_for(timeout);
    if (!m_io_context.stopped()) {
      // Cancel the outstanding asynchronous operation.
      m_socket.cancel();

      // Run the io_context again until the operation completes.
      m_io_context.run();
    }
  }

  static void handle_receive(const boost::system::error_code &error,
                             std::size_t length,
                             boost::system::error_code *out_error,
                             std::size_t *out_length) {
    *out_error = error;
    *out_length = length;
  }

  boost::asio::io_context m_io_context{};
  udp::socket m_socket;
  std::array<uint8_t, 1024> m_buffer{};
};

struct SampaCommand {
  int port{};
  std::vector<uint8_t> payload{};
  std::string info{};

  void set_value(uint32_t val) {
    boost::endian::native_to_big_inplace(val);
    std::memcpy(&payload[20], &val, sizeof(val));
  }

  void set_register(uint32_t reg) {
    boost::endian::native_to_big_inplace(reg);
    std::memcpy(&payload[16], &reg, sizeof(reg));
  }
};

using CommandList = std::unordered_map<std::string, SampaCommand>;
inline CommandList get_commands();

class SlowControl {
public:
  SlowControl() : m_command_list{get_commands()} {}

  bool send_command(const std::string &command, uint32_t value = PLACEHOLDER,
                    uint32_t register_adr = PLACEHOLDER) {
    auto idx = m_command_list.find(command);
    if (idx == m_command_list.end()) {
      return false;
    }

    auto cmd_data = idx->second;
    if (value != PLACEHOLDER) {
      cmd_data.set_value(value);
    }
    if (register_adr != PLACEHOLDER) {
      cmd_data.set_register(register_adr);
    }

    // m_sender.send(fec_address, cmd_data.port, cmd_data.payload);
    // return true;
    return send_check(cmd_data.port, cmd_data.payload);
  }

  bool send_command_line(const std::string &command_line) {
    std::string command{};
    uint32_t arg1 = PLACEHOLDER;
    uint32_t arg2 = PLACEHOLDER;

    std::istringstream line(command_line);
    line >> command >> arg1 >> arg2;

    uint32_t value = arg1;
    uint32_t register_adr = arg2;
    if (arg2 != PLACEHOLDER) {
      register_adr = arg1;
      value = arg2;
    }

    return send_command(command, value, register_adr);
  }

  bool send_check(int port, const std::vector<uint8_t> &payload) {
    m_sender.send_receive(fec_address, port, payload, m_response_payload,
                          std::chrono::milliseconds(receive_timeout));

    std::cout << "Sent ";
    for (auto byte : payload) {
      std::cout << std::setfill('0') << std::setw(2) << std::hex
                << (unsigned int)byte << " ";
    }
    std::cout << "\n";

    std::cout << "Resp ";
    for (auto byte : m_response_payload) {
      std::cout << std::setfill('0') << std::setw(2) << std::hex
                << (unsigned int)byte << " ";
    }
    std::cout << "\n";

    // check echo response
    if (payload.size() != m_response_payload.size()) {
      return false;
    }
    bool equal_header = std::equal(payload.begin() + 1, payload.begin() + 11,
                                   m_response_payload.begin() + 1);

    const int offset = 20;
    bool equal_value = std::equal(payload.begin() + offset, payload.end(),
                                  m_response_payload.begin() + offset);

    return equal_header && equal_value;
  }

  std::string fec_address = "10.0.0.2";
  int receive_timeout = 1000; // in milliseconds

private:
  static constexpr uint32_t PLACEHOLDER = 0xfacafaca;
  std::vector<uint8_t> m_response_payload{};
  PacketSender m_sender{};
  const CommandList m_command_list;
};

inline CommandList get_commands() {
  CommandList commands{};
  commands["start"] = {6600, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0xaa, 0xaa, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x01}};

  commands["stop"] = {6600, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                             0xaa, 0xaa, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x00}};

  commands["reset_fec"] = {6007,
                           {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                            0xaa, 0xaa, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01}};

  commands["reset_sampas"] = {6041,
                              {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0xaa, 0xaa, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00}};

  commands["trigger_udp"] = {6041,
                             {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0xaa, 0xaa, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10}};

  commands["trigger_1hz"] = {6041,
                             {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                              0xaa, 0xaa, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                              0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02}};

  commands["trigger_2.5hz"] = {6041, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x00, 0xaa, 0xaa, 0xff, 0xff,
                                      0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                      0x00, 0x0f, 0x00, 0x00, 0x08, 0x00}};

  commands["trigger_33hz"] = {6041,
                              {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0xaa, 0xaa, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x02, 0x00, 0x5b, 0x8d, 0x80}};

  commands["trigger_66hz"] = {6041,
                              {0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                               0xaa, 0xaa, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
                               0x00, 0x00, 0x00, 0x02, 0x00, 0x2d, 0xc6, 0xc0}};

  commands["trigger_cicles"] = {6041, {0x80, 0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x00, 0xaa, 0xaa, 0xff, 0xff,
                                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                       0x00, 0x02, 0x00, 0x00, 0x00, 0x00}};

  commands["twlenh_sampa1"] = {6024, {0x80, 0x00, 0x00, 0x00, 0xff, 0xff,
                                      0xff, 0xff, 0xaa, 0xaa, 0xff, 0xff,
                                      0x00, 0x00, 0x00, 0x00, 0xf4, 0xf0,
                                      0x00, 0x08, 0x00, 0x00, 0x00, 0x00}};

  commands["twlenh_sampa2"] = {6024, {0x80, 0x00, 0x00, 0x00, 0xff, 0xff,
                                      0xff, 0xff, 0xaa, 0xaa, 0xff, 0xff,
                                      0x00, 0x00, 0x00, 0x00, 0xf4, 0xf0,
                                      0x00, 0x48, 0x00, 0x00, 0x00, 0x00}};

  commands["twlenh_sampa3"] = {6024, {0x80, 0x00, 0x00, 0x00, 0xff, 0xff,
                                      0xff, 0xff, 0xaa, 0xaa, 0xff, 0xff,
                                      0x00, 0x00, 0x00, 0x00, 0xf4, 0xf0,
                                      0x00, 0x88, 0x00, 0x00, 0x00, 0x00}};

  commands["twlenh_sampa4"] = {6024, {0x80, 0x00, 0x00, 0x00, 0xff, 0xff,
                                      0xff, 0xff, 0xaa, 0xaa, 0xff, 0xff,
                                      0x00, 0x00, 0x00, 0x00, 0xf4, 0xf0,
                                      0x00, 0xc8, 0x00, 0x00, 0x00, 0x00}};

  commands["twlenl_sampa1"] = {6024, {0x80, 0x00, 0x00, 0x00, 0xff, 0xff,
                                      0xff, 0xff, 0xaa, 0xaa, 0xff, 0xff,
                                      0x00, 0x00, 0x00, 0x00, 0xf4, 0xf0,
                                      0x00, 0x07, 0x00, 0x00, 0x00, 0x00}};

  commands["twlenl_sampa2"] = {6024, {0x80, 0x00, 0x00, 0x00, 0xff, 0xff,
                                      0xff, 0xff, 0xaa, 0xaa, 0xff, 0xff,
                                      0x00, 0x00, 0x00, 0x00, 0xf4, 0xf0,
                                      0x00, 0x47, 0x00, 0x00, 0x00, 0x00}};

  commands["twlenl_sampa3"] = {6024, {0x80, 0x00, 0x00, 0x00, 0xff, 0xff,
                                      0xff, 0xff, 0xaa, 0xaa, 0xff, 0xff,
                                      0x00, 0x00, 0x00, 0x00, 0xf4, 0xf0,
                                      0x00, 0x87, 0x00, 0x00, 0x00, 0x00}};

  commands["twlenl_sampa4"] = {6024, {0x80, 0x00, 0x00, 0x00, 0xff, 0xff,
                                      0xff, 0xff, 0xaa, 0xaa, 0xff, 0xff,
                                      0x00, 0x00, 0x00, 0x00, 0xf4, 0xf0,
                                      0x00, 0xc7, 0x00, 0x00, 0x00, 0x00}};

  return commands;
}

} // namespace sampasrs