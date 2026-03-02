#include "waveshare_modbus_commander/network_scanner.hpp"
#include "waveshare_modbus_commander/portable_print.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <format>
#include <fstream>
#include <set>
#include <sstream>
#include <string>

// Platform-specific socket headers
#ifdef _WIN32
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  pragma comment(lib, "ws2_32.lib")
   using socket_t = SOCKET;
   constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
   inline int close_socket(socket_t s) { return closesocket(s); }
   inline int set_socket_nonblocking(socket_t s) {
       u_long mode = 1;
       return ioctlsocket(s, FIONBIO, &mode);
   }
   inline int get_last_socket_error() { return WSAGetLastError(); }
   inline bool would_block(int err) { return err == WSAEWOULDBLOCK; }
#else
#  include <arpa/inet.h>
#  include <cerrno>
#  include <fcntl.h>
#  include <net/if.h>
#  include <netinet/in.h>
#  include <sys/ioctl.h>
#  include <sys/socket.h>
#  include <sys/types.h>
#  include <unistd.h>
#  include <ifaddrs.h>
#  include <netdb.h>
#  include <netpacket/packet.h>
   using socket_t = int;
   constexpr socket_t INVALID_SOCK = -1;
   inline int close_socket(socket_t s) { return ::close(s); }
   inline int set_socket_nonblocking(socket_t s) {
       int flags = fcntl(s, F_GETFL, 0);
       return fcntl(s, F_SETFL, flags | O_NONBLOCK);
   }
   inline int get_last_socket_error() { return errno; }
   inline bool would_block(int err) { return err == EAGAIN || err == EWOULDBLOCK; }
#endif

