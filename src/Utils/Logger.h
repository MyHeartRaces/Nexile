#pragma once

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <memory>
#include <mutex>
#include <chrono>
#include <iomanip>
#include <filesystem>

namespace Nexile {

    enum class LogLevel {
        Debug,
        Info,
        Warning,
        Error,
        Critical
    };

    class Logger {
    public:
        static Logger& GetInstance() {
            static Logger instance;
            return instance;
        }

        void SetLogLevel(LogLevel level) {
            m_logLevel = level;
        }

        LogLevel GetLogLevel() const {
            return m_logLevel;
        }

        void SetLogToConsole(bool logToConsole) {
            m_logToConsole = logToConsole;
        }

        void SetLogToFile(bool logToFile, const std::string& filename = "") {
            m_logToFile = logToFile;
            if (!filename.empty()) {
                m_logFilename = filename;
                try {
                    std::filesystem::path logPath = std::filesystem::path(m_logFilename).parent_path();
                    if (!logPath.empty() && !std::filesystem::exists(logPath)) {
                        std::filesystem::create_directories(logPath);
                    }
                }
                catch (const std::exception& e) {
                    std::cerr << "Error creating log directory: " << e.what() << std::endl;
                }
            }
        }

        template<typename... Args>
        void Debug(const char* format, Args... args) {
            Log(LogLevel::Debug, format, args...);
        }

        template<typename... Args>
        void Info(const char* format, Args... args) {
            Log(LogLevel::Info, format, args...);
        }

        template<typename... Args>
        void Warning(const char* format, Args... args) {
            Log(LogLevel::Warning, format, args...);
        }

        template<typename... Args>
        void Error(const char* format, Args... args) {
            Log(LogLevel::Error, format, args...);
        }

        template<typename... Args>
        void Critical(const char* format, Args... args) {
            Log(LogLevel::Critical, format, args...);
        }

    private:
        Logger() : m_logLevel(LogLevel::Info), m_logToConsole(true), m_logToFile(false), m_logFilename("nexile.log") {}
        ~Logger() = default;

        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;

        template<typename T>
        void FormatArg(std::ostringstream& ss, T value) {
            ss << value;
        }

        template<typename... Args>
        void Log(LogLevel level, const char* format, Args... args) {
            if (level < m_logLevel) {
                return;
            }

            std::lock_guard<std::mutex> lock(m_mutex);

            std::ostringstream message;
            FormatString(message, format, args...);

            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

            std::ostringstream timestampSS;
            std::tm tm_now;
            localtime_s(&tm_now, &time_t_now);
            timestampSS << std::put_time(&tm_now, "%Y-%m-%d %H:%M:%S");
            timestampSS << '.' << std::setfill('0') << std::setw(3) << ms.count();
            std::string timestamp = timestampSS.str();

            std::string levelStr;
            switch (level) {
            case LogLevel::Debug:    levelStr = "DEBUG"; break;
            case LogLevel::Info:     levelStr = "INFO"; break;
            case LogLevel::Warning:  levelStr = "WARNING"; break;
            case LogLevel::Error:    levelStr = "ERROR"; break;
            case LogLevel::Critical: levelStr = "CRITICAL"; break;
            default:                 levelStr = "UNKNOWN"; break;
            }

            std::ostringstream fullMessage;
            fullMessage << "[" << timestamp << "] [" << levelStr << "] " << message.str();
            std::string output = fullMessage.str();

            if (m_logToConsole) {
                std::cout << output << std::endl;
            }

            if (m_logToFile) {
                std::ofstream logFile(m_logFilename, std::ios::app);
                if (logFile.is_open()) {
                    logFile << output << std::endl;
                    logFile.close();
                }
            }
        }

        template<typename T, typename... Args>
        void FormatString(std::ostringstream& ss, const char* format, T value, Args... args) {
            while (*format) {
                if (*format == '{' && *(format + 1) == '}') {
                    ss << value;
                    FormatString(ss, format + 2, args...);
                    return;
                }
                ss << *format++;
            }
        }

        void FormatString(std::ostringstream& ss, const char* format) {
            ss << format;
        }

        LogLevel m_logLevel;
        bool m_logToConsole;
        bool m_logToFile;
        std::string m_logFilename;
        std::mutex m_mutex;
    };

} // namespace Nexile

// Convenience macros
#define LOG_DEBUG(format, ...) Nexile::Logger::GetInstance().Debug(format, ##__VA_ARGS__)
#define LOG_INFO(format, ...) Nexile::Logger::GetInstance().Info(format, ##__VA_ARGS__)
#define LOG_WARNING(format, ...) Nexile::Logger::GetInstance().Warning(format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) Nexile::Logger::GetInstance().Error(format, ##__VA_ARGS__)
#define LOG_CRITICAL(format, ...) Nexile::Logger::GetInstance().Critical(format, ##__VA_ARGS__)