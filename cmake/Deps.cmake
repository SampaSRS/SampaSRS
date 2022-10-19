# Download CPM dependency manager
set(CPM_DOWNLOAD_VERSION 0.35.5)

if(CPM_SOURCE_CACHE)
    set(CPM_DOWNLOAD_LOCATION "${CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
elseif(DEFINED ENV{CPM_SOURCE_CACHE})
    set(CPM_DOWNLOAD_LOCATION "$ENV{CPM_SOURCE_CACHE}/cpm/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
else()
    set(CPM_DOWNLOAD_LOCATION "${CMAKE_BINARY_DIR}/cmake/CPM_${CPM_DOWNLOAD_VERSION}.cmake")
endif()

if(NOT(EXISTS ${CPM_DOWNLOAD_LOCATION}))
    message(STATUS "Downloading CPM.cmake to ${CPM_DOWNLOAD_LOCATION}")
    file(DOWNLOAD
        https://github.com/cpm-cmake/CPM.cmake/releases/download/v${CPM_DOWNLOAD_VERSION}/CPM.cmake
        ${CPM_DOWNLOAD_LOCATION}
    )
endif()

include(${CPM_DOWNLOAD_LOCATION})

# Install required dependencies
CPMAddPackage("https://github.com/fmtlib/fmt.git#9.0.0")

CPMAddPackage(
    NAME Boost
    VERSION 1.80.0
    URL https://boostorg.jfrog.io/artifactory/main/release/1.80.0/source/boost_1_80_0.tar.gz
    DOWNLOAD_ONLY True
)

if(Boost_ADDED)
    # Define the header-only Boost target
    add_library(Boost::boost INTERFACE IMPORTED GLOBAL)
    target_include_directories(Boost::boost SYSTEM INTERFACE ${Boost_SOURCE_DIR})

    # Disable autolink
    target_compile_definitions(Boost::boost INTERFACE BOOST_ALL_NO_LIB=1)
endif()

# Install optional dependencies
if (SAMPA_BUILD_ACQUISITION)
    set(LIBTINS_OPTIONS
        "LIBTINS_ENABLE_ACK_TRACKER OFF"
        "LIBTINS_ENABLE_TCP_STREAM_CUSTOM_DATA OFF"
        "LIBTINS_ENABLE_WPA2 OFF"
        "LIBTINS_BUILD_TESTS OFF"
        "LIBTINS_BUILD_EXAMPLES OFF"
        "LIBTINS_BUILD_SHARED OFF"
    )

    if(WIN32)
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "Windows build")
        list(APPEND LIBTINS_OPTIONS "LIBTINS_BUILD_SHARED OFF")
        add_compile_definitions(TINS_STATIC NOMINMAX)
    endif()

    CPMAddPackage("https://github.com/mfontanini/libtins.git@4.4"
        NAME tins
        VERSION 4.4
        GIT_REPOSITORY "https://github.com/mfontanini/libtins.git"
        OPTIONS
        ${LIBTINS_OPTIONS}
        EXCLUDE_FROM_ALL TRUE)
endif()

if(SAMPA_BUILD_ACQUISITION AND SAMPA_BUILD_GUI)
    # ImGui
    CPMAddPackage(
    NAME hello_imgui
    GITHUB_REPOSITORY pthom/hello_imgui
    GIT_TAG master
    OPTIONS "HELLOIMGUI_USE_SDL_OPENGL3 ON"
    )
    set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${hello_imgui_SOURCE_DIR}/hello_imgui_cmake)
    set(imgui_SOURCE_DIR ${hello_imgui_SOURCE_DIR}/external/imgui)

    CPMAddPackage(
    NAME implot
    GITHUB_REPOSITORY epezent/implot
    VERSION 0.14
    )
endif()