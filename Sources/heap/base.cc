#include "heap/base.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace kura::heap {
void Fatal(const char* file, const int line, const char* format, ...) {
  std::fflush(stdout);
  std::fprintf(stderr, "kura: heap: %s:%d: ", file, line);
  va_list args;
  va_start(args, format);
  std::vfprintf(stderr, format, args);
  va_end(args);
  std::fprintf(stderr, "\n");
  std::fflush(stderr);
  std::abort();
}
}  // namespace kura::heap
