#pragma once

#include <iostream>
#include <string>
#include <mutex>
#include <atomic>

enum class LogLevel {
    TRACE = 0,   // 가장 상세한 디버깅 정보
    DEBUG = 1,   // 디버깅에 유용한 정보
    INFO = 2,    // 일반적인 정보성 메시지
    WARN = 3,    // 경고 메시지
    ERROR = 4,   // 에러 메시지
    FATAL = 5    // 치명적 오류
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void setLogLevel(LogLevel level) {
        current_level_.store(level);
    }

    LogLevel getLogLevel() const {
        return current_level_.load();
    }

    template<typename... Args>
    void log(LogLevel level, const char* file, int line, Args... args) {
        if (level >= current_level_.load()) {
            std::lock_guard<std::mutex> lock(mutex_);
            std::cout << "[" << getLevelString(level) << "] "
                     << "[" << file << ":" << line << "] ";
            (std::cout << ... << args) << std::endl;
        }
    }

private:
    Logger() {
        // LOG_LEVEL 매크로에 따라 로그 레벨 설정
        #if defined(LOG_LEVEL)
            switch (LOG_LEVEL) {
                case 0: current_level_ = LogLevel::TRACE; break;
                case 1: current_level_ = LogLevel::DEBUG; break;
                case 2: current_level_ = LogLevel::INFO; break;
                case 3: current_level_ = LogLevel::WARN; break;
                case 4: current_level_ = LogLevel::ERROR; break;
                case 5: current_level_ = LogLevel::FATAL; break;
                default: current_level_ = LogLevel::INFO; break;
            }
        #else
            current_level_ = LogLevel::INFO;
        #endif
    }
    
    std::atomic<LogLevel> current_level_;
    std::mutex mutex_;

    const char* getLevelString(LogLevel level) {
        switch (level) {
            case LogLevel::TRACE: return "TRACE";
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default:              return "UNKNOWN";
        }
    }
};

#define LOG_TRACE(...) Logger::getInstance().log(LogLevel::TRACE, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_DEBUG(...) Logger::getInstance().log(LogLevel::DEBUG, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_INFO(...)  Logger::getInstance().log(LogLevel::INFO,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_WARN(...)  Logger::getInstance().log(LogLevel::WARN,  __FILE__, __LINE__, __VA_ARGS__)
#define LOG_ERROR(...) Logger::getInstance().log(LogLevel::ERROR, __FILE__, __LINE__, __VA_ARGS__)
#define LOG_FATAL(...) Logger::getInstance().log(LogLevel::FATAL, __FILE__, __LINE__, __VA_ARGS__)