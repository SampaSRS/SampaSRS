# SampaSRS

## How to build

The code has only one external dependency [libpcap](https://www.tcpdump.org/index.html).

### Ubuntu dependencies

    sudo apt install libpcap-dev

### Windows dependencies

Windows build is not working yet.

You have two available options to download the precompiled binaries from:

- the newer and maintained [Npcap](https://npcap.com/dist/npcap-sdk-1.13.zip)
- the old and unmaintained [WinPcap](https://www.winpcap.org/install/bin/WpdPack_4_1_2.zip).

To build you also need to pass the location of the extracted binaries to CMake:

    cmake .. -DPCAP_ROOT_DIR=PathTo_libpcap_DevPack \<remaining flags\>

### Build

    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    cmake --build . --config Release

## Running on Linux

The executable `sampa_aquisition` needs special permissions to read raw network sockets. You can run it as root with `sudo` or you can give it the permission read raw sockets with:

    sudo setcap cap_net_raw=pe sampa_aquisition

and run it as a normal user.
