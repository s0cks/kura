#ifndef KURA_LOG_H
#define KURA_LOG_H

#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <mutex>
#include <source_location>
#include <string_view>

namespace kura::log {
enum class LogLevel {
  DEBUG,
  INFO,
  WARNING,
  ERROR,
  FATAL,
};

class FileLogger {
 private:
  std::mutex mutex_{};
  std::ofstream file_;

  constexpr std::string_view level_to_string(LogLevel level) noexcept {
    switch (level) {
      case LogLevel::DEBUG:
        return "DEBUG";
      case LogLevel::INFO:
        return "INFO";
      case LogLevel::WARNING:
        return "WARN";
      case LogLevel::ERROR:
        return "ERROR";
      case LogLevel::FATAL:
        return "FATAL";
    }
  }

 public:
  explicit FileLogger(const std::string_view filename) {
    file_.open(filename.data(), std::ios::out | std::ios::app);
    if (!file_.is_open()) {
      std::cerr << "Failed to open log file: " << filename << "\n";
    }
  }
  FileLogger(const FileLogger&) = delete;
  ~FileLogger() {
    if (file_.is_open()) {
      file_.close();
    }
  }

  inline auto stream() -> std::ostream& {
    return file_;
  }

  void Log(LogLevel level, const std::source_location location, const std::string_view message) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!file_.is_open())
      return;

    auto filename = std::filesystem::path(location.file_name()).filename().string();

    auto now = std::chrono::system_clock::now();
    file_ << std::format("[{:%Y-%m-%d %H:%M:%S}] [{}] [{}:{}] {}\n", now, level_to_string(level), filename,
                         location.line(), message);
    file_.flush();
  }

  FileLogger& operator=(const FileLogger&) = delete;

 public:
  static void Init(const std::string_view filename);
  static FileLogger* Get();
};

static inline void Init(const std::string_view filename) {
  return FileLogger::Init(filename);
}

static inline void debug(const std::string_view message,
                         const std::source_location location = std::source_location::current()) {
  return FileLogger::Get()->Log(LogLevel::DEBUG, location, message);
}

static inline void info(const std::string_view message,
                        const std::source_location location = std::source_location::current()) {
  return FileLogger::Get()->Log(LogLevel::INFO, location, message);
}

static inline void warn(const std::string_view message,
                        const std::source_location location = std::source_location::current()) {
  return FileLogger::Get()->Log(LogLevel::WARNING, location, message);
}

static inline void error(const std::string_view message,
                         const std::source_location location = std::source_location::current()) {
  return FileLogger::Get()->Log(LogLevel::ERROR, location, message);
}

static inline void fatal(const std::string_view message,
                         const std::source_location location = std::source_location::current()) {
  return FileLogger::Get()->Log(LogLevel::FATAL, location, message);
}
}  // namespace kura::log

#endif  // KURA_LOG_H
