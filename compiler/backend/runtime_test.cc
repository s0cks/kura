#include <algorithm>
#include <cinttypes>
#include <csetjmp>
#include <cstdio>
#include <cstring>
#include <format>
#include <rang.hpp>
#include <string_view>
#include <vector>

#include "frontend/syntax/source.h"
#include "heap/objects.h"
#include "runtime/binary_op.h"
#include "runtime_abi.h"
#include "test_suite.h"
#include "tracing/tracing.h"

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

// Expands tabs so the carets line up with what the terminal shows, and maps a
// byte column on the line to a display column.
struct Line {
  std::string text{};
  std::vector<uint32_t> display_column{};  // by byte column, plus one past the end
};

static inline auto ExpandLine(std::string_view raw, const uint32_t tab_width) -> Line {
  Line line{};
  uint32_t column = 0;
  for (size_t idx = 0; idx < raw.size(); idx++) {
    line.display_column.push_back(column);
    const auto c = static_cast<unsigned char>(raw[idx]);
    if (c == '\t') {
      const auto spaces = tab_width - (column % tab_width);
      line.text.append(spaces, ' ');
      column += spaces;
      continue;
    }
    line.text.push_back(static_cast<char>(c));
    // continuation bytes of a UTF-8 sequence take no extra column
    if ((c & 0xC0) != 0x80)
      column++;
  }
  line.display_column.push_back(column);
  return line;
}

auto Digits(uint32_t value) -> size_t {
  size_t digits = 1;
  while (value >= 10) {
    value /= 10;
    digits++;
  }
  return digits;
}

static const kura::syntax::SourceFile* source_ = nullptr;

class TestResultRenderer {
 public:
  struct Opts {
    int tab_width;
  };

 private:
  std::ostream& stream_;
  Opts opts_{};

  inline auto stream() -> std::ostream& {
    return stream_;
  }

  inline auto colorize() const -> bool {
    return true;  // TODO(@s0cks): implement
  }

#define DEFINE_PAINT_COLOR(Color)                                                 \
  inline auto Color(const std::string_view rhs) -> TestResultRenderer& {          \
    if (colorize()) {                                                             \
      stream() << rang::fg::Color << rhs << rang::fg::reset;                      \
    } else {                                                                      \
      stream() << rhs;                                                            \
    }                                                                             \
    return *this;                                                                 \
  }                                                                               \
  inline auto bold_##Color(const std::string_view rhs) -> TestResultRenderer& {   \
    if (colorize()) {                                                             \
      stream() << rang::style::bold << rang::fg::Color << rhs << rang::fg::reset; \
    } else {                                                                      \
      stream() << rhs;                                                            \
    }                                                                             \
    return *this;                                                                 \
  }

  DEFINE_PAINT_COLOR(red);
  DEFINE_PAINT_COLOR(blue);
  DEFINE_PAINT_COLOR(green);
  DEFINE_PAINT_COLOR(yellow);
  DEFINE_PAINT_COLOR(cyan);
  DEFINE_PAINT_COLOR(magenta);
  DEFINE_PAINT_COLOR(gray);
