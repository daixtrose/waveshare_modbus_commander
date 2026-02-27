#include "waveshare_modbus_commander/create_modbus_connection.hpp"
#include "libmodbus_cpp/modbus_connection.hpp"
#include <modbus/modbus.h>

#include <format>
#include <stdexcept>

namespace waveshare {

libmodbus_cpp::ModbusConnection create_modbus_connection(const std::string& ip_address, int port, int timeout_seconds)
{
    libmodbus_cpp::ModbusConnection conn(ip_address, port);
    conn.set_response_timeout(timeout_seconds, 0);
    conn.set_slave_id(1);  // Default slave ID, can be made configurable if needed
    modbus_set_error_recovery(conn.get_context(), MODBUS_ERROR_RECOVERY_LINK);
    
    if (!conn.connect())
    {
        throw std::runtime_error(
            std::format(
                "Failed to connect to device: {}\n\nat {}:{}\n",
                conn.get_last_error(), ip_address, port));
    }
    
    return conn;
}

} // namespace waveshare
