#include <cinttypes>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <vector>

#include "heap/objects.h"
#include "runtime_abi.h"
#include "test_suite.h"

using kura::heap::StringObj;

namespace kura::rt {
TestSuiteState suite_{};
}

using namespace kura::rt;

namespace {
auto StrView(const kura::heap::StringObj* s) -> std::string_view {
  if (s == nullptr)
    return {};
  return s->view();
}
}  // namespace

extern "C" {
void kura_rt_test_suite_begin(const uint64_t total, const char* filter, const bool fail_fast) {
  suite_ = kura::rt::TestSuiteState{};
  suite_.total = total;
  suite_.global_fail_fast = fail_fast;
  if (filter && *filter)
    std::printf("running tests matching \"%s\"...\n\n", filter);
  else
    std::printf("running %" PRIu64 " test(s)...\n\n", total);
}

bool kura_rt_test_matches(const char* name, const char* filter) {
  if (!filter || !*filter)
    return true;
  return name && std::strstr(name, filter) != nullptr;
}

void kura_rt_test_begin(const char* name, uint32_t flags) {
  suite_.name = name ? name : "<unnamed>";
  suite_.flags = flags;
  suite_.failures.clear();
}

void kura_rt_test_fail(void* file, uint64_t line, uint64_t col, void* msg) {
  const auto f = static_cast<StringObj*>(file);
  const auto m = static_cast<StringObj*>(msg);
  suite_.failures.push_back({
      std::string(StrView(f)),
      line,
      col,
      std::string(StrView(m)),
  });
}

void kura_rt_test_end() {
  if (suite_.flags & kura::TestFlag::kSkip) {
    suite_.skipped++;
    std::printf("test %s ... SKIPPED\n", suite_.name.c_str());
    return;
  }

  const bool had_failures = !suite_.failures.empty() || suite_.panicked;
  const bool should_fail = (suite_.flags & kura::TestFlag::kShouldFail) == 0;
  const bool passed = (had_failures != should_fail);

  if (passed) {
    suite_.passed++;
    std::printf("test %s ... ok\n", suite_.name.c_str());
    return;
  }

  suite_.failed++;

  if (suite_.panicked) {
    std::printf("test %s ... PANICKED\n", suite_.name.c_str());
  } else {
    std::printf("test %s ... FAILED\n", suite_.name.c_str());
  }

  if (should_fail && !had_failures) {
    std::printf("  expected test to fail or panic (@should-fail), but it succeeded\n");
  } else {
    std::printf("  %" PRIu64 " failures", suite_.failures.size());

    for (const auto& f : suite_.failures) {
      std::printf("  --> %s:%" PRIu64 ":%" PRIu64 ": %s\n", f.file.c_str(), f.line, f.column, f.message.c_str());
    }

    if (suite_.panicked) {
      std::printf("  panicked: %s\n", suite_.panic_message.c_str());
    }
  }
}

uint64_t kura_rt_test_suite_end() {
  std::printf("\ntest result: %s. %" PRIu64 " passed; %" PRIu64 " failed; %" PRIu64 " skipped\n",
              suite_.failed == 0 ? "ok" : "FAILED", suite_.passed, suite_.failed, suite_.skipped);
  return suite_.failed;
}
void kura_rt_test_invoke(void (*test_fn)(void)) {
  if (!test_fn)
    return;

  suite_.panicked = false;
  suite_.panic_message.clear();
  suite_.in_test = true;

  if (setjmp(suite_.panic_jmp) == 0)
    test_fn();

  suite_.in_test = false;
}
}
