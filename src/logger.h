#ifndef LOGGER_H
#define LOGGER_H

#include <Arduino.h>
#include <vector>
#include <functional>

// Log levels
enum class LogLevel {
    DEBUG,
    INFO,
    WARN,
    ERROR
};

// Log entry with timestamp and level
struct LogEntry {
    unsigned long timestamp;
    LogLevel level;
    String message;
};

// Central logging facility
// Logs to Serial (when CDC available) and stores messages for web console
class Logger {
public:
    static Logger& instance();

    // Initialize logger (call in setup)
    void begin();

    // Log at various levels
    void debug(const char* format, ...);
    void info(const char* format, ...);
    void warn(const char* format, ...);
    void error(const char* format, ...);

    // Log with explicit level
    void log(LogLevel level, const char* format, ...);

    // Raw log (no timestamp/level prefix in buffer, but still stored)
    void raw(const char* format, ...);

    // Get recent log entries
    std::vector<LogEntry> getRecentLogs(int count = 50);

    // Get logs since a specific index (for incremental updates)
    std::vector<LogEntry> getLogsSince(unsigned long sinceIndex, int maxCount = 50);

    // Get total log count (for tracking new messages)
    unsigned long getLogCount() const { return _totalCount; }

    // Clear log buffer
    void clearLogs();

    // Enable/disable Serial output (for when USB OTG is enabled)
    void setSerialEnabled(bool enabled) { _serialEnabled = enabled; }
    bool isSerialEnabled() const { return _serialEnabled; }

    // Set minimum log level for Serial output
    void setSerialLogLevel(LogLevel level) { _serialLogLevel = level; }

    // Set minimum log level for buffer storage
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
