#include <sampasrs/aquisition.h>

#include <string>

int main(int argc, const char *argv[]) {
  std::string file_prefix = "sampasrs";
  if (argc > 1) {
    file_prefix = argv[1];
  }

  std::string address{};
  if (argc > 2) {
    address = argv[2];
  }

  sampasrs::Aquisition sampa(file_prefix, address);
  sampa.run();
}
