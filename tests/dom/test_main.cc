#include <gtest/gtest.h>

#include "type.h"

namespace {
// The builtin types (and so the property registry) have to exist before any
// expression is built.
class TypeEnvironment : public ::testing::Environment {
 public:
  void SetUp() override {
    kura::Type::Init();
  }
};

[[maybe_unused]] const auto* const kEnvironment = ::testing::AddGlobalTestEnvironment(new TypeEnvironment());
}  // namespace
