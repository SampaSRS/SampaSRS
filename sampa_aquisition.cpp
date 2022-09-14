#include <sampasrs/aquisition.hpp>

#include <string>

int main(int argc, const char* argv[])
{
  std::string file_prefix = "sampasrs";
  if (argc > 1) {
    file_prefix = argv[1];
  }

  std::string address = "10.0.0.2";
  if (argc > 2) {
    address = argv[2];
  }

  sampasrs::Aquisition sampa(file_prefix, address);
  sampa.run();
}
