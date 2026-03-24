cmake_minimum_required(VERSION 3.25...4.2)

project(waveshare_modbus_commander
    VERSION 0.9.0
    DESCRIPTION "CLI commander for generic Modbus TCP devices"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(FetchContent)

option(WAVESHARE_ENABLE_CPACK "Enable CPack packaging support" ${PROJECT_IS_TOP_LEVEL})

if(NOT TARGET waveshare)
    set(LIBWAVESHARE_ENABLE_CPACK OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        libwaveshare_proj
        GIT_REPOSITORY https://github.com/daixtrose/libwaveshare.git
        GIT_TAG v0.2.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(libwaveshare_proj)
endif()

if(NOT TARGET CLI11::CLI11)
    FetchContent_Declare(
        cli11_proj
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.6.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(cli11_proj)
endif()

add_executable(waveshare_modbus_commander
    ${CMAKE_CURRENT_LIST_DIR}/src/waveshare_modbus_commander.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/cli_parser.cpp
)

set_target_properties(waveshare_modbus_commander PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_include_directories(waveshare_modbus_commander
    PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(waveshare_modbus_commander
    PRIVATE
        waveshare
        CLI11::CLI11
)

target_compile_definitions(waveshare_modbus_commander PRIVATE
    PROJECT_VERSION="${PROJECT_VERSION}"
)

target_compile_features(waveshare_modbus_commander PRIVATE cxx_std_23)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(waveshare_modbus_commander PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
elseif(MSVC)
    target_compile_options(waveshare_modbus_commander PRIVATE
        /W4
    )
endif()

# Statically link MinGW runtime (libstdc++, libgcc, libwinpthread) so the
# resulting .exe is fully self-contained and runs without MSYS2/MinGW installed.
if(MINGW)
    target_link_options(waveshare_modbus_commander PRIVATE -static)
endif()

# Statically link C++ runtime for cross-compiled aarch64 binaries.
# The Bookworm sysroot provides glibc 2.36, but libstdc++ is from GCC 14.
# Bookworm only ships GCC 12 libstdc++, so we must link it statically.
if(CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
    target_link_options(waveshare_modbus_commander PRIVATE -static-libstdc++ -static-libgcc)
endif()

include(${CMAKE_CURRENT_LIST_DIR}/CMakeListsCPackConfiguration.txt)