namespace waveshare {

namespace {

/// VirCom protocol constants
constexpr uint16_t VIRCOM_PORT = 1092;
constexpr size_t   VIRCOM_PACKET_SIZE = 170;
constexpr uint8_t  VIRCOM_MAGIC_0 = 0x5A; // 'Z'
constexpr uint8_t  VIRCOM_MAGIC_1 = 0x4C; // 'L'
constexpr uint8_t  VIRCOM_CMD_SEARCH  = 0x00;
constexpr uint8_t  VIRCOM_CMD_RESPONSE = 0x01;

/// Build a VirCom search request packet (170 bytes).
/// Only the first 3 bytes are significant: 5A 4C 00
std::array<uint8_t, VIRCOM_PACKET_SIZE> build_search_request()
{
    std::array<uint8_t, VIRCOM_PACKET_SIZE> packet{};
    packet[0] = VIRCOM_MAGIC_0;
    packet[1] = VIRCOM_MAGIC_1;
    packet[2] = VIRCOM_CMD_SEARCH;
    return packet;
}

/// Format 4 bytes as a dotted-decimal IPv4 address string.
std::string format_ip(const uint8_t* data)
{
    return std::format("{}.{}.{}.{}", data[0], data[1], data[2], data[3]);
}

/// Format 6 bytes as a colon-separated MAC address string.
[[maybe_unused]]
std::string format_mac(const uint8_t* data)
{
    return std::format("{:02x}:{:02x}:{:02x}:{:02x}:{:02x}:{:02x}",
                       data[0], data[1], data[2], data[3], data[4], data[5]);
}

/// Extract a null-terminated ASCII string from a byte buffer.
std::string extract_string(const uint8_t* data, size_t max_len)
{
    size_t len = 0;
    while (len < max_len && data[len] != 0 && data[len] >= 0x20 && data[len] < 0x7F) {
        ++len;
    }
    return std::string(reinterpret_cast<const char*>(data), len);
}

/// Parse a VirCom search response (170 bytes) into a DiscoveredDevice.
/// Returns true on success.
bool parse_response(const uint8_t* data, size_t len, DiscoveredDevice& dev)
{
    if (len < VIRCOM_PACKET_SIZE) return false;
    if (data[0] != VIRCOM_MAGIC_0 || data[1] != VIRCOM_MAGIC_1) return false;
    if (data[2] != VIRCOM_CMD_RESPONSE) return false;

    dev.ip_address  = format_ip(&data[3]);
    dev.subnet_mask = format_ip(&data[7]);
    dev.gateway     = format_ip(&data[11]);
    dev.dns_server  = format_ip(&data[15]);
    dev.ip_mode     = data[19];
    dev.baud_rate_index = data[22];

    // Module ID: 10 ASCII bytes at offset 0x18
    dev.module_id = extract_string(&data[0x18], 10);

    // Device name: null-terminated string at offset 0x29
    dev.device_name = extract_string(&data[0x29], 16);

    // Parameter string: scan for "dsp=" or similar URL-encoded params.
    // Located after a null-terminated DNS/IP ASCII string.
    // Start searching from offset 0x45 for the parameter block.
    for (size_t i = 0x45; i < len - 3; ++i) {
        if (data[i] == 'd' && data[i+1] == 's' && data[i+2] == 'p' && data[i+3] == '=') {
            dev.parameters = extract_string(&data[i], len - i);
            break;
        }
    }
    // If no "dsp=" found, try extracting any string after the first null past offset 0x45
    if (dev.parameters.empty()) {
        for (size_t i = 0x45; i < len; ++i) {
            if (data[i] == 0 && i + 1 < len && data[i+1] >= 0x20) {
                dev.parameters = extract_string(&data[i+1], len - i - 1);
                break;
            }
        }
    }

    return true;
}

#ifdef _WIN32
/// RAII wrapper for Winsock initialization.
struct WinsockInit {
    bool ok = false;
    WinsockInit() {
        WSADATA wsa;
        ok = (WSAStartup(MAKEWORD(2, 2), &wsa) == 0);
    }
    ~WinsockInit() { if (ok) WSACleanup(); }
    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;
};
#endif

// ---------------------------------------------------------------------------
// WSL2 auto-discovery helpers
// ---------------------------------------------------------------------------

/// Detect if we are running inside WSL2 by inspecting /proc/version.
bool is_wsl2()
{
#ifdef _WIN32
    return false;
#else
    std::ifstream proc_ver("/proc/version");
    if (!proc_ver) return false;
    std::string line;
    std::getline(proc_ver, line);
    // WSL2 kernels contain "microsoft" (case-insensitive)
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    return lower.find("microsoft") != std::string::npos;
#endif
}

/// Parse /etc/resolv.conf and return all "search" domains.
std::vector<std::string> get_search_domains()
{
    std::vector<std::string> domains;
#ifndef _WIN32
    std::ifstream resolv("/etc/resolv.conf");
    std::string line;
    while (std::getline(resolv, line)) {
        // Skip comments
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string key;
        iss >> key;
        if (key == "search" || key == "domain") {
            std::string dom;
            while (iss >> dom) {
                domains.push_back(dom);
            }
        }
    }
#endif
    return domains;
}

/// Collect IPv4 addresses of all local interfaces.
std::set<uint32_t> get_local_interface_ips()
{
    std::set<uint32_t> ips;
#ifndef _WIN32
    struct ifaddrs* ifa_list = nullptr;
    if (getifaddrs(&ifa_list) == 0) {
        for (auto* ifa = ifa_list; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            auto* sa = reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
            ips.insert(ntohl(sa->sin_addr.s_addr));
        }
        freeifaddrs(ifa_list);
    }
#endif
    return ips;
}

/// Discover /24 subnets reachable from WSL2 by resolving the machine's
/// hostname using the DNS search domains from /etc/resolv.conf.
/// Returns a list of network-byte-order /24 base addresses (host part = 0).
std::vector<uint32_t> discover_host_subnets(bool debug)
{
    std::vector<uint32_t> subnets;
#ifndef _WIN32
    auto local_ips = get_local_interface_ips();
    auto domains = get_search_domains();

    char hostname[256]{};
    if (::gethostname(hostname, sizeof(hostname)) != 0) return subnets;

    // Build candidate FQDNs: hostname.domain for each search domain,
    // plus the bare hostname.
    std::vector<std::string> candidates;
    for (const auto& dom : domains) {
        candidates.push_back(std::string(hostname) + "." + dom);
    }
    candidates.emplace_back(hostname);

    for (const auto& name : candidates) {
        struct addrinfo hints{};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        struct addrinfo* res = nullptr;
        if (::getaddrinfo(name.c_str(), nullptr, &hints, &res) != 0 || !res) {
            continue;
        }

        for (auto* rp = res; rp; rp = rp->ai_next) {
            if (rp->ai_family != AF_INET) continue;
            auto* sa = reinterpret_cast<sockaddr_in*>(rp->ai_addr);
            uint32_t ip = ntohl(sa->sin_addr.s_addr);

            // Skip loopback and link-local
            if ((ip >> 24) == 127) continue;
            if ((ip >> 16) == 0xA9FE) continue;  // 169.254.x.x

            // Skip IPs that match a local WSL2 interface (already covered
            // by the broadcast approach)
            if (local_ips.count(ip)) continue;

            // Derive the /24 base
            uint32_t subnet = ip & 0xFFFFFF00u;

            // Avoid duplicates
            bool dup = false;
            for (auto s : subnets) {
                if (s == subnet) { dup = true; break; }
            }
            if (!dup) {
                if (debug) {
                    uint32_t net = subnet;
                    portable::println("WSL2: resolved '{}' -> {}.{}.{}.{}, will scan {}.{}.{}.0/24",
                                      name,
                                      (ip >> 24) & 0xFF, (ip >> 16) & 0xFF,
                                      (ip >> 8) & 0xFF, ip & 0xFF,
                                      (net >> 24) & 0xFF, (net >> 16) & 0xFF,
                                      (net >> 8) & 0xFF);
                }
                subnets.push_back(subnet);
            }
        }
        ::freeaddrinfo(res);
    }
#endif
    return subnets;
}

// ---------------------------------------------------------------------------
// Interface broadcast enumeration
// ---------------------------------------------------------------------------

/// Collect all interface broadcast addresses for the running system.
/// On Linux this uses getifaddrs(); on Windows GetAdaptersAddresses().
std::vector<in_addr> get_interface_broadcast_addresses()
{
    std::vector<in_addr> addrs;

#ifdef _WIN32
    // On Windows, the limited broadcast (255.255.255.255) is usually
    // sufficient.  Directed broadcasts require GetAdaptersAddresses
    // which we skip here for simplicity — the unicast fallback covers it.
#else
    struct ifaddrs* ifa_list = nullptr;
    if (getifaddrs(&ifa_list) == 0) {
        for (auto* ifa = ifa_list; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;
            if ((ifa->ifa_flags & IFF_BROADCAST) == 0) continue;
            if ((ifa->ifa_flags & IFF_UP) == 0) continue;
            if (ifa->ifa_broadaddr == nullptr) continue;

            auto* bcast = reinterpret_cast<sockaddr_in*>(ifa->ifa_broadaddr);
            // Skip the loopback / link-local broadcast
            uint32_t ip = ntohl(bcast->sin_addr.s_addr);
            if (ip == INADDR_BROADCAST || ip == INADDR_ANY) continue;
            // Avoid duplicates
            bool dup = false;
            for (const auto& a : addrs) {
                if (a.s_addr == bcast->sin_addr.s_addr) { dup = true; break; }
            }
            if (!dup) addrs.push_back(bcast->sin_addr);
        }
        freeifaddrs(ifa_list);
    }
#endif

    return addrs;
}

/// Helper: send the search request to a given sockaddr_in target.
int send_search(socket_t sock, const std::array<uint8_t, VIRCOM_PACKET_SIZE>& request,
                const sockaddr_in& dest)
{
    return static_cast<int>(
        ::sendto(sock,
                 reinterpret_cast<const char*>(request.data()),
                 static_cast<int>(request.size()),
                 0,
                 reinterpret_cast<const sockaddr*>(&dest),
                 sizeof(dest)));
}

} // anonymous namespace

std::vector<DiscoveredDevice> scan_network(int timeout_ms, bool debug,
                                           const std::string& target_ip)
{
    std::vector<DiscoveredDevice> devices;

#ifdef _WIN32
    WinsockInit wsa_init;
    if (!wsa_init.ok) {
        portable::println(stderr, "Failed to initialize Winsock");
        return devices;
    }
#endif

    // Create UDP socket
    socket_t sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCK) {
        portable::println(stderr, "Failed to create UDP socket: error {}", get_last_socket_error());
        return devices;
    }

