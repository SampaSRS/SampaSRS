cmake_minimum_required(VERSION 3.14)

project(sampasrs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SAMPA_NATIVE_OPTIMIZATION ON "Target native architecture if supported by the compiler")

if(SAMPA_NATIVE_OPTIMIZATION AND NOT MSVC)
    add_compile_options(-march=native)
endif()

# Install dependencies
include(cmake/Deps.cmake)

find_package(Threads REQUIRED)
add_executable(sampa_aquisition sampa_aquisition.cpp)
target_include_directories(sampa_aquisition PRIVATE ${libtins_SOURCE_DIR}/include ${PCAP_INCLUDE_DIR})
target_link_libraries(sampa_aquisition PRIVATE tins ${CMAKE_THREAD_LIBS_INIT} fmt::fmt Boost::boost)

add_executable(fake_packets fake_packets.cpp)
target_include_directories(fake_packets PRIVATE)
target_link_libraries(fake_packets PRIVATE ${CMAKE_THREAD_LIBS_INIT} fmt::fmt Boost::boost)

add_executable(event_sorter event_sorter.cpp)
target_include_directories(event_sorter PRIVATE ${libtins_SOURCE_DIR}/include ${PCAP_INCLUDE_DIR})
target_link_libraries(event_sorter PRIVATE tins ${CMAKE_THREAD_LIBS_INIT} fmt::fmt Boost::boost)

# target_compile_options(event_sorter PRIVATE -fsanitize=address,undefined)
# target_link_options(event_sorter PRIVATE -fsanitize=address,undefined)
add_executable(check_raw check_raw.cpp)
