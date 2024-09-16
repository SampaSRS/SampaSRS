- [SampaSRS](#sampasrs)
  - [How to build](#how-to-build)
    - [Ubuntu dependencies](#ubuntu-dependencies)
    - [Windows dependencies](#windows-dependencies)
    - [Build](#build)
    - [Cluster build](#cluster-build)
    - [Running on Linux](#running-on-linux)
  - [Details and User manual](#details-and-user-manual)
  - [Support](#support)
# SampaSRS

The SampaSRS is a software designed to control and operate the SAMPA ASIC using CERN's Scalable Readout System framework. The software also read, decode and process data produced by detectors.

## How to build

The code has the following build dependencies

- C++17 compiler
- Git
- CMake (3.14 or later)
- [ROOT](https://root.cern/) (6.26 or later)
- [libpcap](https://www.tcpdump.org/index.html)
- Latest compatible firmware version - 14

### Ubuntu dependencies

    sudo apt install build-essential git cmake libpcap-dev

### Windows dependencies

Download the precompiled binaries from [WinPcap](https://www.winpcap.org/install/bin/WpdPack_4_1_2.zip).

To build you also need to pass the location of the extracted binaries to CMake:

    cmake .. -DPCAP_ROOT_DIR=PathTo_libpcap_DevPack <remaining flags>

### Build

    mkdir build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=../install
    cmake --build . --target install

### Cluster build

It's possible to build only the reconstruction code to be able to run in a cluster environment, removing `libpcap` as a dependency. To accomplish that run CMake with the following flags:

    mkdir build && cd build
    cmake .. -DCMAKE_INSTALL_PREFIX=../install -DSAMPA_BUILD_ACQUISITION=OFF -DSAMPA_NATIVE_OPTIMIZATION=OFF
    cmake --build . --target install

The `SAMPA_BUILD_ACQUISITION` flag will disable all acquisition-related code, including the GUI. If the code needs to run on a different machine than the one it was compiled you probably should also disable the native optimizations, setting `SAMPA_NATIVE_OPTIMIZATION` to off. This will ensure the code is compatible with multiple processor architectures, with some performance cost.

### Running on Linux

>[!IMPORTANT]
>The executables `sampa_acquisition` and `sampa_gui` needs special permissions to read raw network sockets. You can run those as root with `sudo` or you can give them the permission to run as a normal user with:

    sudo setcap cap_net_raw=pe sampa_acquisition
    sudo setcap cap_net_raw=pe sampa_gui



## Details and User manual

The full details and the user manual can be found on the [wiki](https://github.com/SampaSRS/SampaSRS/wiki/) webpage.

## Support

If you have any questions, please contact the [SAMPA development team](hepic@if.usp.br) @HEPIC-IFUSP