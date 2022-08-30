cmake_minimum_required(VERSION 3.14)

project(sampasrs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SAMPA_NATIVE_OPTIMIZATION ON "Target native architecture if supported by the compiler")
option(SAMPA_SANITIZERS OFF "Enable Address and UB sanitizers")

if(SAMPA_NATIVE_OPTIMIZATION AND NOT MSVC)
    add_compile_options(-march=native)
endif()

# Install dependencies
include(cmake/Deps.cmake)

find_package(Threads REQUIRED)

# Header only lib
add_library(sampasrs INTERFACE)
target_include_directories(sampasrs INTERFACE include ${libtins_SOURCE_DIR}/include ${PCAP_INCLUDE_DIR})
target_link_libraries(sampasrs INTERFACE tins ${CMAKE_THREAD_LIBS_INIT} fmt::fmt Boost::boost)

if(SAMPA_SANITIZERS)
    target_compile_options(sampasrs INTERFACE -fsanitize=address,undefined)
    target_link_options(sampasrs INTERFACE -fsanitize=address,undefined)
endif()

add_executable(sampa_aquisition sampa_aquisition.cpp)
target_link_libraries(sampa_aquisition PRIVATE sampasrs)

add_executable(sampa_decoder sampa_decoder.cpp)
target_link_libraries(sampa_decoder PRIVATE sampasrs)

add_executable(fake_packets fake_packets.cpp)
target_link_libraries(fake_packets PRIVATE sampasrs)

add_executable(check_raw check_raw.cpp)