#undef DEFINE_PAINT_COLOR

  inline auto bold(std::string_view rhs) -> TestResultRenderer& {
    if (colorize()) {
      stream() << rang::style::bold << rhs << rang::style::reset;
    } else {
      stream() << rhs;
    }
    return *this;
  }

 public:
  explicit TestResultRenderer(std::ostream& stream, const Opts opts = {}) :
    stream_(stream),
    opts_(std::move(opts)) {}
  ~TestResultRenderer() = default;

  void Render(const TestResult res) {
    auto last_line = source_->GetPosition(res.span.begin).line + 1;
    for (const auto& label : res.labels)
      last_line = std::max(last_line, source_->GetPosition(label.span.begin).line + 1);
    const auto gutter = std::string(Digits(last_line), ' ');

    const auto begin = source_->GetPosition(res.span.begin);

    switch (res.status) {
      case TestResult::kFailed:
        red("Failed");
        break;
      default:
        break;
    }
    stream() << ": ";
    bold(res.message);
    stream() << "\n";

    magenta("--> ");
    stream() << source_->GetName() << ":" << begin << "\n";

    stream() << gutter;
    magenta(" |");
    stream() << "\n";

    // The primary span and every label, ordered by line.
    struct Mark {
      kura::Span span;
      const std::string* text;
      bool primary;
    };

    std::vector<Mark> marks{{res.span, &res.span_label, true}};
    for (const auto& label : res.labels)
      marks.push_back({label.span, &label.message, false});

    std::stable_sort(marks.begin(), marks.end(), [](const Mark& a, const Mark& b) {
      return a.span.begin < b.span.begin;
    });

    uint32_t previous_line = UINT32_MAX;
    for (const auto& mark : marks) {
      const auto start = source_->GetPosition(mark.span.begin);
      if (previous_line != UINT32_MAX && start.line != previous_line) {
        stream() << gutter;
        magenta(" |");
        stream() << (start.line > previous_line + 1 ? " ..." : "") << '\n';
      }

      const auto same_line = previous_line == start.line;
      previous_line = start.line;
      const auto line_span = source_->GetLineSpan(start.line);
      const auto line = ExpandLine(source_->GetText(line_span), opts_.tab_width);
      const auto number = std::to_string(start.line + 1);
      if (!same_line) {
        stream() << std::string(gutter.size() - number.size(), ' ');
        magenta(number + " |");
        stream() << " " << line.text << "\n";
      }

      // The carets cover the span on this line; a span that runs on past the
      // end of the line is drawn up to the end of it.
      const auto first = std::min<size_t>(start.column, line.display_column.size() - 1);
      const auto end_offset = std::min(mark.span.end, line_span.end);
      const auto end_column = std::min<size_t>(end_offset - line_span.begin, line.display_column.size() - 1);
      const auto from = line.display_column[first];
      auto to = line.display_column[std::max(first, end_column)];
      if (to <= from)
        to = from + 1;

      const auto marker = std::string(to - from, mark.primary ? '^' : '-');
      stream() << gutter;
      magenta(" |");
      stream() << ' ' << std::string(from, ' ');
      if (mark.primary) {
        switch (res.status) {
          case TestResult::kFailed:
            red(marker);
            break;
          case TestResult::kPassed:
            green(marker);
            break;
          case TestResult::kSkipped:
            bold(marker);
            break;
        }
      } else {
        magenta(marker);
      }
      stream() << " ";
      if (!mark.text->empty()) {
        if (mark.primary) {
          switch (res.status) {
            case TestResult::kFailed:
              red(*mark.text);
              break;
            case TestResult::kPassed:
              green(*mark.text);
              break;
            case TestResult::kSkipped:
              bold(*mark.text);
              break;
          }
        } else {
          magenta(*mark.text);
        }
      }
      stream() << '\n';
    }

    if (!res.help.empty()) {
      stream() << gutter;
      magenta(" = ");
      bold("help: ");
      stream() << res.help << "\n";
    }

    for (const auto& note : res.notes) {
      stream() << gutter;
      magenta(" = ");
      bold("note: ");
      stream() << note << "\n";
    }
  }
};

static inline void Render(std::ostream& stream, const TestResult& rhs) {
  TestResultRenderer renderer(stream, TestResultRenderer::Opts{
                                          .tab_width = 2,
                                      });
  renderer.Render(rhs);
}

}  // namespace