    // Enable broadcast
    int broadcast_enable = 1;
    if (::setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
                     reinterpret_cast<const char*>(&broadcast_enable),
                     sizeof(broadcast_enable)) < 0) {
        portable::println(stderr, "Failed to enable broadcast: error {}", get_last_socket_error());
        close_socket(sock);
        return devices;
    }

    // Allow address reuse
    int reuse = 1;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    // Bind to any address to receive responses
    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = INADDR_ANY;
    bind_addr.sin_port = 0; // let OS choose ephemeral port
    if (::bind(sock, reinterpret_cast<sockaddr*>(&bind_addr), sizeof(bind_addr)) < 0) {
        portable::println(stderr, "Failed to bind UDP socket: error {}", get_last_socket_error());
        close_socket(sock);
        return devices;
    }

    auto request = build_search_request();

    // -----------------------------------------------------------------------
    // Strategy: send the VirCom search packet to multiple targets to maximise
    // the chance of reaching devices, even across NAT / virtual interfaces.
    // -----------------------------------------------------------------------

    // 1. Limited broadcast (255.255.255.255) — works on the local L2 segment
    {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_addr.s_addr = INADDR_BROADCAST;
        dest.sin_port = htons(VIRCOM_PORT);

        int sent = send_search(sock, request, dest);
        if (debug) {
            if (sent > 0)
                portable::println("Sent VirCom search ({} bytes) -> 255.255.255.255:{}", sent, VIRCOM_PORT);
            else
                portable::println("Failed to send limited broadcast: error {}", get_last_socket_error());
        }
    }

    // 2. Per-interface directed broadcasts (e.g. 192.168.178.255)
    {
        auto bcast_addrs = get_interface_broadcast_addresses();
        for (const auto& baddr : bcast_addrs) {
            sockaddr_in dest{};
            dest.sin_family = AF_INET;
            dest.sin_addr = baddr;
            dest.sin_port = htons(VIRCOM_PORT);

            char ip_str[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &baddr, ip_str, sizeof(ip_str));

            int sent = send_search(sock, request, dest);
            if (debug) {
                if (sent > 0)
                    portable::println("Sent VirCom search ({} bytes) -> {}:{}", sent, ip_str, VIRCOM_PORT);
                else
                    portable::println("Failed to send to {}: error {}", ip_str, get_last_socket_error());
            }
        }
    }

    // 3. Unicast to a specific target IP (if provided, e.g. from --ip)
    if (!target_ip.empty()) {
        sockaddr_in dest{};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(VIRCOM_PORT);
        if (inet_pton(AF_INET, target_ip.c_str(), &dest.sin_addr) == 1) {
            int sent = send_search(sock, request, dest);
            if (debug) {
                if (sent > 0)
                    portable::println("Sent VirCom search ({} bytes) -> {}:{} (unicast)", sent, target_ip, VIRCOM_PORT);
                else
                    portable::println("Failed to send to {}: error {}", target_ip, get_last_socket_error());
            }
        } else if (debug) {
            portable::println("Invalid target IP for unicast scan: {}", target_ip);
        }
    }

    // 4. WSL2 auto-discovery: resolve the Windows host's physical LAN IPs
    //    via hostname + DNS search domain, then unicast-sweep each /24 subnet.
    //    This is needed because WSL2's NAT prevents UDP broadcasts from
    //    reaching the physical LAN.  Unicast packets are NATed through.
    if (is_wsl2()) {
        auto host_subnets = discover_host_subnets(debug);
        if (!host_subnets.empty()) {
            if (debug) {
                portable::println("WSL2 detected — sweeping {} discovered subnet(s) with unicast probes",
                                  host_subnets.size());
            }
            for (uint32_t subnet : host_subnets) {
                // Send to .1 through .254 in each /24
                for (int host = 1; host <= 254; ++host) {
                    uint32_t ip_host = subnet | static_cast<uint32_t>(host);
                    sockaddr_in dest{};
                    dest.sin_family = AF_INET;
                    dest.sin_port = htons(VIRCOM_PORT);
                    dest.sin_addr.s_addr = htonl(ip_host);
                    send_search(sock, request, dest);
                }
                if (debug) {
                    portable::println("Sent 254 unicast probes to {}.{}.{}.1-254:{}",
                                      (subnet >> 24) & 0xFF, (subnet >> 16) & 0xFF,
                                      (subnet >> 8) & 0xFF, VIRCOM_PORT);
                }
            }
        } else if (debug) {
            portable::println("WSL2 detected but no additional subnets discovered via hostname resolution");
        }
    }

    if (debug) {
        portable::println("Waiting {} ms for responses ...", timeout_ms);
    }

    // Set socket to non-blocking for the receive loop
    set_socket_nonblocking(sock);

    // Collect responses until timeout
    auto start = std::chrono::steady_clock::now();
    std::array<uint8_t, 512> recv_buf{};

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start);
        if (elapsed.count() >= timeout_ms) break;

        sockaddr_in sender_addr{};
        socklen_t sender_len = sizeof(sender_addr);

        auto n = ::recvfrom(sock,
                            reinterpret_cast<char*>(recv_buf.data()),
                            static_cast<int>(recv_buf.size()),
                            0,
                            reinterpret_cast<sockaddr*>(&sender_addr),
                            &sender_len);

        if (n < 0) {
            int err = get_last_socket_error();
            if (would_block(err)) {
                // No data yet — brief sleep to avoid busy-waiting
                #ifdef _WIN32
                    Sleep(10);
                #else
                    usleep(10000);
                #endif
                continue;
            }
            // When sweeping many hosts, ICMP "destination unreachable" causes
            // ECONNREFUSED on Linux (or WSAECONNRESET on Windows).  These are
            // transient and must not abort the receive loop.
            #ifdef _WIN32
            if (err == WSAECONNRESET || err == WSAECONNREFUSED) { continue; }
            #else
            if (err == ECONNREFUSED || err == ENETUNREACH || err == EHOSTUNREACH) { continue; }
            #endif
            // Genuine error
            if (debug) {
                portable::println("recvfrom error: {}", err);
            }
            break;
        }

        if (n < static_cast<decltype(n)>(VIRCOM_PACKET_SIZE)) {
            if (debug) {
                portable::println("Ignoring short packet ({} bytes) from {}",
                                  n, inet_ntoa(sender_addr.sin_addr));
            }
            continue;
        }

        DiscoveredDevice dev;
        if (parse_response(recv_buf.data(), static_cast<size_t>(n), dev)) {
            // Use the sender's IP from the socket layer as canonical IP
            // (in case the payload IP differs due to NAT or misconfiguration)
            char sender_ip[INET_ADDRSTRLEN]{};
            inet_ntop(AF_INET, &sender_addr.sin_addr, sender_ip, sizeof(sender_ip));

            // Avoid duplicates (same IP)
            bool duplicate = false;
            for (const auto& existing : devices) {
                if (existing.ip_address == dev.ip_address) {
                    duplicate = true;
                    break;
                }
            }

            if (!duplicate) {
                if (debug) {
                    portable::println("Received response from {} (payload IP: {})",
                                      sender_ip, dev.ip_address);
                    portable::println("  Device name: {}", dev.device_name);
                    portable::println("  Module ID:   {}", dev.module_id);
                    portable::println("  Subnet:      {}", dev.subnet_mask);
                    portable::println("  Gateway:     {}", dev.gateway);
                    portable::println("  DNS:         {}", dev.dns_server);
                    portable::println("  IP mode:     {} ({})", dev.ip_mode,
                                      dev.ip_mode == 1 ? "DHCP" : "Static");
                    portable::println("  Parameters:  {}", dev.parameters);
                }
                devices.push_back(std::move(dev));
            }
        } else if (debug) {
            portable::println("Failed to parse response from {}", inet_ntoa(sender_addr.sin_addr));
        }
    }

    close_socket(sock);
    return devices;
}

