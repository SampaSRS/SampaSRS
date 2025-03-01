cmake_minimum_required(VERSION 3.14)

project(sampasrs LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Set default build type
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(default_build_type "Release")
    message(STATUS "Setting build type to '${default_build_type}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${default_build_type}" CACHE
        STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

option(SAMPA_BUILD_ACQUISITION "Build Acquisition" ON)
option(SAMPA_BUILD_GUI "Build Acquisition GUI" ON)
option(SAMPA_SANITIZERS "Enable Address and UB sanitizers" OFF)
option(SAMPA_NATIVE_OPTIMIZATION "Target native architecture if supported by the compiler" ON)

if(SAMPA_NATIVE_OPTIMIZATION AND NOT MSVC)
    add_compile_options(-march=native)
endif()

# Install dependencies
include(cmake/Deps.cmake)

find_package(Threads REQUIRED)

set(targets_to_install "")

# Header only lib
add_library(sampasrs INTERFACE)
target_include_directories(sampasrs INTERFACE include ${libtins_SOURCE_DIR}/include ${PCAP_INCLUDE_DIR})
target_link_libraries(sampasrs INTERFACE ${CMAKE_THREAD_LIBS_INIT} fmt::fmt Boost::boost)

if(SAMPA_SANITIZERS)
target_compile_options(sampasrs INTERFACE -fsanitize=address,undefined)
target_link_options(sampasrs INTERFACE -fsanitize=address,undefined)
endif()

if (SAMPA_BUILD_ACQUISITION)
    message(STATUS "Building acquisition code")
    target_include_directories(sampasrs INTERFACE ${libtins_SOURCE_DIR}/include ${PCAP_INCLUDE_DIR})
    target_link_libraries(sampasrs INTERFACE tins)

    # Define macro for all targets that uses it
    target_compile_definitions(sampasrs INTERFACE WITH_LIBPCAP)
    
    add_executable(sampa_acquisition sampa_acquisition.cpp)
    target_link_libraries(sampa_acquisition PRIVATE sampasrs)
    
    add_executable(fake_packets fake_packets.cpp)
    target_link_libraries(fake_packets PRIVATE sampasrs)
    
    add_executable(replay replay.cpp)
    target_link_libraries(replay PRIVATE sampasrs)

    list(APPEND targets_to_install sampa_acquisition fake_packets replay)
endif()

find_package(ROOT 6.18 CONFIG)
if (ROOT_FOUND)
# include("${ROOT_DIR}/modules/RootNewMacros.cmake")
    message(STATUS "ROOT found")

    add_executable(sampa_decoder sampa_decoder.cpp)
    add_executable(clustering clustering.cpp clusters.cpp)
    add_executable(2D_clustering 2D_clustering.cpp clusters.cpp)
    add_executable(zs_clustering zs_clustering.cpp clusters.cpp)
    add_executable(common_mode common_mode.cpp)
    add_executable(create_pedestal create_pedestal.cpp)
    
    add_library(sampa_root SHARED)
    target_link_libraries(sampa_root PUBLIC ROOT::Tree ROOT::TreePlayer)
    target_include_directories(sampa_root PUBLIC include)
    root_generate_dictionary(sampa_dicts LINKDEF include/sampasrs/LinkDef.h MODULE sampa_root)
    
    target_link_libraries(sampa_decoder PRIVATE sampasrs sampa_root)
    target_link_libraries(clustering PRIVATE sampasrs sampa_root)
    target_link_libraries(zs_clustering PRIVATE sampasrs sampa_root)
    target_link_libraries(common_mode PRIVATE sampasrs sampa_root)
    target_link_libraries(create_pedestal PRIVATE sampasrs sampa_root)
    target_link_libraries(2D_clustering PRIVATE sampasrs sampa_root)


    list(APPEND targets_to_install sampa_decoder clustering zs_clustering common_mode 2D_clustering create_pedestal sampa_root)
else()
    message(WARNING "ROOT not found, sampa_decoder won't be compiled")
endif()

add_executable(sampa_control sampa_control.cpp)
target_link_libraries(sampa_control PRIVATE sampasrs)

add_executable(check_raw check_raw.cpp)

if (SAMPA_BUILD_ACQUISITION AND SAMPA_BUILD_GUI)
    message(STATUS "Building acquisition GUI")
    include(hello_imgui_add_app)
    hello_imgui_add_app(
        sampa_gui
        sampa_gui.cpp
        ${implot_SOURCE_DIR}/implot.h
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_internal.h
        ${implot_SOURCE_DIR}/implot_items.cpp
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.h
        ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
        ${ImGuiFileDialog_SOURCES}
    )
    target_include_directories(sampa_gui PRIVATE ${implot_SOURCE_DIR} ${ImGuiFileDialog_SOURCE_DIR})
    target_link_libraries(sampa_gui PRIVATE sampasrs sampa_root)
    target_compile_definitions(sampa_gui PRIVATE USE_BOOKMARK)


    list(APPEND targets_to_install sampa_gui)
endif()

install(
    TARGETS ${targets_to_install}
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
    INCLUDES DESTINATION include)

install(
    FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/scripts/thissampa.sh
    DESTINATION bin)
        
if (ROOT_FOUND)
    install(
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/libsampa_root_rdict.pcm
        ${CMAKE_CURRENT_BINARY_DIR}/libsampa_root.rootmap
        DESTINATION lib)
endif()
