// The runtime's draw calls (kura_rt_*), the ones compiled code makes, against the headless renderer:
// they have to reach whichever renderer the host set on the thread, decoded the way the compiler
// passes them.
#include <cstdint>
#include <cstring>
#include <gtest/gtest.h>
#include <thread>

#include "backend/runtime_abi.h"
#include "renderer/headless/renderer.h"

namespace {
using kura::renderer::Command;
using kura::renderer::HeadlessOptions;
using kura::renderer::HeadlessRenderer;

// What compiled code passes for a Float: the bits of an f64.
auto F(const double value) -> uint64_t {
  uint64_t bits;
  std::memcpy(&bits, &value, sizeof(bits));
  return bits;
}

class RuntimeRendererTest : public ::testing::Test {
 protected:
  void SetUp() override {
    renderer_ = std::make_unique<HeadlessRenderer>(HeadlessOptions{.rasterize = false});
    abi_ = renderer_->Abi();
    ASSERT_EQ(kura_rt_set_renderer(&abi_), KURA_RENDERER_OK);
  }

  void TearDown() override {
    kura_rt_set_renderer(nullptr);
  }

  std::unique_ptr<HeadlessRenderer> renderer_;
  KuraRenderer abi_{};
};

TEST_F(RuntimeRendererTest, DrawCallsReachTheRenderer) {
  kura_rt_begin_frame(640, 480);
  kura_rt_check_renderer();
  EXPECT_TRUE(renderer_->InFrame());
  EXPECT_EQ(renderer_->GetWidth(), 640u);
  EXPECT_EQ(renderer_->GetHeight(), 480u);

  kura_rt_fill_rect(F(10), F(20.5), F(30), F(40), 0xFF0000FFu);
  kura_rt_check_renderer();
  ASSERT_EQ(renderer_->GetCommands().size(), 1u);
  const auto& command = renderer_->GetCommands()[0];
  EXPECT_EQ(command.kind, Command::Kind::kFillRect);
  EXPECT_EQ(command.rect.x, 10.0f);
  EXPECT_EQ(command.rect.y, 20.5f);
  EXPECT_EQ(command.rect.width, 30.0f);
  EXPECT_EQ(command.rect.height, 40.0f);

  kura_rt_end_frame();
  kura_rt_check_renderer();
  EXPECT_FALSE(renderer_->InFrame());
  EXPECT_EQ(renderer_->GetFrameCount(), 1u);
}

TEST_F(RuntimeRendererTest, ColorIsPackedRgba8) {
  kura_rt_begin_frame(16, 16);
  kura_rt_fill_rect(F(0), F(0), F(1), F(1), 0x336699CCu);
  const auto color = renderer_->GetCommands().at(0).color;
  EXPECT_FLOAT_EQ(color.r, 0x33 / 255.0f);
  EXPECT_FLOAT_EQ(color.g, 0x66 / 255.0f);
  EXPECT_FLOAT_EQ(color.b, 0x99 / 255.0f);
  EXPECT_FLOAT_EQ(color.a, 0xCC / 255.0f);
  kura_rt_end_frame();
}

TEST_F(RuntimeRendererTest, CheckRendererPassesWhenTheLastCallWorked) {
  kura_rt_begin_frame(16, 16);
  kura_rt_fill_rect(F(0), F(0), F(1), F(1), 0);
  kura_rt_check_renderer();
  kura_rt_end_frame();
  kura_rt_check_renderer();
  SUCCEED();
}

TEST_F(RuntimeRendererTest, GetRendererReturnsWhatWasSet) {
  const auto got = kura_rt_get_renderer();
  ASSERT_NE(got, nullptr);
  EXPECT_EQ(got->ops, abi_.ops);
  EXPECT_EQ(got->self, abi_.self);

  EXPECT_EQ(kura_rt_set_renderer(nullptr), KURA_RENDERER_OK);
  EXPECT_EQ(kura_rt_get_renderer(), nullptr);
}

TEST_F(RuntimeRendererTest, AnUnusableRendererIsRefusedAndTheOldOneKept) {
  KuraRenderer none{};
  EXPECT_EQ(kura_rt_set_renderer(&none), KURA_RENDERER_ERR_INVALID_ARG);

  KuraRendererOps old = *abi_.ops;
  old.abi_version = KURA_RENDERER_ABI_VERSION + 1;
  KuraRenderer wrong_version{&old, abi_.self};
  EXPECT_EQ(kura_rt_set_renderer(&wrong_version), KURA_RENDERER_ERR_INVALID_ARG);

  KuraRendererOps small = *abi_.ops;
  small.struct_size = sizeof(KuraRendererOps) - 1;
  KuraRenderer too_small{&small, abi_.self};
  EXPECT_EQ(kura_rt_set_renderer(&too_small), KURA_RENDERER_ERR_INVALID_ARG);

  ASSERT_NE(kura_rt_get_renderer(), nullptr);
  EXPECT_EQ(kura_rt_get_renderer()->ops, abi_.ops);
}

TEST_F(RuntimeRendererTest, TheRendererIsPerThread) {
  bool had_one = true;
  std::thread other([&] {
    had_one = kura_rt_get_renderer() != nullptr;
  });
  other.join();
  EXPECT_FALSE(had_one);
  EXPECT_NE(kura_rt_get_renderer(), nullptr);
}

using RuntimeRendererDeathTest = RuntimeRendererTest;

TEST_F(RuntimeRendererDeathTest, CheckRendererPanicsWhenTheLastCallFailed) {
  // outside of a frame
  kura_rt_fill_rect(F(0), F(0), F(1), F(1), 0);
  EXPECT_EXIT(kura_rt_check_renderer(), ::testing::ExitedWithCode(101), "rejected kura_rt_fill_rect");
}

TEST_F(RuntimeRendererDeathTest, DrawingWithoutARendererPanics) {
  kura_rt_set_renderer(nullptr);
  EXPECT_EXIT(kura_rt_begin_frame(1, 1), ::testing::ExitedWithCode(101), "no renderer on this thread");
}
}  // namespace