std::string format_device_table(const std::vector<DiscoveredDevice>& devices)
{
    if (devices.empty()) {
        return "No Waveshare devices found.\n";
    }

    // Determine column widths
    size_t w_ip   = 15;  // "IP Address"
    size_t w_name = 11;  // "Device Name"
    size_t w_mask = 15;  // "Subnet Mask"
    size_t w_gw   = 15;  // "Gateway"
    size_t w_mode = 7;   // "IP Mode"
    size_t w_mod  = 9;   // "Module ID"

    for (const auto& d : devices) {
        w_ip   = std::max(w_ip,   d.ip_address.size());
        w_name = std::max(w_name, d.device_name.size());
        w_mask = std::max(w_mask, d.subnet_mask.size());
        w_gw   = std::max(w_gw,   d.gateway.size());
        w_mod  = std::max(w_mod,  d.module_id.size());
    }

    std::string out;

    // Header
    out += std::format("{:<{}}  {:<{}}  {:<{}}  {:<{}}  {:<{}}  {:<{}}\n",
                       "IP Address", w_ip,
                       "Device Name", w_name,
                       "Subnet Mask", w_mask,
                       "Gateway", w_gw,
                       "IP Mode", w_mode,
                       "Module ID", w_mod);

    // Separator
    out += std::string(w_ip, '-') + "  " +
           std::string(w_name, '-') + "  " +
           std::string(w_mask, '-') + "  " +
           std::string(w_gw, '-') + "  " +
           std::string(w_mode, '-') + "  " +
           std::string(w_mod, '-') + "\n";

    // Rows
    for (const auto& d : devices) {
        std::string mode = (d.ip_mode == 1) ? "DHCP" : "Static";
        out += std::format("{:<{}}  {:<{}}  {:<{}}  {:<{}}  {:<{}}  {:<{}}\n",
                           d.ip_address, w_ip,
                           d.device_name, w_name,
                           d.subnet_mask, w_mask,
                           d.gateway, w_gw,
                           mode, w_mode,
                           d.module_id, w_mod);
    }

    out += std::format("\n{} device(s) found.\n", devices.size());

    return out;
}

} // namespace waveshare
