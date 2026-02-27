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
        GIT_TAG v1.1.1
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

# ---------------------------------------------------------------------------
# Self-contained install configuration
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "waveshare-modbus-commander")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND WAVESHARE_ENABLE_CPACK)
    # Install to private prefix under /opt
    set(INSTALL_PREFIX "/opt/daixtrose/${CPACK_PACKAGE_NAME}-${PROJECT_VERSION}")
    set(CMAKE_INSTALL_PREFIX ${INSTALL_PREFIX} CACHE PATH "" FORCE)
    set(CPACK_PACKAGING_INSTALL_PREFIX ${INSTALL_PREFIX})

    # RUNPATH: executable finds shared libs relative to its own location
    file(RELATIVE_PATH _libdir_rel
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_BINDIR}
        ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_INSTALL_LIBDIR}
    )
    set_target_properties(waveshare_modbus_commander PROPERTIES
        BUILD_RPATH_USE_ORIGIN ON
        INSTALL_RPATH "$ORIGIN:$ORIGIN/${_libdir_rel}"
    )
endif()

install(TARGETS waveshare_modbus_commander
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Ship the libmodbus shared library built from source via ExternalProject
install(DIRECTORY "${CMAKE_BINARY_DIR}/_deps/libmodbus-install/lib/"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    FILES_MATCHING
    PATTERN "libmodbus.so*"
    PATTERN "pkgconfig" EXCLUDE
)

if(WAVESHARE_ENABLE_CPACK)
    set(CPACK_PACKAGE_VENDOR "Markus Werle")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
    set(CPACK_PACKAGE_CONTACT "daixtrose")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Markus Werle")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_VERBATIM_VARIABLES YES)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(CPACK_GENERATOR TGZ DEB)

        # Debian maintainer scripts: symlink into /usr/local/bin
        set(EXECUTABLE_NAME "waveshare_modbus_commander")
        configure_file(
            ${CMAKE_CURRENT_LIST_DIR}/packaging/postinst.in
            ${CMAKE_CURRENT_BINARY_DIR}/packaging/postinst
            @ONLY
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                             GROUP_READ GROUP_EXECUTE
                             WORLD_READ WORLD_EXECUTE
        )
        configure_file(
            ${CMAKE_CURRENT_LIST_DIR}/packaging/prerm.in
            ${CMAKE_CURRENT_BINARY_DIR}/packaging/prerm
            @ONLY
            FILE_PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE
                             GROUP_READ GROUP_EXECUTE
                             WORLD_READ WORLD_EXECUTE
        )
        list(APPEND CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
            "${CMAKE_CURRENT_BINARY_DIR}/packaging/postinst"
            "${CMAKE_CURRENT_BINARY_DIR}/packaging/prerm"
        )
    else()
        set(CPACK_GENERATOR TGZ)
    endif()

    include(CPack)
endif()
