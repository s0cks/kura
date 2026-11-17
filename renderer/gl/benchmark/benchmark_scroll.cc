#include <algorithm>
#include <cmath>
#include <format>
#include <numeric>
#include <string>
#include <vector>

#include "gl_fixture.h"

using kura::test::KuraBenchHarness;
using kura::test::OpenGLFixture;

namespace {

// Populates the counter list with `item_count` rows before measurement starts
bool PopulateList(KuraBenchHarness& harness, int64_t item_count, std::string* error) {
  if (!harness.session->Send(kura::rt::MakeVariant("Reset"), error)) {
    return false;
  }

  for (int64_t i = 0; i < item_count; ++i) {
    // Adjust payload constructor if MakeVariant takes a KuraValue span/initializer_list
    const auto value = kura::rt::MakeInt(i);
    if (!harness.session->Send(kura::rt::MakeVariant("Append", &value), error)) {
      return false;
    }
  }
  return true;
}

}  // namespace

// 1. CPU Stress Test: Measures layout, text shaping, and draw-call batching as list size N grows
BENCHMARK_DEFINE_F(OpenGLFixture, ScrollListGrowth_CPU)(benchmark::State& state) {
  const int64_t list_size = state.range(0);
  const std::string full_path = std::format("{}/counter.kui", KURA_EXAMPLES_DIR);

  KuraBenchHarness harness;
  std::string error;
  if (!harness.Init(full_path, 1432, 1692, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  if (!PopulateList(harness, list_size, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  // Warmup pass (shapes initial visible glyphs & compiles shaders)
  if (!harness.session->Render(&error)) {
    state.SkipWithError(error.c_str());
    return;
  }
  glFinish();

  const double scroll_x = 1432.0 * 0.5;
  const double scroll_y = 1692.0 * 0.5;
  uint64_t frame = 0;

  for (auto _ : state) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    // Oscillate scroll direction every 120 frames so we continuously traverse populated rows
    const double dy = ((frame / 120) % 2 == 0) ? 24.0 : -24.0;
    bool handled = false;
    if (!harness.session->Scroll(scroll_x, scroll_y, 0.0, dy, &handled, &error)) {
      state.SkipWithError(error.c_str());
      break;
    }

    if (!harness.session->Render(&error)) {
      state.SkipWithError(error.c_str());
      break;
    }

    if ((frame & 63) == 63) {
      glFlush();
    }
    ++frame;
  }

  glFinish();

  state.SetItemsProcessed(state.iterations() * list_size);
  state.counters["CPU_FPS"] = benchmark::Counter(static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(OpenGLFixture, ScrollListGrowth_CPU)
    ->ArgName("Items")
    ->Arg(50)
    ->Arg(250)
    ->Arg(1000)
    ->Arg(5000)
    ->Unit(benchmark::kMicrosecond)
    ->MinWarmUpTime(0.05)
    ->MinTime(0.1)
    ->Repetitions(3)
    ->ReportAggregatesOnly(true);

// 2. GPU Stress Test: Measures whether offscreen list items leak into GPU vertex/fragment work
BENCHMARK_DEFINE_F(OpenGLFixture, ScrollListGrowth_GPU)(benchmark::State& state) {
  const int64_t list_size = state.range(0);
  const std::string full_path = std::format("{}/counter.kui", KURA_EXAMPLES_DIR);

  KuraBenchHarness harness;
  std::string error;
  if (!harness.Init(full_path, 1432, 1692, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  if (!PopulateList(harness, list_size, &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  if (!harness.session->Render(&error)) {
    state.SkipWithError(error.c_str());
    return;
  }
  glFinish();

  const double scroll_x = 1432.0 * 0.5;
  const double scroll_y = 1692.0 * 0.5;
  uint64_t frame = 0;

  for (auto _ : state) {
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);

    const double dy = ((frame / 120) % 2 == 0) ? 24.0 : -24.0;
    bool handled = false;
    if (!harness.session->Scroll(scroll_x, scroll_y, 0.0, dy, &handled, &error)) {
      state.SkipWithError(error.c_str());
      break;
    }

    glBeginQuery(GL_TIME_ELAPSED, time_query_);
    const bool ok = harness.session->Render(&error);
    glEndQuery(GL_TIME_ELAPSED);

    if (!ok) {
      state.SkipWithError(error.c_str());
      break;
    }

    GLuint64 elapsed_ns = 0;
    glGetQueryObjectui64v(time_query_, GL_QUERY_RESULT, &elapsed_ns);

    const double seconds = elapsed_ns > 0 ? (static_cast<double>(elapsed_ns) / 1e9) : 1e-7;
    state.SetIterationTime(seconds);
    ++frame;
  }

  state.counters["GPU_FPS"] = benchmark::Counter(static_cast<double>(state.iterations()), benchmark::Counter::kIsRate);
}

BENCHMARK_REGISTER_F(OpenGLFixture, ScrollListGrowth_GPU)
    ->ArgName("Items")
    ->Arg(50)
    ->Arg(250)
    ->Arg(1000)
    ->Arg(5000)
    ->UseManualTime()
    ->Unit(benchmark::kMicrosecond)
    ->MinWarmUpTime(0.05)
    ->MinTime(0.05)
    ->Repetitions(3)
    ->ReportAggregatesOnly(true);
