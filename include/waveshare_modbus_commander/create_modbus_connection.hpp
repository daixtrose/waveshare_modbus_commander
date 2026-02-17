#ifndef WAVESHARE_CREATE_MODBUS_CONNECTION_HPP
#define WAVESHARE_CREATE_MODBUS_CONNECTION_HPP

#include <string>

namespace caparoc {
class ModbusConnection;
}

namespace waveshare {

/**
 * @brief Create and establish a Modbus TCP connection
 * 
 * @param ip_address IP address of the device
 * @param port Modbus TCP port
 * @param timeout_seconds Connection timeout in seconds
 * @return caparoc::ModbusConnection Connected ModbusConnection object
 * @throws std::runtime_error if connection fails
 */
caparoc::ModbusConnection create_modbus_connection(const std::string& ip_address, int port, int timeout_seconds);

} // namespace waveshare

#endif  // WAVESHARE_CREATE_MODBUS_CONNECTION_HPP
