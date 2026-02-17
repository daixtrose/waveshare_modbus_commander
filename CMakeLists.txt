cmake_minimum_required(VERSION 3.25)

project(waveshare_modbus_commander
    VERSION 0.1.0
    DESCRIPTION "CLI commander for generic Modbus TCP devices"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

if(NOT TARGET modbus_cpp)
    FetchContent_Declare(
        libmodbus_cpp_proj
        GIT_REPOSITORY https://github.com/daixtrose/libmodbus-cpp.git
        GIT_TAG v0.1.0
    )
    FetchContent_MakeAvailable(libmodbus_cpp_proj)
endif()

if(NOT TARGET CLI11::CLI11)
    FetchContent_Declare(
        cli11_proj
        QUIET
        GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
        GIT_TAG v2.6.1
    )
    FetchContent_MakeAvailable(cli11_proj)
endif()

add_executable(waveshare_modbus_commander
    ${CMAKE_CURRENT_LIST_DIR}/src/waveshare_modbus_commander.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/cli_parser.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/create_modbus_connection.cpp
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
        modbus_cpp
        CLI11::CLI11
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
