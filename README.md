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

## Usage

### Relay Control

#### Iterate through all relay switches

Cycles through all 8 relay coils one by one — turns each on for 1 second,
then off — and repeats until interrupted with `Ctrl-C`. On exit all relays are
switched off safely. Requires a Modbus TCP connection to the device.

```bash
# Default device address (192.168.1.2:502)
./build/bin/waveshare_modbus_commander --iterate-relais-switches

# Explicit IP / port
./build/bin/waveshare_modbus_commander -i 192.168.178.69 -p 502 --iterate-relais-switches
```

Example output:

```
=== Iterate Relay Switches (Ctrl-C to stop) ===
All relays OFF
Coil 1 ON
Coil 1 OFF
Coil 2 ON
Coil 2 OFF
Coil 3 ON
Coil 3 OFF
Coil 4 ON
Coil 4 OFF
Coil 5 ON
Coil 5 OFF
Coil 6 ON
Coil 6 OFF
Coil 7 ON
Coil 7 OFF
Coil 8 ON
Coil 8 OFF
--- Cycle complete, waiting 3 seconds ---
Coil 1 ON
^C
Interrupted — turning all relays OFF ...
All relays OFF (safe shutdown)
```

---

### Network Discovery

#### Scan the network for Waveshare devices

Sends a VirCom UDP broadcast on port 1092 to discover all Waveshare serial
server / relay modules on the local network segment. On WSL2 a /24 subnet
sweep and unicast probes are used automatically to cross the NAT boundary.

```bash
# Scan with default 3-second timeout
./build/bin/waveshare_modbus_commander --scan-network

# Scan with a longer timeout (useful on slow networks)
./build/bin/waveshare_modbus_commander --scan-network --scan-timeout 5000

# Probe a specific IP directly (useful from WSL2 or across VLANs)
./build/bin/waveshare_modbus_commander --scan-network -i 192.168.178.69
```

Example output:

```
=== Scanning network for Waveshare devices ===
IP Address       MAC Address        Device Name  Port  Subnet Mask      Gateway          IP Mode  Module ID
---------------  -----------------  -----------  ----  ---------------  ---------------  -------  ----------
192.168.178.69   28:80:ca:ea:41:f3  WSDEV0001    502   255.255.255.0    192.168.178.1    DHCP     8888888888
192.168.1.200    28:80:ca:ec:41:f9  WSDEV0002    502   255.255.255.0    192.168.1.1      Static   8888888888

2 device(s) found.
```

---

### Device IP Configuration

All configuration commands identify the target device by its MAC address.
When only a single device is found on the network, it is auto-selected.
When multiple devices exist, `--mac` is required.

#### Set a static IP address

Assigns a static IP, subnet mask, gateway, and DNS server to the device.

```bash
# Single device on network (auto-selected)
./build/bin/waveshare_modbus_commander --set-ip 192.168.1.200 255.255.255.0 192.168.1.1 8.8.8.8

# Multiple devices — specify target by MAC
./build/bin/waveshare_modbus_commander --mac 28:80:ca:ec:41:f9 \
    --set-ip 192.168.1.200 255.255.255.0 192.168.1.1 8.8.8.8
```

Example output:

```
=== Set Static IP ===
Auto-selected the only device found: 192.168.178.69 (28:80:ca:ea:41:f3)
Static IP configuration sent to device 28:80:ca:ea:41:f3.
New IP: 192.168.1.200, Mask: 255.255.255.0, Gateway: 192.168.1.1, DNS: 8.8.8.8
```

#### Switch a device to DHCP

Enables DHCP on the device and waits for it to reappear with a new
DHCP-assigned IP. The wait timeout is configurable (default 30 s).

```bash
# Using MAC to target a specific device
./build/bin/waveshare_modbus_commander --mac 28:80:ca:ec:41:f9 --set-dhcp

# Custom wait timeout (10 seconds)
./build/bin/waveshare_modbus_commander --mac 28:80:ca:ec:41:f9 --set-dhcp --dhcp-wait-timeout 10000
```

Example output:

```
=== Set DHCP Mode ===
Switching device 28:80:ca:ec:41:f9 (192.168.1.200) to DHCP mode ...
Waiting for device 28:80:ca:ec:41:f9 to reappear with DHCP-assigned IP ...
Scanning for device 28:80:ca:ec:41:f9 (3s / 30s) ...
Scanning for device 28:80:ca:ec:41:f9 (8s / 30s) ...
Device 28:80:ca:ec:41:f9 reappeared at 192.168.178.69 (DHCP)
Device is now at 192.168.178.69 (DHCP)
IP Address       MAC Address        Device Name  Port  Subnet Mask      Gateway          IP Mode  Module ID
---------------  -----------------  -----------  ----  ---------------  ---------------  -------  ----------
192.168.178.69   28:80:ca:ec:41:f9  WSDEV0002    502   255.255.255.0    192.168.178.1    DHCP     8888888888

1 device(s) found.
```

---

### Device Name

#### Set the device name

Changes the device name (max 9 ASCII characters). The device is identified
by MAC when multiple devices are present.

```bash
# Rename a device
./build/bin/waveshare_modbus_commander --mac 28:80:ca:ea:41:f3 --set-name RELAY01

# Name too long — rejected with an error
./build/bin/waveshare_modbus_commander --mac 28:80:ca:ea:41:f3 --set-name ABCDEFGHIJ
```

Example output (success):

```
Auto-selected the only device found: 192.168.178.69 (28:80:ca:ea:41:f3)
Device name set to 'RELAY01' on device 28:80:ca:ea:41:f3.
```

Example output (name too long):

```
Error: Device name 'ABCDEFGHIJ' is too long (max 9 characters, got 10).
```

---

### Modbus TCP Protocol

#### Set a device to Modbus TCP mode

Configures the device's transfer protocol to Modbus TCP with TCP Server work
mode. The port defaults to 502 but can be overridden.

```bash
# Default port 502
./build/bin/waveshare_modbus_commander --mac 28:80:ca:ec:41:f9 --set-modbus-tcp

# Custom port
./build/bin/waveshare_modbus_commander --mac 28:80:ca:ec:41:f9 --set-modbus-tcp --modbus-tcp-port 4502
```

Example output:

```
=== Set Modbus TCP Protocol ===
Auto-selected the only device found: 192.168.1.200 (28:80:ca:ec:41:f9)
Modbus TCP configuration sent to device 28:80:ca:ec:41:f9.
Protocol: Modbus TCP, Work Mode: TCP Server, Port: 502
```

## Waveshare Module Configuration

Visit the configuration website of your Waveshare module, e.g.http://192.168.178.69/ip_en.html and change the settings accordingly:

<img width="1320" height="1061" alt="image" src="https://github.com/user-attachments/assets/13b5a996-e5a3-4011-854e-92911b2115a4" />

