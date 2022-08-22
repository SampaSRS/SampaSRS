cmake_minimum_required(VERSION 3.14)

project(sampasrs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Install dependencies
if(WIN32)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Windows build")
    set(LIBTINS_BUILD_SHARED OFF CACHE BOOL "Windows build")
    add_compile_definitions(TINS_STATIC NOMINMAX)
endif()

set(LIBTINS_ENABLE_ACK_TRACKER OFF CACHE BOOL "No boost")
set(LIBTINS_ENABLE_TCP_STREAM_CUSTOM_DATA OFF CACHE BOOL "No boost")
set(LIBTINS_ENABLE_WPA2 OFF CACHE BOOL "Unused stuff")
set(LIBTINS_BUILD_TESTS OFF CACHE BOOL "Unused stuff")
set(LIBTINS_BUILD_EXAMPLES OFF CACHE BOOL "Unused stuff")

include(FetchContent)
FetchContent_Declare(libtins
    GIT_REPOSITORY "https://github.com/mfontanini/libtins.git"
    GIT_TAG "e3aedc56edc7e9f39170c195de9a886a4309b0cb") # v4.4
FetchContent_MakeAvailable(libtins)

FetchContent_Declare(fmt
    GIT_REPOSITORY "https://github.com/fmtlib/fmt.git"
    GIT_TAG "c4ee726532178e556d923372f29163bd206d7732" # 9.0.0
)
FetchContent_MakeAvailable(fmt)

FetchContent_Declare(asio
    GIT_REPOSITORY "https://github.com/chriskohlhoff/asio"
    GIT_TAG "147f7225a96d45a2807a64e443177f621844e51c") # v1.24
FetchContent_MakeAvailable(asio)

FetchContent_Declare(circular_buffer
    GIT_REPOSITORY "https://github.com/jonathonracz/circular_buffer_stl.git"
    GIT_TAG "b0f97c5945c64d2231ab7e238f8e7ef48097ab47") # v4.4
FetchContent_MakeAvailable(circular_buffer)

find_package(Threads REQUIRED)
add_executable(sampa_aquisition sampa_aquisition.cpp)
target_include_directories(sampa_aquisition PRIVATE ${libtins_SOURCE_DIR}/include ${PCAP_INCLUDE_DIR} ${circular_buffer_SOURCE_DIR}/include)
target_link_libraries(sampa_aquisition PRIVATE tins ${CMAKE_THREAD_LIBS_INIT} fmt::fmt)

add_executable(fake_packets fake_packets.cpp)
target_include_directories(fake_packets PRIVATE ${asio_SOURCE_DIR}/asio/include)
target_link_libraries(fake_packets PRIVATE ${CMAKE_THREAD_LIBS_INIT} fmt::fmt)

add_executable(check_raw check_raw.cpp)
