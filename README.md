# waveshare_modbus_commander

A CLI tool for commanding the Waveshare Dual Ethernet Ports 8-ch Relay Module (C) via Modbus RTU Protocol.

## Prerequisites

- CMake 3.25+
- C++23 compiler (examples use `g++-14`)
- `pkg-config`
- `libmodbus` development package (`libmodbus-dev` on Debian/Ubuntu)

Example dependency install (Debian/Ubuntu):

```bash
sudo apt-get update
sudo apt-get install -y cmake pkg-config libmodbus-dev
```

## Build

`waveshare_modbus_commander` fetches `libmodbus-cpp` and `CLI11` via `FetchContent`.

Use a fresh build directory, especially when switching compilers:

```bash
rm -rf build
```

Build:

```bash
cd waveshare_modbus_commander
rm -rf build
CXX=g++-14 cmake -D CMAKE_BUILD_TYPE=Debug -B build -S .
cmake --build build -j4
```

## Run

```bash
./build/bin/waveshare_modbus_commander --help
```

## Waveshare Module Configuration

Visit the configuration website of your Waveshare module, e.g.http://192.168.178.69/ip_en.html and change the settings accordingly:

<img width="1320" height="1061" alt="image" src="https://github.com/user-attachments/assets/13b5a996-e5a3-4011-854e-92911b2115a4" />

