#include "caparoc/modbus_connection.hpp"
#include "waveshare_modbus_commander/cli_parser.hpp"
#include "waveshare_modbus_commander/create_modbus_connection.hpp"

#include <format>
#include <print>
#include <stdexcept>
#include <cstdlib>
#include <vector>

namespace
{
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

    void execute_write_coil(caparoc::ModbusConnection &conn, const waveshare::CoilWriteArgs &args)
    {
        try
        {
            auto addr = std::stoi(args.address, nullptr, 0);
            bool state = false;
            if (!try_parse_coil_state(args.state, state))
            {
                std::println("Invalid coil state '{}'. Use one of: on|off|true|false|1|0", args.state);
                return;
            }

            if (conn.write_coil(static_cast<uint16_t>(addr), state))
            {
                std::println("Coil 0x{:04X} = {} (SUCCESS)", addr, state ? "ON" : "OFF");
            }
            else
            {
                std::println("Coil 0x{:04X} = {} (FAILED): {}", addr, state ? "ON" : "OFF", conn.get_last_error());
            }
        }
        catch (const std::exception &e)
        {
            std::println("Error: {}", e.what());
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
            std::println("========================");
            std::println("Waveshare Modbus Commander");
            std::println("========================");
            std::println("Connecting to {}:{}", options.ip_address, options.port);
            std::println("");

            std::println("Command Line Options:");
            std::println("{}", waveshare::dump_command_line_options(options));
            std::println("");
        }

        // Create and connect to device
        auto conn = waveshare::create_modbus_connection(options.ip_address, options.port, options.timeout_seconds);

        if (options.debug)
        {
            std::println("Connected successfully!");
            std::println("");
        }

        // Process all requested actions
        for (const auto &action : options.actions)
        {
            switch (action)
            {
            case waveshare::CommandLineAction::READ_COIL:
                std::println("=== Read Coil ===");
                for (const auto &args : options.read_coil_args)
                {
                    try
                    {
                        auto addr = std::stoi(args.address, nullptr, 0);
                        bool value;
                        if (conn.read_coil(static_cast<uint16_t>(addr), value))
                        {
                            std::println("Coil 0x{:04X}: {} ({})", addr, value ? "ON" : "OFF", value);
                        }
                        else
                        {
                            std::println("Failed to read coil 0x{:04X}: {}", addr, conn.get_last_error());
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::println("Error: {}", e.what());
                    }
                }
                break;

            case waveshare::CommandLineAction::READ_COILS:
                std::println("=== Read Coils ===");
                for (const auto &args : options.read_coils_args)
                {
                    try
                    {
                        auto addr = std::stoi(args.address, nullptr, 0);
                        auto count = std::stoi(args.count, nullptr, 0);
                        
                        std::vector<uint8_t> values(count);
                        if (conn.read_coils(static_cast<uint16_t>(addr), static_cast<uint16_t>(count), values.data()))
                        {
                            std::println("Read {} coils starting at 0x{:04X}:", count, addr);
                            for (int i = 0; i < count; ++i)
                            {
                                bool bit_value = (values[i / 8] & (1 << (i % 8))) != 0;
                                std::println("  Coil 0x{:04X} ({}): {} ({})", addr + i, addr + i, bit_value ? "ON" : "OFF", bit_value);
                            }
                        }
                        else
                        {
                            std::println("Failed to read coils 0x{:04X}-0x{:04X}: {}", addr, addr + count - 1, conn.get_last_error());
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::println("Error: {}", e.what());
                    }
                }
                break;

            case waveshare::CommandLineAction::WRITE_COIL:
                std::println("=== Write Coil ===");
                for (const auto &args : options.write_coil_args)
                {
                    execute_write_coil(conn, args);
                }
                break;

            case waveshare::CommandLineAction::WRITE_COILS:
                std::println("=== Write Coil Pairs ===");
                for (const auto &args : options.write_coils_args)
                {
                    execute_write_coil(conn, args);
                }
                break;

            case waveshare::CommandLineAction::READ_REGISTER:
                std::println("=== Read Register ===");
                for (const auto &args : options.read_register_args)
                {
                    try
                    {
                        auto addr = std::stoi(args.address, nullptr, 0);
                        uint16_t value;
                        if (conn.read_register(static_cast<uint16_t>(addr), value))
                        {
                            std::println("Register 0x{:04X}: {} (0x{:04X})", addr, value, value);
                        }
                        else
                        {
                            std::println("Failed to read register 0x{:04X}: {}", addr, conn.get_last_error());
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::println("Error: {}", e.what());
                    }
                }
                break;

            case waveshare::CommandLineAction::READ_REGISTERS:
                std::println("=== Read Registers ===");
                for (const auto &args : options.read_registers_args)
                {
                    try
                    {
                        auto addr = std::stoi(args.address, nullptr, 0);
                        auto count = std::stoi(args.count, nullptr, 0);
                        
                        std::vector<uint16_t> values(count);
                        if (conn.read_registers(static_cast<uint16_t>(addr), static_cast<uint16_t>(count), values.data()))
                        {
                            std::println("Read {} registers starting at 0x{:04X}:", count, addr);
                            for (int i = 0; i < count; ++i)
                            {
                                std::println("  Register 0x{:04X}: {} (0x{:04X})", addr + i, values[i], values[i]);
                            }
                        }
                        else
                        {
                            std::println("Failed to read registers 0x{:04X}-0x{:04X}: {}", addr, addr + count - 1, conn.get_last_error());
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::println("Error: {}", e.what());
                    }
                }
                break;

            case waveshare::CommandLineAction::WRITE_REGISTER:
                std::println("=== Write Register ===");
                for (const auto &args : options.write_register_args)
                {
                    try
                    {
                        auto addr = std::stoi(args.address, nullptr, 0);
                        auto value = std::stoi(args.value, nullptr, 0);
                        
                        if (conn.write_register(static_cast<uint16_t>(addr), static_cast<uint16_t>(value)))
                        {
                            std::println("Register 0x{:04X} = {} (0x{:04X}) (SUCCESS)", addr, value, value);
                        }
                        else
                        {
                            std::println("Register 0x{:04X} = {} (FAILED): {}", addr, value, conn.get_last_error());
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::println("Error: {}", e.what());
                    }
                }
                break;

            case waveshare::CommandLineAction::WRITE_REGISTERS:
                std::println("=== Write Registers ===");
                for (const auto &args : options.write_registers_args)
                {
                    try
                    {
                        auto addr = std::stoi(args.address, nullptr, 0);
                        std::size_t count = args.values.size();
                        
                        std::vector<uint16_t> values;
                        for (const auto& val_str : args.values)
                        {
                            values.push_back(static_cast<uint16_t>(std::stoi(val_str, nullptr, 0)));
                        }
                        
                        if (conn.write_registers(static_cast<uint16_t>(addr), static_cast<uint16_t>(count), values.data()))
                        {
                            std::println("Successfully wrote {} registers starting at 0x{:04X}:", count, addr);
                            for (std::size_t i = 0; i < count; ++i)
                            {
                                std::println("  Register 0x{:04X}: {} (0x{:04X})", addr + i, values[i], values[i]);
                            }
                        }
                        else
                        {
                            std::println("Failed to write registers starting at 0x{:04X}: {}", addr, conn.get_last_error());
                        }
                    }
                    catch (const std::exception &e)
                    {
                        std::println("Error: {}", e.what());
                    }
                }
                break;

            case waveshare::CommandLineAction::NONE:
                std::println("No action specified. Use --help to see available options.");
                break;
            }
        }

        return EXIT_SUCCESS;
    }
    catch (const std::exception &e)
    {
        std::println(stderr, "Error: {}", e.what());
        return EXIT_FAILURE;
    }
}
