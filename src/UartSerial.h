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
    size_t read(uint8_t* buf, size_t maxLen) override;
    size_t write(const uint8_t* data, size_t length) override;
    size_t writeAvailable() const override;
    size_t available() const override;  // Note: calls non-const HardwareSerial method

    /**
     * Change the baud rate, applied immediately if the UART is running.
     * @param baud New baud rate (MIN_BAUD to MAX_BAUD)
     * @return false if the rate is out of range
     */
    bool setBaudRate(uint32_t baud) override;

    /** @return Configured baud rate */
    uint32_t getBaudRate() const override { return _baud; }

    /** Lowest supported baud rate */
    static const uint32_t MIN_BAUD = 300;
    /** Highest supported baud rate (ESP32 UART limit) */
    static const uint32_t MAX_BAUD = 5000000;

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

    // Lines longer than this are returned as-is so unterminated data can't grow the buffer without bound
    static const size_t MAX_LINE_LENGTH = 512;
};

#endif // UART_SERIAL_H
