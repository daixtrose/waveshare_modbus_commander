#include "waveshare_modbus_commander/cli_parser.hpp"
#include "CLI/CLI.hpp"

#include <format>

namespace waveshare
{
    namespace
    {
        std::string action_to_string(CommandLineAction action)
        {
            switch (action)
            {
            case CommandLineAction::NONE:
                return "NONE";
            case CommandLineAction::READ_COIL:
                return "READ_COIL";
            case CommandLineAction::READ_COILS:
                return "READ_COILS";
            case CommandLineAction::WRITE_COIL:
                return "WRITE_COIL";
            case CommandLineAction::WRITE_COILS:
                return "WRITE_COILS";
            case CommandLineAction::READ_REGISTER:
                return "READ_REGISTER";
            case CommandLineAction::READ_REGISTERS:
                return "READ_REGISTERS";
            case CommandLineAction::WRITE_REGISTER:
                return "WRITE_REGISTER";
            case CommandLineAction::WRITE_REGISTERS:
                return "WRITE_REGISTERS";
            case CommandLineAction::ITERATE_RELAY_SWITCHES:
                return "ITERATE_RELAY_SWITCHES";
            case CommandLineAction::SCAN_NETWORK:
                return "SCAN_NETWORK";
            case CommandLineAction::SET_STATIC_IP:
                return "SET_STATIC_IP";
            case CommandLineAction::SET_DHCP:
                return "SET_DHCP";
            case CommandLineAction::SET_MODBUS_TCP:
                return "SET_MODBUS_TCP";
            case CommandLineAction::SET_MODBUS_TCP_PORT:
                return "SET_MODBUS_TCP_PORT";
            case CommandLineAction::SET_NAME:
                return "SET_NAME";
            }
            return "UNKNOWN";
        }
    }

    CommandLineOptions parse_command_line(int argc, char *argv[])
    {
        // Default values
        CommandLineOptions options;

        CLI::App app{"Waveshare Modbus Commander - Universal Modbus TCP tool for coil and register operations"};
        app.set_help_flag("-h,--help", "Show all available options");

        app.add_option("-i,--ip", options.ip_address, "IP address of the Modbus device")
            ->default_val("192.168.1.2")
            ->each([&options](const std::string&) { options.ip_explicitly_set = true; });
        app.add_option("-p,--port", options.port, "Modbus TCP port")
            ->default_val(502);
        app.add_option("-t,--timeout", options.timeout_seconds, "Connection timeout in seconds")
            ->default_val(3);
        app.add_flag("-d,--debug", options.debug, "Enable debug output")
            ->default_val(false);

        std::vector<std::string> write_coils_args_raw;
        std::vector<std::string> write_registers_args_raw;

        // Coil operations
        auto read_coil_option = app.add_option("--read-coil", "Read single coil status (address)")
                                    ->expected(1);
        auto read_coils_option = app.add_option("--read-coils", "Read multiple coils (address count)")
                                     ->expected(2);
        auto write_coil_option = app.add_option("--write-coil", "Write single coil (address state) - state: on|off|true|false|1|0")
                                     ->expected(2);
        auto write_coils_option = app.add_option("--write-coils", write_coils_args_raw,
                                      "Write multiple coil address/state pairs (address1 state1 [address2 state2 ...])")
                          ->expected(2, -1);

        // Register operations
        auto read_register_option = app.add_option("--read-register", "Read single holding register (address)")
                                        ->expected(1);
        auto read_registers_option = app.add_option("--read-registers", "Read multiple holding registers (address count)")
                                         ->expected(2);
        auto write_register_option = app.add_option("--write-register", "Write single holding register (address value)")
                                         ->expected(2);
        auto write_registers_option = app.add_option("--write-registers", write_registers_args_raw,
                                 "Write multiple holding registers (address value1 value2 ...)")
                          ->expected(2, -1);

        app.add_flag_callback("--iterate-relais-switches", [&options]()
                              { options.actions.push_back(CommandLineAction::ITERATE_RELAY_SWITCHES); },
                              "Iterate through relay switches: turn each coil on for 1s in sequence, repeat until Ctrl-C");

        app.add_flag_callback("--scan-network", [&options]()
                              { options.actions.push_back(CommandLineAction::SCAN_NETWORK); },
                              "Scan the local network for Waveshare serial server devices via UDP broadcast");

        app.add_option("--scan-timeout", options.scan_timeout_ms,
                       "Timeout in milliseconds for network scan (default: 3000)")
            ->default_val(3000);

        app.add_option("--mac", options.target_mac,
                       "Target device MAC address (e.g. 28:80:ca:ea:41:f3) for --set-ip / --set-dhcp");

        std::vector<std::string> set_ip_args;
        auto set_ip_option = app.add_option("--set-ip", set_ip_args,
                                            "Set static IP on a device: <ip> <mask> <gateway> <dns>")
                                 ->expected(4);

        app.add_flag_callback("--set-dhcp", [&options]()
                              { options.actions.push_back(CommandLineAction::SET_DHCP); },
                              "Set a device to DHCP mode (use --mac to identify the target device)");

        app.add_option("--dhcp-wait-timeout", options.dhcp_wait_timeout_ms,
                       "How long to wait (ms) for DHCP IP assignment after --set-dhcp (default: 30000)")
            ->default_val(30000);

        app.add_flag_callback("--set-modbus-tcp", [&options]()
                              { options.actions.push_back(CommandLineAction::SET_MODBUS_TCP); },
                              "Set a device to Modbus TCP protocol (TCP Server, use --mac to identify the target)");

        app.add_option("--modbus-tcp-port", options.modbus_tcp_port,
                       "Modbus TCP port for --set-modbus-tcp (default: 502)")
            ->default_val(502);

        auto set_port_option = app.add_option("--set-modbus-tcp-port", options.set_port_value,
                                              "Change only the listening port (use --mac to identify the target)");

        auto set_name_option = app.add_option("--set-name", options.set_name,
                                              "Set the device name (max 9 ASCII characters, use --mac to identify the target)");

        try
        {
            app.parse(argc, argv);
        }
        catch (const CLI::ParseError &e)
        {
            app.exit(e);
            exit(e.get_exit_code());
        }

        // Process coil operations
        if (read_coil_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::READ_COIL);
            auto results = read_coil_option->results();
            for (const auto& result : results)
            {
                options.read_coil_args.push_back({result});
            }
        }