namespace {
// The comparison an assertion made, as it is written, for the label under it.
auto OpText(const uint64_t op) -> std::string_view {
  switch (static_cast<kura::BinaryOp>(op)) {
    case kura::BinaryOp::kEq:
      return "==";
    case kura::BinaryOp::kNeq:
      return "!=";
    case kura::BinaryOp::kGreaterThan:
      return ">";
    case kura::BinaryOp::kGreaterThanEqual:
      return ">=";
    case kura::BinaryOp::kLessThan:
      return "<";
    case kura::BinaryOp::kLessThanEqual:
      return "<=";
    default:
      return "?";
  }
}

// A failed assertion, recorded. The underline covers the assertion, `assert(` to `)`, and not the rest of its line:
// a line with something after the assertion, or two of them, says which one failed.
auto RecordFailure(const uint64_t line, const uint64_t col, const uint64_t length, void* msg) -> TestResult& {
  const auto m = static_cast<StringObj*>(msg);

  kura::Span span{0, 0};
  if (source_ != nullptr && line > 0) {
    kura::syntax::Position pos{
        .line = static_cast<uint32_t>(line - 1),
        .column = col > 0 ? static_cast<uint32_t>(col - 1) : 0,
    };
    const uint32_t begin = source_->GetOffset(pos);
    const uint32_t line_end = source_->GetLineSpan(pos.line).end;
    const uint32_t end = length > 0 ? std::min<uint32_t>(begin + static_cast<uint32_t>(length), line_end) : line_end;
    span = kura::Span{begin, std::max(begin + 1, end)};
  }

  return suite_.failed.emplace_back(TestResult{
      .status = TestResult::kFailed,
      .message = std::string(StrView(m)),
      .span = span,
      .span_label = "assertion failed",
  });
}
}  // namespace

