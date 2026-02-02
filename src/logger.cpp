#include "logger.h"
#include <stdarg.h>

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::begin() {
    _startTime = millis();
    _logBuffer.reserve(MAX_LOG_ENTRIES);
}

void Logger::debug(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logInternal(LogLevel::DEBUG, format, args);
    va_end(args);
}

void Logger::info(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logInternal(LogLevel::INFO, format, args);
    va_end(args);
}

void Logger::warn(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logInternal(LogLevel::WARN, format, args);
    va_end(args);
}

void Logger::error(const char* format, ...) {
    va_list args;
    va_start(args, format);
    logInternal(LogLevel::ERROR, format, args);
    va_end(args);
}

void Logger::log(LogLevel level, const char* format, ...) {
    va_list args;
    va_start(args, format);
    logInternal(level, format, args);
    va_end(args);
}

void Logger::raw(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    String message(buffer);

    // Output to Serial without prefix
    if (_serialEnabled && Serial) {
        Serial.print(message);
    }

    // Store in buffer (trim trailing newlines for cleaner display)
    message.trim();
    if (message.length() > 0) {
        addToBuffer(LogLevel::INFO, message);
    }
}

void Logger::logInternal(LogLevel level, const char* format, va_list args) {
    char buffer[256];
    vsnprintf(buffer, sizeof(buffer), format, args);

    String message(buffer);

    // Output to Serial with prefix
    if (_serialEnabled && Serial && level >= _serialLogLevel) {
        unsigned long elapsed = millis() - _startTime;
        Serial.printf("[%lu.%03lu] [%s] %s\n",
                      elapsed / 1000,
                      elapsed % 1000,
                      levelToShortString(level),
                      buffer);
    }

    // Store in buffer
    if (level >= _bufferLogLevel) {
        addToBuffer(level, message);
    }
}

void Logger::addToBuffer(LogLevel level, const String& message) {
    LogEntry entry;
    entry.timestamp = millis();
    entry.level = level;
    entry.message = message;

    // Circular buffer: remove oldest if full
    if (_logBuffer.size() >= MAX_LOG_ENTRIES) {
        _logBuffer.erase(_logBuffer.begin());
    }

    _logBuffer.push_back(entry);
    _totalCount++;
}

const char* Logger::levelToString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        default:              return "UNKNOWN";
    }
}

const char* Logger::levelToShortString(LogLevel level) {
    switch (level) {
        case LogLevel::DEBUG: return "D";
        case LogLevel::INFO:  return "I";
        case LogLevel::WARN:  return "W";
        case LogLevel::ERROR: return "E";
        default:              return "?";
    }
}

std::vector<LogEntry> Logger::getRecentLogs(int count) {
    std::vector<LogEntry> result;

    int start = 0;
    if ((int)_logBuffer.size() > count) {
        start = _logBuffer.size() - count;
    }

    for (int i = start; i < (int)_logBuffer.size(); i++) {
        result.push_back(_logBuffer[i]);
    }

    return result;
}

std::vector<LogEntry> Logger::getLogsSince(unsigned long sinceIndex, int maxCount) {
    std::vector<LogEntry> result;

    // Calculate how many logs we've received since sinceIndex
    if (_totalCount <= sinceIndex) {
        return result;  // No new logs
    }

    unsigned long newLogs = _totalCount - sinceIndex;

    // Calculate starting position in buffer
    int bufferSize = _logBuffer.size();
    int startPos = bufferSize - (int)newLogs;
    if (startPos < 0) startPos = 0;

    // Limit to maxCount
    if (bufferSize - startPos > maxCount) {
        startPos = bufferSize - maxCount;
    }

    for (int i = startPos; i < bufferSize; i++) {
        result.push_back(_logBuffer[i]);
    }

    return result;
}

void Logger::clearLogs() {
    _logBuffer.clear();
    // Don't reset _totalCount so clients can detect the clear
}
