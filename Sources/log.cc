#include "log.h"

namespace kura::log {
thread_local FileLogger* logger;

void FileLogger::Init(const std::string_view filename) {
  if (logger)
    return;
  logger = new FileLogger(filename);
}

FileLogger* FileLogger::Get() {
  return logger;
}
}  // namespace kura::log