extern "C" {
void kura_rt_set_source(const kura::syntax::SourceFile* source) {
  source_ = source;
}

void kura_rt_test_suite_begin(const uint64_t total, const char* filter, const bool fail_fast) {
  suite_ = kura::rt::TestSuiteState{};
  suite_.global_fail_fast = fail_fast;
  suite_.suite_start = std::chrono::high_resolution_clock::now();
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

void kura_rt_test_begin(const char* name, uint32_t flags, const char* expect) {
  suite_.name = name ? name : "<unnamed>";
  suite_.flags = flags;
  suite_.expect = expect != nullptr ? expect : "";
  suite_.failed.clear();
  suite_.zone_depth = kura_rt_tracy_depth();
  suite_.view_depth = kura_rt_view_depth();
  kura_rt_tracy_zone_begin_named(suite_.name.data(), suite_.name.size(), kura::tracing::kColorTest);
  suite_.start = std::chrono::high_resolution_clock::now();
}

void kura_rt_test_fail(void* file, uint64_t line, uint64_t col, uint64_t length, void* msg) {
  (void)file;
  RecordFailure(line, col, length, msg);
}

void kura_rt_test_fail_cmp(void* file, uint64_t line, uint64_t col, uint64_t length, void* msg, uint64_t op,
                           uint64_t lt, uint64_t lb, uint64_t rt, uint64_t rb) {
  (void)file;
  auto& failure = RecordFailure(line, col, length, msg);
  const auto left = kura::rt::Format(kura::rt::KuraValue{lt, lb});
  const auto right = kura::rt::Format(kura::rt::KuraValue{rt, rb});
  failure.span_label = std::format("expected left {} right", OpText(op));
  failure.notes.push_back("left:  " + left);
  failure.notes.push_back("right: " + right);
}

void kura_rt_test_end() {
  // the test's zone, and any that a panic left open inside it
  kura_rt_tracy_unwind(suite_.zone_depth);
  suite_.finish = std::chrono::high_resolution_clock::now();
  const auto duration = std::chrono::duration_cast<std::chrono::microseconds>((suite_.finish - suite_.start));

  if (suite_.flags & kura::TestFlag::kSkip) {
    suite_.skipped++;
    std::cout << "  " << suite_.name << " ... skipped (" << duration << ")" << std::endl;
    return;
  }

  const bool expect_panic = (suite_.flags & kura::TestFlag::kShouldPanic) != 0;
  const bool expect_failure = (suite_.flags & kura::TestFlag::kShouldFail) != 0;
  const bool asserted = !suite_.failed.empty();
  const bool panicked = suite_.panicked;

  // What makes a test pass: a plain one, that nothing failed; @should-fail, that something did (an assertion or a
  // panic); @should-panic, that it panicked (a failed assertion is not one, so it can't pass for the wrong reason).
  // `why` is what is wrong when it didn't, for a test that went the way it wasn't supposed to.
  bool passed = false;
  std::string why{};
  if (expect_panic) {
    passed = panicked;
    if (!passed)
      why = asserted ? "expected the test to panic (@should-panic), but it only failed assertions"
                     : "expected the test to panic (@should-panic), but it ran to the end";
  } else if (expect_failure) {
    passed = asserted || panicked;
    if (!passed)
      why = "expected the test to fail or panic (@should-fail), but it passed";
  } else {
    passed = !asserted && !panicked;
  }

  // `@should-fail("text")`: the failure has to say it
  if (passed && (expect_panic || expect_failure) && !suite_.expect.empty()) {
    bool found = panicked && suite_.panic_message.find(suite_.expect) != std::string::npos;
    if (!expect_panic) {
      for (const auto& f : suite_.failed)
        found = found || f.message.find(suite_.expect) != std::string::npos;
    }
    if (!found) {
      passed = false;
      why = std::format("it failed, but not the way it was expected to: `{}` was not the expected message `{}`",
                        suite_.panic_message, suite_.expect);
    }
  }

  if (passed) {
    suite_.passed++;
    std::cout << "  " << suite_.name << " ... " << rang::style::bold << rang::fg::green << "ok" << rang::style::reset
              << " (" << duration << ")" << std::endl;
    return;
  }

  suite_.num_failed++;
  std::cout << "  " << suite_.name << " ... " << rang::style::bold << rang::fg::red << "failed" << rang::style::reset
            << " (" << duration << ")" << std::endl;
  std::cout << "\nfailures:\n";
  if (!why.empty()) {
    std::cout << "\n"
              << rang::fg::red << "Failed" << rang::fg::reset << ": " << rang::style::bold << suite_.name
              << rang::style::reset << "\n  " << why << "\n";
    // what it did say, so the difference between that and what was wanted is on the screen
    for (const auto& f : suite_.failed)
      std::cout << "  assertion: " << f.message << "\n";
    if (panicked)
      std::cout << "  panic: " << suite_.panic_message << "\n";
  } else {
    for (const auto& f : suite_.failed) {
      std::cout << "\n";
      Render(std::cout, f);
    }

    if (panicked)
      std::printf("\n  panicked: %s\n", suite_.panic_message.c_str());
  }
  std::cout << "\n";
}

// In kura_rt_test_suite_end():
uint64_t kura_rt_test_suite_end() {
  suite_.suite_finish = std::chrono::high_resolution_clock::now();
  const auto duration =
      std::chrono::duration_cast<std::chrono::microseconds>((suite_.suite_finish - suite_.suite_start));
  std::cout << "\nresult: ";
  if (suite_.num_failed == 0) {
    std::cout << rang::style::bold << rang::fg::green << "ok";
  } else {
    std::cout << rang::style::bold << rang::fg::red << "failed";
  }
  std::cout << rang::style::reset;
  std::cout << " (" << duration << ")\n";  // added space before (duration)

  std::cout << suite_.skipped << " skipped; ";
  std::cout << suite_.passed << " passed; ";
  std::cout << suite_.num_failed << " failed\n";
  return suite_.num_failed;
}

void kura_rt_test_invoke(void (*test_fn)(void)) {
  if (!test_fn)
    return;

  suite_.panicked = false;
  suite_.panic_message.clear();
  suite_.in_test = true;

  if (setjmp(suite_.panic_jmp) == 0)
    test_fn();
  else {
    kura_rt_tracy_unwind(suite_.zone_depth + 1);  // keep the test's own zone: `kura_rt_test_end` closes it
    kura_rt_view_unwind(suite_.view_depth);
  }

  suite_.in_test = false;
}
}
