#ifndef WAVESHARE_NETWORK_SCANNER_HPP
#define WAVESHARE_NETWORK_SCANNER_HPP

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace waveshare {

/// VirCom packet size (fixed for all commands).
constexpr size_t VIRCOM_PACKET_SIZE = 170;

/// Information about a discovered Waveshare serial server device,
/// obtained via the VirCom UDP broadcast protocol (port 1092).
struct DiscoveredDevice {
    std::string ip_address;
    std::string subnet_mask;
    std::string gateway;
    std::string dns_server;
    std::string mac_address;    ///< MAC from VirCom payload (offset 0x22, 6 bytes)
    std::string device_name;    ///< e.g. "WSDEV0001"
    std::string module_id;      ///< 10-byte module identifier
    std::string parameters;     ///< URL-encoded parameter string (e.g. "dsp=4196&ipm=1&bd")
    uint8_t ip_mode = 0;        ///< 0 = Static, 1 = DHCP  (offset 0x3B)
    uint8_t baud_rate_index = 0;

    /// The full raw 170-byte VirCom response.  Used as a template
    /// when building SET_CONFIG packets.
    std::array<uint8_t, VIRCOM_PACKET_SIZE> raw_response{};
};

/// Scan the local network for Waveshare devices using the VirCom
/// UDP broadcast protocol.  Sends a search request to all available
/// broadcast addresses and collects responses within the given timeout.
///
/// The scan targets (in order):
///   1. 255.255.255.255 (limited broadcast — works on the local L2 segment)
///   2. Per-interface directed broadcasts (e.g. 192.168.178.255)
///   3. Unicast to @p target_ip, if non-empty (useful from NATed environments
///      such as WSL2 where broadcasts don't reach the physical LAN)
///
/// @param timeout_ms  How long to wait for responses (milliseconds).
/// @param debug       Print diagnostic information if true.
/// @param target_ip   Optional specific IP to probe via unicast.
/// @return Vector of discovered devices (may be empty).
std::vector<DiscoveredDevice> scan_network(int timeout_ms, bool debug,
                                           const std::string& target_ip = {});

/// Format a list of discovered devices as a human-readable table.
std::string format_device_table(const std::vector<DiscoveredDevice>& devices);

/// Set a device to a static IP configuration via VirCom SET_CONFIG.
/// The device is identified by its MAC address.
/// @param device      The device (from scan_network) to configure.
/// @param new_ip      New static IP address (dotted decimal).
/// @param new_mask    New subnet mask.
/// @param new_gateway New default gateway.
/// @param new_dns     New DNS server.
/// @param debug       Print diagnostic information.
/// @return true if the config packet was sent successfully.
bool set_device_ip(const DiscoveredDevice& device,
                   const std::string& new_ip,
                   const std::string& new_mask,
                   const std::string& new_gateway,
                   const std::string& new_dns,
                   bool debug);

/// Set a device to DHCP mode via VirCom SET_CONFIG.
/// After sending the command, waits for the device to reappear on the
/// network with a new (DHCP-assigned) IP address.
/// @param device               The device to configure.
/// @param wait_timeout_ms      How long to wait for DHCP reassignment.
/// @param debug                Print diagnostic information.
/// @return The new DiscoveredDevice with updated IP, or empty if not found.
std::vector<DiscoveredDevice> set_device_dhcp(
    const DiscoveredDevice& device,
    int wait_timeout_ms,
    bool debug);

/// Configure a device for Modbus TCP protocol via VirCom SET_CONFIG.
/// Sets Transfer Protocol to Modbus TCP, Work Mode to TCP Server,
/// and the listening port (default 502).
/// @param device  The device to configure.
/// @param port    TCP port for Modbus (default 502).
/// @param debug   Print diagnostic information.
/// @return true if the config packet was sent successfully.
bool set_device_modbus_tcp(const DiscoveredDevice& device,
                           uint16_t port,
                           bool debug);

/// Set the device name via VirCom SET_CONFIG.
/// @param device  The device to configure.
/// @param name    New device name (max 9 ASCII characters).
/// @param debug   Print diagnostic information.
/// @return true if the config packet was sent successfully.
bool set_device_name(const DiscoveredDevice& device,
                     const std::string& name,
                     bool debug);

} // namespace waveshare

#endif // WAVESHARE_NETWORK_SCANNER_HPP
