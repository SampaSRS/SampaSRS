# SampaSRS

## How to build

The code has only two external dependencies

- [libpcap](https://www.tcpdump.org/index.html)
- [ROOT](https://root.cern/) (6.26 or later)

### Ubuntu dependencies

    sudo apt install libpcap-dev

### Windows dependencies

Donwload the precompiled binaries from [WinPcap](https://www.winpcap.org/install/bin/WpdPack_4_1_2.zip).

To build you also need to pass the location of the extracted binaries to CMake:

    cmake .. -DPCAP_ROOT_DIR=PathTo_libpcap_DevPack <remaining flags>

### Build

    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --config Release

## Running on Linux

The executable `sampa_aquisition` needs special permissions to read raw network sockets. You can run it as root with `sudo` or you can give it the permission read raw sockets with:

    sudo setcap cap_net_raw=pe sampa_aquisition

and run it as a normal user.
