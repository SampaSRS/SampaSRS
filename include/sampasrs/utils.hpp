#pragma once

#include <boost/asio.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/endian/conversion.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace sampasrs {

template <typename T>
constexpr T bit_mask(const unsigned char size)
{
  return (T {1} << size) - 1;
}

template <typename T, unsigned char start, unsigned char end>
constexpr T get_bit_range(const T input)
{
  constexpr T mask = bit_mask<T>(static_cast<unsigned char>(end - start));
  return (input >> start) & mask;
}

template <typename T>
T read_from_buffer(const uint8_t* ptr)
{
  T output;
  std::memcpy(&output, ptr, sizeof(T));
  boost::endian::big_to_native_inplace(output);
  return output;
}

class Timer {
  public:
  using clock = std::chrono::steady_clock;

  explicit Timer(clock::duration interval)
      : m_last(clock::now())
      , m_interval(interval)
  {
  }

  explicit operator bool()
  {
    const auto now = clock::now();
    if (now > (m_last + m_interval)) {
      m_last = now;
      return true;
    }
    return false;
  }

  private:
  clock::time_point m_last;
  clock::duration m_interval;
};

template <typename Item>
struct Lock {
  private:
  std::lock_guard<std::mutex> lock;

  public:
  Item& item;

  Lock(Item& _item, std::mutex& mutex)
      : lock(mutex)
      , item(_item)
  {
  }
};

// Adapted from:
// https://www.boost.org/doc/libs/1_80_0/doc/html/boost_asio/example/cpp11/timeouts/blocking_udp_client.cpp
class PacketSender {
  using udp = boost::asio::ip::udp;

  public:
  PacketSender()
      : m_socket(m_io_context, udp::endpoint(udp::v4(), 6007))
  {
  }

  udp::endpoint resolve_endpoint(const std::string& address, int port)
  {
    udp::resolver resolver(m_io_context);
    return *resolver.resolve(udp::v4(), address, std::to_string(port)).begin();
  }

  void send(const std::string& address, int port,
      const std::vector<uint8_t>& payload)
  {
    send(resolve_endpoint(address, port), payload);
  }

  bool send_receive(const std::string& address, int port,
      const std::vector<uint8_t>& payload,
      std::vector<uint8_t>& response,
      std::chrono::steady_clock::duration timeout)
  {
    auto endpoint = resolve_endpoint(address, port);
    send(endpoint, payload);

    boost::system::error_code error;
    auto received_bytes = receive_from(endpoint, boost::asio::buffer(m_buffer), timeout, error);

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
  void send(const udp::endpoint& endpoint, const uint8_t* ptr, size_t size)
  {
    try {
      m_socket.send_to(boost::asio::buffer(ptr, size), endpoint);
    } catch (std::exception& e) {
      std::cerr << e.what() << "\n";
    }
  }

  void send(const udp::endpoint& endpoint,
      const std::vector<uint8_t>& payload)
  {
    send(endpoint, payload.data(), payload.size());
  }

  std::size_t receive_from(udp::endpoint& endpoint,
      const boost::asio::mutable_buffer& buffer,
      std::chrono::steady_clock::duration timeout,
      boost::system::error_code& error)
  {

    std::size_t length = 0;
    m_socket.async_receive_from(boost::asio::buffer(buffer), endpoint,
        [&error, &length](auto&& PH1, auto&& PH2) {
          return PacketSender::handle_receive(
              std::forward<decltype(PH1)>(PH1),
              std::forward<decltype(PH2)>(PH2), &error,
              &length);
        });

    run(timeout);

    return length;
  }
  void run(std::chrono::steady_clock::duration timeout)
  {
    m_io_context.restart();
    m_io_context.run_for(timeout);
    if (!m_io_context.stopped()) {
      // Cancel the outstanding asynchronous operation.
      m_socket.cancel();

      // Run the io_context again until the operation completes.
      m_io_context.run();
    }
  }

  static void handle_receive(const boost::system::error_code& error,
      std::size_t length,
      boost::system::error_code* out_error,
      std::size_t* out_length)
  {
    *out_error = error;
    *out_length = length;
  }

  boost::asio::io_context m_io_context {};
  udp::socket m_socket;
  std::array<uint8_t, 1024> m_buffer {};
};

} // namespace sampasrs
