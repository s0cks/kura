#include <algorithm>
#include <chrono>
#include <format>
#include <numeric>
#include <string>
#include <vector>

#include "gl_fixture.h"

using kura::test::KuraBenchHarness;
using kura::test::OpenGLFixture;

namespace {

void BenchmarkKuiFile_GPU(OpenGLFixture& fixture, benchmark::State& state, const std::string& filename) {
  const std::string full_path = std::format("{}/{}", KURA_EXAMPLES_DIR, filename);

  KuraBenchHarness harness;
  std::string error;
  if (!harness.Init(full_path, static_cast<uint32_t>(fixture.width()), static_cast<uint32_t>(fixture.height()),
                    &error)) {
    state.SkipWithError(error.c_str());
    return;
  }

  // Warmup pass
  if (!harness.session->Render(&error)) {
    state.SkipWithError(error.c_str());
    return;
  }
  glFinish();

  std::vector<double> gpu_samples_sec;
  gpu_samples_sec.reserve(2048);

  double total_gpu_seconds = 0.0;
  const auto wall_start = std::chrono::steady_clock::now();

  for (auto _ : state) {
    glBindFramebuffer(GL_FRAMEBUFFER, fixture.fbo());

    glBeginQuery(GL_TIME_ELAPSED, fixture.time_query());
    const bool ok = harness.session->Render(&error);
    glEndQuery(GL_TIME_ELAPSED);

    if (!ok) {
      state.SkipWithError(error.c_str());
      break;
    }

    GLuint64 elapsed_ns = 0;
    glGetQueryObjectui64v(fixture.time_query(), GL_QUERY_RESULT, &elapsed_ns);

    const double seconds = elapsed_ns > 0 ? (static_cast<double>(elapsed_ns) / 1e9) : 1e-7;
    total_gpu_seconds += seconds;
    gpu_samples_sec.push_back(seconds);
    state.SetIterationTime(seconds);
  }

  const double wall_seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - wall_start).count();

  const double iters = static_cast<double>(state.iterations());
  const double gpu_fps = iters / total_gpu_seconds;
  const double wall_fps = iters / wall_seconds;

  double one_percent_low_fps = gpu_fps;
  if (!gpu_samples_sec.empty()) {
    const size_t tail_count = std::max<size_t>(1, gpu_samples_sec.size() / 100);
    const auto split_it = gpu_samples_sec.end() - static_cast<std::ptrdiff_t>(tail_count);
    std::nth_element(gpu_samples_sec.begin(), split_it, gpu_samples_sec.end());

    const double slowest_sum = std::accumulate(split_it, gpu_samples_sec.end(), 0.0);
    one_percent_low_fps = 1.0 / (slowest_sum / static_cast<double>(tail_count));
  }

  const double pixels_per_frame = static_cast<double>(fixture.width()) * static_cast<double>(fixture.height());

  state.counters["GPU_FPS"] = gpu_fps;
  state.counters["1%_Low"] = one_percent_low_fps;
  state.counters["Wall_FPS"] = wall_fps;
  state.counters["FillRate"] = benchmark::Counter(iters * pixels_per_frame, benchmark::Counter::kIsRate);
}

}  // namespace

#define KURA_REGISTER_GPU_SCENE(CaseName, KuiFile)                        \
  BENCHMARK_DEFINE_F(OpenGLFixture, CaseName)(benchmark::State & state) { \
    BenchmarkKuiFile_GPU(*this, state, KuiFile);                          \
  }                                                                       \
  BENCHMARK_REGISTER_F(OpenGLFixture, CaseName)                           \
      ->ArgNames({"W", "H"})                                              \
      ->Args({1024, 1024})                                                \
      ->Args({1280, 720})                                                 \
      ->Args({1432, 1692})                                                \
      ->Args({1920, 1200})                                                \
      ->Args({2560, 1440})                                                \
      ->UseManualTime()                                                   \
      ->Unit(benchmark::kMicrosecond)                                     \
      ->MinWarmUpTime(0.05)                                               \
      ->MinTime(0.05)                                                     \
      ->Repetitions(3)                                                    \
      ->ReportAggregatesOnly(true)

// Register as many .kui scenes as you want:
KURA_REGISTER_GPU_SCENE(Counter_GPU, "counter.kui");
KURA_REGISTER_GPU_SCENE(TodoList_GPU, "todo.kui");
KURA_REGISTER_GPU_SCENE(TextHeavy_GPU, "text.kui");
