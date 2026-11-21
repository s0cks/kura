#include <benchmark/benchmark.h>

#include "layout/layout.h"

using namespace kura;

static inline void BM_Parse(benchmark::State& state) {
  for (auto _ : state) {}
}

BENCHMARK(BM_Parse)->Unit(benchmark::kMicrosecond);
