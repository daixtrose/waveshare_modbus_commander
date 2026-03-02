#include "libmodbus_cpp/modbus_connection.hpp"
#include "waveshare_modbus_commander/cli_parser.hpp"
#include "waveshare_modbus_commander/create_modbus_connection.hpp"
#include "waveshare_modbus_commander/network_scanner.hpp"
#include "waveshare_modbus_commander/portable_print.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <format>
#include <functional>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    // Global flag for Ctrl-C signal handling
    std::atomic<bool> g_interrupted{false};

    void sigint_handler(int /*signum*/)
    {
        g_interrupted.store(true, std::memory_order_relaxed);
    }

    bool try_parse_coil_state(const std::string &state_token, bool &state)
    {
        if (state_token == "on" || state_token == "ON" ||
            state_token == "true" || state_token == "TRUE" ||
            state_token == "1")
        {
            state = true;
            return true;
        }

        if (state_token == "off" || state_token == "OFF" ||
            state_token == "false" || state_token == "FALSE" ||
            state_token == "0")
        {
            state = false;
            return true;
        }

        return false;
    }

    void execute_write_coil(libmodbus_cpp::ModbusConnection &conn, const waveshare::CoilWriteArgs &args)
    {
        try
        {
            auto addr = std::stoi(args.address, nullptr, 0);
            bool state = false;
            if (!try_parse_coil_state(args.state, state))
            {
                portable::println("Invalid coil state '{}'. Use one of: on|off|true|false|1|0", args.state);
                return;
            }

            if (conn.write_coil(static_cast<uint16_t>(addr), state))
            {
                portable::println("Coil 0x{:04X} = {} (SUCCESS)", addr, state ? "ON" : "OFF");
            }
            else
            {
                portable::println("Coil 0x{:04X} = {} (FAILED): {}", addr, state ? "ON" : "OFF", conn.get_last_error());
            }
        }
        catch (const std::exception &e)
        {
            portable::println("Error: {}", e.what());
        }
    }
}

