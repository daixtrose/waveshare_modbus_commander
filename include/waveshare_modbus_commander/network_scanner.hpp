#ifndef WAVESHARE_NETWORK_SCANNER_HPP
#define WAVESHARE_NETWORK_SCANNER_HPP

#include <cstdint>
#include <string>
#include <vector>

namespace waveshare {

/// Information about a discovered Waveshare serial server device,
/// obtained via the VirCom UDP broadcast protocol (port 1092).
struct DiscoveredDevice {
    std::string ip_address;
    std::string subnet_mask;
    std::string gateway;
    std::string dns_server;
    std::string mac_address;    ///< From Ethernet frame (not in payload)
    std::string device_name;    ///< e.g. "WSDEV0001"
    std::string module_id;      ///< 10-byte module identifier
    std::string parameters;     ///< URL-encoded parameter string (e.g. "dsp=4196&ipm=1&bd")
    uint8_t ip_mode = 0;        ///< 0 = DHCP, 1 = Static
    uint8_t baud_rate_index = 0;
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

} // namespace waveshare

#endif // WAVESHARE_NETWORK_SCANNER_HPP
