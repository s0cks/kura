// What a host does once before it loads a program (kurac and the GL demo do the same): a heap for the compiler's
// syntax trees and types to live in, and the runtime's type objects.
#include <gtest/gtest.h>

#include "heap/heap.h"
#include "type.h"

namespace {
class RuntimeEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    kura::heap::Heap::SetCurrent(new kura::heap::Heap(kura::heap::Heap::SmallOptions()));
    kura::Type::Init();
  }
};

[[maybe_unused]] const auto* const kEnvironment = ::testing::AddGlobalTestEnvironment(new RuntimeEnvironment());
}  // namespace
