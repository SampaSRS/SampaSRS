# SampaSRS

## How to build

The code has the following build dependencies

- C++17 compiler
- Git
- CMake (3.14 or later)
- [ROOT](https://root.cern/) (6.26 or later)
- [libpcap](https://www.tcpdump.org/index.html)
- SDL2 (for sampa_gui)

### Ubuntu dependencies

    sudo apt install build-essential git cmake libpcap-dev libsdl2-dev

### Windows dependencies

Download the precompiled binaries from [WinPcap](https://www.winpcap.org/install/bin/WpdPack_4_1_2.zip).

To build you also need to pass the location of the extracted binaries to CMake:

    cmake .. -DPCAP_ROOT_DIR=PathTo_libpcap_DevPack <remaining flags>

### Build

We need to create a dictionary to write and access the ttree

    mkdir build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=../install .
    cmake --build . --target install

## Running on Linux

The executable `sampa_acquisition` and `sampa_gui` needs special permissions to read raw network sockets. You can run it as root with `sudo` or you can give it the permission to run as a normal user with:

    sudo setcap cap_net_raw=pe sampa_acquisition
    sudo setcap cap_net_raw=pe sampa_gui
