cmake_minimum_required(VERSION 3.25...4.2)

project(waveshare_modbus_commander
    VERSION 0.1.0
    DESCRIPTION "CLI commander for generic Modbus TCP devices"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(GNUInstallDirs)
include(FetchContent)

option(WAVESHARE_ENABLE_CPACK "Enable CPack packaging support" ${PROJECT_IS_TOP_LEVEL})

if(NOT TARGET modbus_cpp)
    FetchContent_Declare(
        libmodbus_cpp_proj
        GIT_REPOSITORY https://github.com/daixtrose/libmodbus-cpp.git
        GIT_TAG v1.1.0
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(libmodbus_cpp_proj)
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

install(TARGETS waveshare_modbus_commander
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WAVESHARE_ENABLE_CPACK)
    set(CPACK_PACKAGE_NAME waveshare-modbus-commander)
    set(CPACK_PACKAGE_VENDOR "Markus Werle")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "CLI commander for generic Modbus TCP devices")
    set(CPACK_PACKAGE_CONTACT "")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Markus Werle")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_VERBATIM_VARIABLES YES)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(CPACK_GENERATOR TGZ DEB)
        set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS TRUE)
    else()
        set(CPACK_GENERATOR TGZ)
    endif()

    include(CPack)
endif()
