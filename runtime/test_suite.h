#ifndef KURA_TEST_SUITE_H
#define KURA_TEST_SUITE_H

#include <cstdint>
#include <string>

namespace kura {
enum TestFlag : uint32_t {
  kNone = 0,
  kFailFast = 1 << 0,
  kShouldFail = 1 << 1,
  kSkip = 1 << 2,
  kShouldPanic = 1 << 3,
};

using TestFlags = uint32_t;

class Function;
struct TestCase {
  std::string name;
  Function* fn;
  TestFlags flags = kNone;
  // What the failure of a @should-fail or @should-panic test has to contain, or empty for any failure.
  std::string expect{};
};
}  // namespace kura

#endif  // KURA_TEST_SUITE_H
