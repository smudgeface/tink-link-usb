#ifndef UART_SERIAL_H
#define UART_SERIAL_H

#include <Arduino.h>
#include <HardwareSerial.h>
#include "SerialInterface.h"

/**
 * UART serial transport implementing SerialInterface.
 *
 * Wraps HardwareSerial for ESP32 UART communication. Used for
 * devices that communicate via RS-232 (e.g., Extron switchers).
 *
 * Usage:
 *   UartSerial uart(1, 44, 43, 9600);  // UART1, RX=44, TX=43
 *   uart.begin();
 *   uart.sendData("command\r\n");
 *   String line;
 *   if (uart.readLine(line)) {
 *       // Process line
 *   }
 */
class UartSerial : public SerialInterface {
public:
    /**
     * Create UART serial transport.
     * @param uartNum UART number (0, 1, or 2)
     * @param rxPin RX GPIO pin number
     * @param txPin TX GPIO pin number
     * @param baud Baud rate
     */
    UartSerial(uint8_t uartNum, uint8_t rxPin, uint8_t txPin, uint32_t baud);

    ~UartSerial();

    // SerialInterface overrides
    bool initTransport() override;
    void update() override;
    bool isConnected() const override;
    bool sendData(const String& data) override;
    bool readLine(String& line) override;
    size_t available() const override;  // Note: calls non-const HardwareSerial method

    /**
     * Stop UART and release resources.
     */
    void end();

private:
    HardwareSerial _hwSerial;
    uint8_t _rxPin;
    uint8_t _txPin;
    uint32_t _baud;
    bool _initialized;
    String _lineBuffer;
};

#endif // UART_SERIAL_H