        if (read_coils_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::READ_COILS);
            auto results = read_coils_option->results();
            for (std::size_t i = 0; i + 1 < results.size(); i += 2)
            {
                options.read_coils_args.push_back({results[i], results[i + 1]});
            }
        }

        if (write_coil_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::WRITE_COIL);
            auto results = write_coil_option->results();
            for (std::size_t i = 0; i + 1 < results.size(); i += 2)
            {
                options.write_coil_args.push_back({results[i], results[i + 1]});
            }
        }

        if (write_coils_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::WRITE_COILS);
            auto results = write_coils_option->results();
            if (results.size() % 2 != 0)
            {
                throw CLI::ValidationError("--write-coils", "requires an even number of values: address1 state1 [address2 state2 ...]");
            }

            for (std::size_t i = 0; i + 1 < results.size(); i += 2)
            {
                options.write_coils_args.push_back({results[i], results[i + 1]});
            }
        }

        // Process register operations
        if (read_register_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::READ_REGISTER);
            auto results = read_register_option->results();
            for (const auto& result : results)
            {
                options.read_register_args.push_back({result});
            }
        }

        if (read_registers_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::READ_REGISTERS);
            auto results = read_registers_option->results();
            for (std::size_t i = 0; i + 1 < results.size(); i += 2)
            {
                options.read_registers_args.push_back({results[i], results[i + 1]});
            }
        }

        if (write_register_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::WRITE_REGISTER);
            auto results = write_register_option->results();
            for (std::size_t i = 0; i + 1 < results.size(); i += 2)
            {
                options.write_register_args.push_back({results[i], results[i + 1]});
            }
        }

        if (write_registers_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::WRITE_REGISTERS);
            auto results = write_registers_option->results();
            if (results.size() >= 2)
            {
                RegistersWriteArgs args;
                args.address = results[0];
                for (std::size_t i = 1; i < results.size(); ++i)
                {
                    args.values.push_back(results[i]);
                }
                options.write_registers_args.push_back(args);
            }
        }

        // Process --set-ip (4 positional args: ip mask gateway dns)
        if (set_ip_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::SET_STATIC_IP);
            if (set_ip_args.size() == 4)
            {
                options.set_ip_address  = set_ip_args[0];
                options.set_subnet_mask = set_ip_args[1];
                options.set_gateway     = set_ip_args[2];
                options.set_dns         = set_ip_args[3];
            }
        }

        // Process --set-name
        if (set_name_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::SET_NAME);
        }

        // Process --set-modbus-tcp-port
        if (set_port_option->count() > 0)
        {
            options.actions.push_back(CommandLineAction::SET_MODBUS_TCP_PORT);
        }

        return options;
    }

    std::string dump_command_line_options(const CommandLineOptions &options)
    {
        std::string output;
        output += std::format("ip_address: {}\n", options.ip_address);
        output += std::format("port: {}\n", options.port);
        output += std::format("timeout_seconds: {}\n", options.timeout_seconds);
        output += std::format("debug: {}\n", options.debug);
        output += "actions:\n";
        if (options.actions.empty())
        {
            output += "  (none)\n";
        }
        else
        {
            for (const auto &action : options.actions)
            {
                output += std::format("  - {}\n", action_to_string(action));
            }
        }

        return output;
    }

} // namespace waveshare
