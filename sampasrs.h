#include "asio/io_context.hpp"
#include "asio/ip/udp.hpp"
#include <asio.hpp>
#include <cstdint>
#include <iostream>
#include <string>

class PacketSender {
  using udp = asio::ip::udp;

public:
  PacketSender(const std::string &address, int port)
      : m_socket(m_io_context, udp::v4()) {
    udp::resolver resolver(m_io_context);
    m_endpoint =
        *resolver.resolve(udp::v4(), address, std::to_string(port)).begin();
  }

  void send(const std::vector<uint8_t> &payload) {
    try {

      m_socket.send_to(asio::buffer(payload), m_endpoint);
    } catch (std::exception &e) {
      std::cerr << e.what() << "\n";
    }
  }

private:
  asio::io_context m_io_context{};
  udp::socket m_socket;
  udp::endpoint m_endpoint;
};
