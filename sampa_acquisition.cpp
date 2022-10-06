#include <sampasrs/acquisition.hpp>

#include <string>

int main(int argc, const char* argv[])
{
  std::string file_prefix = "sampasrs";
  if (argc > 1) {
    file_prefix = argv[1];
  }

  std::string address = "10.0.0.2"; // FEC address, used to find which network interface to sniff
  if (argc > 2) {
    address = argv[2];
  }

  sampasrs::Acquisition sampa(file_prefix, true, address);
  sampa.run();
}
