#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <vector>
#include <functional>

/**
 * Log severity levels, ordered from least to most severe.
 * Used to filter which messages are output to Serial or stored in buffer.
 */
enum class LogLevel {
    DEBUG,  ///< Detailed diagnostic information
    INFO,   ///< General operational messages
    WARN,   ///< Warning conditions that may need attention
    ERROR   ///< Error conditions that affect operation
};

/**
 * A single log entry stored in the circular buffer.
 */
struct LogEntry {
    unsigned long timestamp;  ///< Milliseconds since boot (from millis())
    LogLevel level;           ///< Severity level of the message
    String message;           ///< The log message content
};

/**
 * Central logging facility for TinkLink-USB.
 *
 * Provides dual-output logging: messages are sent to Serial (when CDC is
 * available) and stored in a circular buffer for retrieval via web console.
 *
 * Usage:
 *   Logger::instance().begin();  // Call once in setup()
 *   LOG_INFO("System started");  // Use convenience macros
 *   LOG_DEBUG("Value: %d", val);
 *
 * The buffer holds up to MAX_LOG_ENTRIES messages. When full, oldest entries
 * are removed to make room for new ones.
 */
class Logger {
public:
    /**
     * Get the singleton Logger instance.
     * @return Reference to the global Logger
     */
    static Logger& instance();

    /**
     * Initialize the logger. Call once in setup() before logging.
     * Records the start time for relative timestamps.
     */
    void begin();

    /**
     * Log a message at DEBUG level.
     * @param format printf-style format string
     * @param ... Format arguments
     */
    void debug(const char* format, ...);

    /**
     * Log a message at INFO level.
     * @param format printf-style format string
     * @param ... Format arguments
     */
    void info(const char* format, ...);

    /**
     * Log a message at WARN level.
     * @param format printf-style format string
     * @param ... Format arguments
     */
    void warn(const char* format, ...);

    /**
     * Log a message at ERROR level.
     * @param format printf-style format string
     * @param ... Format arguments
     */
    void error(const char* format, ...);

    /**
     * Log a message at the specified level.
     * @param level The severity level
     * @param format printf-style format string
     * @param ... Format arguments
     */
    void log(LogLevel level, const char* format, ...);

    /**
     * Log raw output without timestamp or level prefix.
     * Useful for banners, separators, or pre-formatted text.
     * Messages are still stored in the buffer (as INFO level).
     * @param format printf-style format string
     * @param ... Format arguments
     */
    void raw(const char* format, ...);

    /**
     * Get the most recent log entries.
     * @param count Maximum number of entries to return (default 50)
     * @return Vector of log entries, oldest first
     */
    std::vector<LogEntry> getRecentLogs(int count = 50);

    /**
     * Get log entries added since a specific index.
     * Used for incremental updates in web console polling.
     *
     * @param sinceIndex The _totalCount value from a previous call to getLogCount()
     * @param maxCount Maximum entries to return (default 50)
     * @return Vector of new log entries since sinceIndex, oldest first.
     *         Empty if no new logs or if sinceIndex >= current count.
     */
    std::vector<LogEntry> getLogsSince(unsigned long sinceIndex, int maxCount = 50);

    /**
     * Get the total number of log messages ever recorded.
     * This counter never resets (except on reboot) and can be used with
     * getLogsSince() to detect new messages.
     * @return Total log count since boot
     */
    unsigned long getLogCount() const { return _totalCount; }

    /**
     * Clear all entries from the log buffer.
     * Note: _totalCount is preserved so clients can detect the clear.
     */
    void clearLogs();

    /**
     * Enable or disable Serial output.
     * Disable when USB OTG mode is active (no CDC available).
     * @param enabled true to enable Serial output, false to disable
     */
    void setSerialEnabled(bool enabled) { _serialEnabled = enabled; }

    /** @return true if Serial output is enabled */
    bool isSerialEnabled() const { return _serialEnabled; }

    /**
     * Set minimum log level for Serial output.
     * Messages below this level are not printed to Serial.
     * @param level Minimum level to output
     */
    void setSerialLogLevel(LogLevel level) { _serialLogLevel = level; }

    /**
     * Set minimum log level for buffer storage.
     * Messages below this level are not stored in the buffer.
     * @param level Minimum level to store
     */
    void setBufferLogLevel(LogLevel level) { _bufferLogLevel = level; }

private:
    Logger() = default;
    ~Logger() = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void logInternal(LogLevel level, const char* format, va_list args);
    void addToBuffer(LogLevel level, const String& message);
    const char* levelToString(LogLevel level);
    const char* levelToShortString(LogLevel level);

    static const int MAX_LOG_ENTRIES = 100;
    std::vector<LogEntry> _logBuffer;
    unsigned long _totalCount = 0;

    bool _serialEnabled = true;
    LogLevel _serialLogLevel = LogLevel::DEBUG;
    LogLevel _bufferLogLevel = LogLevel::DEBUG;

    unsigned long _startTime = 0;
};

// Convenience macros for logging
#define LOG_DEBUG(...) Logger::instance().debug(__VA_ARGS__)
#define LOG_INFO(...)  Logger::instance().info(__VA_ARGS__)
#define LOG_WARN(...)  Logger::instance().warn(__VA_ARGS__)
#define LOG_ERROR(...) Logger::instance().error(__VA_ARGS__)
#define LOG_RAW(...)   Logger::instance().raw(__VA_ARGS__)

#endif // LOGGER_H
