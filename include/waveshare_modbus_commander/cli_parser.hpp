#ifndef WAVESHARE_CLI_PARSER_HPP
#define WAVESHARE_CLI_PARSER_HPP

#include <list>
#include <string>
#include <vector>

namespace waveshare {

enum class CommandLineAction {
    NONE,
    READ_COIL,
    READ_COILS,
    WRITE_COIL,
    WRITE_COILS,
    READ_REGISTER,
    READ_REGISTERS,
    WRITE_REGISTER,
    WRITE_REGISTERS
};

struct CoilReadArgs {
    std::string address;
};

struct CoilsReadArgs {
    std::string address;
    std::string count;
};

struct CoilWriteArgs {
    std::string address;
    std::string state;  // "on", "off", "true", "false", "1", "0"
};

struct RegisterReadArgs {
    std::string address;
};

struct RegistersReadArgs {
    std::string address;
    std::string count;
};

struct RegisterWriteArgs {
    std::string address;
    std::string value;
};

struct RegistersWriteArgs {
    std::string address;
    std::vector<std::string> values;
};

struct CommandLineOptions {
    std::string ip_address; 
    int port;
    int timeout_seconds;

    std::list<CommandLineAction> actions;

    std::vector<CoilReadArgs> read_coil_args;
    std::vector<CoilsReadArgs> read_coils_args;
    std::vector<CoilWriteArgs> write_coil_args;
    std::vector<CoilWriteArgs> write_coils_args;
    std::vector<RegisterReadArgs> read_register_args;
    std::vector<RegistersReadArgs> read_registers_args;
    std::vector<RegisterWriteArgs> write_register_args;
    std::vector<RegistersWriteArgs> write_registers_args;
    
    bool debug = false;
}; 

CommandLineOptions parse_command_line(int argc, char* argv[]);
std::string dump_command_line_options(const CommandLineOptions& options);
    
} // namespace waveshare

#endif  // WAVESHARE_CLI_PARSER_HPP
