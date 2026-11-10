#include <benchmark/benchmark.h>
#include <fstream>
#include <print>
#include <sstream>
#include <string>
#include <vector>

#include "benchmark/types.h"
#include "benchmark/utils.h"
#include "common.h"
#include "frontend/syntax/parser.h"
#include "heap/heap.h"

/**
// Inside your benchmark function after the loop:
int64_t total_allocs_estimated = total_nodes_parsed * state.iterations();
state.counters["Allocs/s"] = benchmark::Counter(
    static_cast<double>(total_allocs_estimated),
    benchmark::Counter::kIsRate
);
 */

using namespace kura;

struct ParserSample {
  std::string_view label;
  std::string_view data;
};

static constexpr ParserSample kSamples[] = {
    {"EmptyFn", "fn empty() => None"},
    {"BinaryAdd", "fn add(a: Int, b: Int): Int => a + b"},
    {"BasicMatch", "fn init(msg: Number): String => match msg { 0 => \"Zero\", 10 => \"Ten\", _ => \"????\", }"},
    {"ComplexMatch",
     "fn init(msg: Number): String => match msg { x if x > 10 => \"This is "
     "10\", x if x < 0 => \"negative\", 0 => \"??\", _ => \"Hello World\", }"}};

static inline void BM_Parse(benchmark::State& state) {
  const auto& sample = kSamples[state.range(0)];
  const size_t bytes = sample.data.size();
  const std::string label(sample.label);
  const std::string code(sample.data);

  const auto heap = new heap::Heap(heap::Heap::SmallOptions());
  KASSERT(heap);
  heap::Heap::SetCurrent(heap);

  {
    const auto tree = syntax::ParseText(label, code);
    if (!tree->IsValid()) {
      std::cerr << sample.label << " tree is invalid" << std::endl;
      std::cerr << "code: \n" << sample.data << std::endl;
      std::exit(1);
    }
  }

  for (auto _ : state) {
    state.PauseTiming();
    heap->model().Reset();
    state.ResumeTiming();

    const auto tree = syntax::ParseText(label, code);
    benchmark::DoNotOptimize(tree.get());
    benchmark::ClobberMemory();
  }

  state.SetLabel(std::format("BM_Parse/{}", sample.label));
  state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * bytes);
}

BENCHMARK(BM_Parse)->DenseRange(0, std::size(kSamples) - 1)->Unit(benchmark::kMicrosecond);
BENCHMARK_MAIN();