int main(int argc, char *argv[])
{
    try
    {
        auto options = waveshare::parse_command_line(argc, argv);

        if (options.debug)
        {
            portable::println("========================");
            portable::println("Waveshare Modbus Commander");
            portable::println("========================");
            portable::println("Connecting to {}:{}", options.ip_address, options.port);
            portable::println("");

            portable::println("Command Line Options:");
            portable::println("{}", waveshare::dump_command_line_options(options));
            portable::println("");
        }

        // Check if any action requires a Modbus connection
        bool needs_connection = false;
        for (const auto& action : options.actions) {
            if (action != waveshare::CommandLineAction::SCAN_NETWORK &&
                action != waveshare::CommandLineAction::SET_STATIC_IP &&
                action != waveshare::CommandLineAction::SET_DHCP &&
                action != waveshare::CommandLineAction::SET_MODBUS_TCP &&
                action != waveshare::CommandLineAction::SET_MODBUS_TCP_PORT &&
                action != waveshare::CommandLineAction::SET_NAME &&
                action != waveshare::CommandLineAction::NONE) {
                needs_connection = true;
                break;
            }
        }

        // When a Modbus connection is needed and --name or --mac was given
        // (but -i was not explicitly set), resolve the IP via a network scan.
        if (needs_connection &&
            !options.ip_explicitly_set &&
            (!options.target_mac.empty() || !options.target_name.empty()))
        {
            auto devices = waveshare::scan_network(options.scan_timeout_ms,
                                                   options.debug, "");
            std::string error;
            auto* dev = waveshare::resolve_target_device(
                devices, options.target_mac, options.target_name, "", error);
            if (!dev) {
                portable::println(stderr, "{}", error);
                return EXIT_FAILURE;
            }
            portable::println("Resolved device: {} ({}) at {}",
                              dev->device_name, dev->mac_address, dev->ip_address);
            options.ip_address = dev->ip_address;
            // Use the device's port if no explicit -p was given and the
            // device has a known port
            if (options.port == 502 && dev->port != 0) {
                options.port = dev->port;
            }
        }

        // Create and connect to device (only if needed)
        std::optional<libmodbus_cpp::ModbusConnection> conn;
        if (needs_connection) {
            conn = waveshare::create_modbus_connection(options.ip_address, options.port, options.timeout_seconds);

            if (options.debug)
            {
                portable::println("Connected successfully!");
                portable::println("");
            }
        }

        // ── Helpers for the action loop ────────────────────────────────

        // MAC address locked after first VirCom resolution so that chained
        // commands survive name/IP changes and reboots.
        std::string resolved_mac;

        // Scan the network and resolve the target device.  If resolved_mac
        // is already set and the device isn't found immediately, keep
        // retrying (the device may still be rebooting from a prior config).
        auto resolve_device = [&](std::vector<waveshare::DiscoveredDevice>& devices)
            -> const waveshare::DiscoveredDevice*
        {
            std::string target;
            if (options.ip_explicitly_set) target = options.ip_address;

            devices = waveshare::scan_network(options.scan_timeout_ms,
                                              options.debug, target);

            std::string mac  = !resolved_mac.empty() ? resolved_mac : options.target_mac;
            std::string name = !resolved_mac.empty() ? ""           : options.target_name;
            std::string ip   = !resolved_mac.empty() ? ""
                             : (options.ip_explicitly_set ? options.ip_address : "");

            std::string error;
            const auto* dev = waveshare::resolve_target_device(
                devices, mac, name, ip, error);

            if (!dev && !resolved_mac.empty()) {
                auto reappeared = waveshare::wait_for_device_reboot(
                    resolved_mac, options.wait_timeout_ms, options.debug);
                if (reappeared) {
                    devices = {*reappeared};
                    return &devices[0];
                }
                portable::println(stderr, "Device {} did not reappear.", resolved_mac);
                return nullptr;
            }
            if (!dev) {
                portable::println(stderr, "{}", devices.empty()
                    ? "No devices found on the network." : error);
                return nullptr;
            }
            if (resolved_mac.empty()) resolved_mac = dev->mac_address;
            return dev;
        };

        // Resolve target, apply a VirCom configuration, wait for reboot.
        // `configure` receives the resolved device and returns true on success.
        auto resolve_configure_wait = [&](
            std::function<bool(const waveshare::DiscoveredDevice&)> configure) -> int
        {
            std::vector<waveshare::DiscoveredDevice> devices;
            const auto* dev = resolve_device(devices);
            if (!dev) return EXIT_FAILURE;

            if (!configure(*dev)) {
                portable::println(stderr, "Failed to send configuration.");
                return EXIT_FAILURE;
            }

            auto reappeared = waveshare::wait_for_device_reboot(
                dev->mac_address, options.wait_timeout_ms, options.debug);
            return reappeared ? EXIT_SUCCESS : EXIT_FAILURE;
        };

        // Execute a Modbus operation on each element of `args`, converting
        // the first field to a numeric address.
        auto for_each_addr = [](const auto& args, auto&& body) {
            for (const auto& a : args) {
                try { body(a); }
                catch (const std::exception& e) { portable::println("Error: {}", e.what()); }
            }
        };

        // ── Action loop ────────────────────────────────────────────────

        for (const auto &action : options.actions)
        {
            switch (action)
            {
            case waveshare::CommandLineAction::READ_COIL:
                portable::println("=== Read Coil ===");
                for_each_addr(options.read_coil_args, [&](const auto& args) {
                    auto addr = std::stoi(args.address, nullptr, 0);
                    bool value;
                    if (conn->read_coil(static_cast<uint16_t>(addr), value))
                        portable::println("Coil 0x{:04X}: {} ({})", addr, value ? "ON" : "OFF", value);
                    else
                        portable::println("Failed to read coil 0x{:04X}: {}", addr, conn->get_last_error());
                });
                break;

            case waveshare::CommandLineAction::READ_COILS:
                portable::println("=== Read Coils ===");
                for_each_addr(options.read_coils_args, [&](const auto& args) {
                    auto addr = std::stoi(args.address, nullptr, 0);
                    auto count = std::stoi(args.count, nullptr, 0);
                    std::vector<uint8_t> values(count);
                    if (conn->read_coils(static_cast<uint16_t>(addr), static_cast<uint16_t>(count), values.data())) {
                        portable::println("Read {} coils starting at 0x{:04X}:", count, addr);
                        for (int i = 0; i < count; ++i) {
                            bool bit = (values[i / 8] & (1 << (i % 8))) != 0;
                            portable::println("  Coil 0x{:04X} ({}): {} ({})", addr + i, addr + i, bit ? "ON" : "OFF", bit);
                        }
                    } else {
                        portable::println("Failed to read coils 0x{:04X}-0x{:04X}: {}", addr, addr + count - 1, conn->get_last_error());
                    }
                });
                break;

            case waveshare::CommandLineAction::WRITE_COIL:
                portable::println("=== Write Coil ===");
                for (const auto &args : options.write_coil_args)
                    execute_write_coil(*conn, args);
                break;

            case waveshare::CommandLineAction::WRITE_COILS:
                portable::println("=== Write Coil Pairs ===");
                for (const auto &args : options.write_coils_args)
                    execute_write_coil(*conn, args);
                break;

            case waveshare::CommandLineAction::READ_REGISTER:
                portable::println("=== Read Register ===");
                for_each_addr(options.read_register_args, [&](const auto& args) {
                    auto addr = std::stoi(args.address, nullptr, 0);
                    uint16_t value;
                    if (conn->read_register(static_cast<uint16_t>(addr), value))
                        portable::println("Register 0x{:04X}: {} (0x{:04X})", addr, value, value);
                    else
                        portable::println("Failed to read register 0x{:04X}: {}", addr, conn->get_last_error());
                });
                break;

            case waveshare::CommandLineAction::READ_REGISTERS:
                portable::println("=== Read Registers ===");
                for_each_addr(options.read_registers_args, [&](const auto& args) {
                    auto addr = std::stoi(args.address, nullptr, 0);
                    auto count = std::stoi(args.count, nullptr, 0);
                    std::vector<uint16_t> values(count);
                    if (conn->read_registers(static_cast<uint16_t>(addr), static_cast<uint16_t>(count), values.data())) {
                        portable::println("Read {} registers starting at 0x{:04X}:", count, addr);
                        for (int i = 0; i < count; ++i)
                            portable::println("  Register 0x{:04X}: {} (0x{:04X})", addr + i, values[i], values[i]);
                    } else {
                        portable::println("Failed to read registers 0x{:04X}-0x{:04X}: {}", addr, addr + count - 1, conn->get_last_error());
                    }
                });
                break;

            case waveshare::CommandLineAction::WRITE_REGISTER:
                portable::println("=== Write Register ===");
                for_each_addr(options.write_register_args, [&](const auto& args) {
                    auto addr  = std::stoi(args.address, nullptr, 0);
                    auto value = std::stoi(args.value, nullptr, 0);
                    if (conn->write_register(static_cast<uint16_t>(addr), static_cast<uint16_t>(value)))
                        portable::println("Register 0x{:04X} = {} (0x{:04X}) (SUCCESS)", addr, value, value);
                    else
                        portable::println("Register 0x{:04X} = {} (FAILED): {}", addr, value, conn->get_last_error());
                });
                break;

            case waveshare::CommandLineAction::WRITE_REGISTERS:
                portable::println("=== Write Registers ===");
                for_each_addr(options.write_registers_args, [&](const auto& args) {
                    auto addr = std::stoi(args.address, nullptr, 0);
                    std::vector<uint16_t> values;
                    for (const auto& v : args.values)
                        values.push_back(static_cast<uint16_t>(std::stoi(v, nullptr, 0)));
                    auto count = values.size();
                    if (conn->write_registers(static_cast<uint16_t>(addr), static_cast<uint16_t>(count), values.data())) {
                        portable::println("Successfully wrote {} registers starting at 0x{:04X}:", count, addr);
                        for (std::size_t i = 0; i < count; ++i)
                            portable::println("  Register 0x{:04X}: {} (0x{:04X})", addr + i, values[i], values[i]);
                    } else {
                        portable::println("Failed to write registers starting at 0x{:04X}: {}", addr, conn->get_last_error());
                    }
                });
                break;

            case waveshare::CommandLineAction::READ_DIGITAL_INPUTS:
            {
                portable::println("=== Read Digital Inputs ===");
                constexpr uint16_t di_count = 8;
                uint8_t values[di_count]{};
                if (conn->read_discrete_inputs(0x0000, di_count, values)) {
                    // Header row
                    std::string header, states;
                    for (uint16_t i = 0; i < di_count; ++i) {
                        if (i > 0) { header += '\t'; states += '\t'; }
                        header += std::format("DI{}", i + 1);
                        states += (values[i] ? "ON" : "OFF");
                    }
                    portable::println("{}", header);
                    portable::println("{}", states);
                } else {
                    portable::println("Failed to read digital inputs: {}", conn->get_last_error());
                }
                break;
            }

            case waveshare::CommandLineAction::ITERATE_RELAY_SWITCHES:
            {
                portable::println("=== Iterate Relay Switches (Ctrl-C to stop) ===");

                // Install SIGINT handler
                g_interrupted.store(false);
                auto prev_handler = std::signal(SIGINT, sigint_handler);

                // Turn all relays off first (address 0x00FF = all relays)
                if (!conn->write_coil(0x00FF, false))
                {
                    portable::println("FAILED to turn all relays off: {}", conn->get_last_error());
                    break;
                }
                portable::println("All relays OFF");

                constexpr int NUM_COILS = 8;
                constexpr auto ON_DURATION = std::chrono::seconds(1);
                constexpr auto PAUSE_BETWEEN_CYCLES = std::chrono::seconds(3);

                while (!g_interrupted.load(std::memory_order_relaxed))
                {
                    for (int i = 0; i < NUM_COILS && !g_interrupted.load(std::memory_order_relaxed); ++i)
                    {
                        uint16_t addr = static_cast<uint16_t>(i);

                        // Turn coil on
                        if (!conn->write_coil(addr, true))
                        {
                            portable::println("FAILED to turn coil {} ON: {}", i + 1, conn->get_last_error());
                            continue;
                        }
                        portable::println("Coil {} ON", i + 1);

                        // Wait 1 second (check for interrupt every 100ms)
                        for (int t = 0; t < 10 && !g_interrupted.load(std::memory_order_relaxed); ++t)
                        {
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }

                        // Turn coil off
                        if (!conn->write_coil(addr, false))
                        {
                            portable::println("FAILED to turn coil {} OFF: {}", i + 1, conn->get_last_error());
                        }
                        else
                        {
                            portable::println("Coil {} OFF", i + 1);
                        }
                    }

                    if (g_interrupted.load(std::memory_order_relaxed))
                        break;

                    portable::println("--- Cycle complete, waiting 3 seconds ---");

                    // Wait 3 seconds between cycles (check for interrupt every 100ms)
                    for (int t = 0; t < 30 && !g_interrupted.load(std::memory_order_relaxed); ++t)
                    {
                        std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    }
                }

                // Ensure all relays are off on exit
                portable::println("\nInterrupted — turning all relays OFF ...");
                if (conn->write_coil(0x00FF, false))
                {
                    portable::println("All relays OFF (safe shutdown)");
                }
                else
                {
                    portable::println("WARNING: failed to turn all relays off: {}", conn->get_last_error());
                }

                // Restore previous signal handler
                std::signal(SIGINT, prev_handler);
                break;
            }

            case waveshare::CommandLineAction::NONE:
                portable::println("No action specified. Use --help to see available options.");
                break;

            case waveshare::CommandLineAction::SCAN_NETWORK:
            {
                portable::println("=== Scanning network for Waveshare devices ===");
                // Pass the user-specified IP as a unicast probe target.
                // This ensures discovery works even from NATed environments
                // (e.g. WSL2) where broadcasts don't reach the physical LAN.
                std::string target;
                if (options.ip_explicitly_set) {
                    target = options.ip_address;
                }
                auto devices = waveshare::scan_network(options.scan_timeout_ms,
                                                       options.debug,
                                                       target);
                portable::println("{}", waveshare::format_device_table(devices));
                break;
            }

            case waveshare::CommandLineAction::SET_STATIC_IP:
            {
                portable::println("=== Set Static IP ===");
                auto rc = resolve_configure_wait([&](const waveshare::DiscoveredDevice& dev) {
                    portable::println("Target: {} ({}, {})",
                                      dev.device_name, dev.mac_address, dev.ip_address);
                    if (!waveshare::set_device_ip(dev, options.set_ip_address,
                                                  options.set_subnet_mask, options.set_gateway,
                                                  options.set_dns, options.debug))
                        return false;
                    portable::println("Static IP configuration sent to device {}.", dev.mac_address);
                    portable::println("New IP: {}, Mask: {}, Gateway: {}, DNS: {}",
                                      options.set_ip_address, options.set_subnet_mask,
                                      options.set_gateway, options.set_dns);
                    return true;
                });
                if (rc != EXIT_SUCCESS) return rc;
                break;
            }

            case waveshare::CommandLineAction::SET_DHCP:
            {
                portable::println("=== Set DHCP Mode ===");

                std::vector<waveshare::DiscoveredDevice> devices;
                const auto* target_dev = resolve_device(devices);
                if (!target_dev) return EXIT_FAILURE;

                portable::println("Switching device {} ({}) to DHCP mode ...",
                                  target_dev->mac_address, target_dev->ip_address);

                auto result = waveshare::set_device_dhcp(*target_dev,
                                                         options.wait_timeout_ms,
                                                         options.debug);
                if (!result.empty()) {
                    portable::println("Device is now at {} (DHCP)", result[0].ip_address);
                    portable::println("{}", waveshare::format_device_table(result));
                }
                break;
            }

            case waveshare::CommandLineAction::SET_MODBUS_TCP:
            {
                portable::println("=== Set Modbus TCP Protocol ===");
                auto rc = resolve_configure_wait([&](const waveshare::DiscoveredDevice& dev) {
                    if (!waveshare::set_device_modbus_tcp(
                            dev, static_cast<uint16_t>(options.modbus_tcp_port), options.debug))
                        return false;
                    portable::println("Modbus TCP configuration sent to device {}.", dev.mac_address);
                    portable::println("Protocol: Modbus TCP, Work Mode: TCP Server, Port: {}",
                                      options.modbus_tcp_port);
                    return true;
                });
                if (rc != EXIT_SUCCESS) return rc;
                break;
            }

            case waveshare::CommandLineAction::SET_MODBUS_TCP_PORT:
            {
                portable::println("=== Set Modbus TCP Port ===");
                auto rc = resolve_configure_wait([&](const waveshare::DiscoveredDevice& dev) {
                    if (!waveshare::set_device_port(
                            dev, static_cast<uint16_t>(options.set_port_value), options.debug))
                        return false;
                    portable::println("Port changed to {} on device {}.",
                                      options.set_port_value, dev.mac_address);
                    return true;
                });
                if (rc != EXIT_SUCCESS) return rc;
                break;
            }

            case waveshare::CommandLineAction::SET_NAME:
            {
                if (options.set_name.size() > 9) {
                    portable::println(stderr, "Error: Device name '{}' is too long (max 9 characters, got {}).",
                                      options.set_name, options.set_name.size());
                    return EXIT_FAILURE;
                }
                auto rc = resolve_configure_wait([&](const waveshare::DiscoveredDevice& dev) {
                    if (!waveshare::set_device_name(dev, options.set_name, options.debug))
                        return false;
                    portable::println("Device name set to '{}' on device {}.",
                                      options.set_name, dev.mac_address);
                    return true;
                });
                if (rc != EXIT_SUCCESS) return rc;
                break;
            }
            }
        }

        return EXIT_SUCCESS;
    }
    catch (const std::exception &e)
    {
        portable::println(stderr, "Error: {}", e.what());
        return EXIT_FAILURE;
    }
}
