#include "conformance.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

#include <gtest/gtest.h>

namespace kura::test {
namespace {
constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();
constexpr KuraColor kRed = {1.0f, 0.0f, 0.0f, 1.0f};
constexpr KuraRect kBox = {0.0f, 0.0f, 10.0f, 10.0f};
constexpr KuraTextStyle kStyle = {16.0f};

// A renderer from the factory, destroyed when the test ends (however it ends).
class Fixture {
 public:
  explicit Fixture(const RendererFactory& factory) :
    renderer_(factory()) {}

  ~Fixture() {
    if (renderer_.ops != nullptr && renderer_.ops->destroy != nullptr)
      renderer_.ops->destroy(renderer_.self);
  }

  Fixture(const Fixture&) = delete;
  Fixture& operator=(const Fixture&) = delete;

  auto Ops() const -> const KuraRendererOps& {
    return *renderer_.ops;
  }

  auto Valid() const -> bool {
    return renderer_.ops != nullptr;
  }

  auto Begin(const uint32_t width = 64, const uint32_t height = 64) -> KuraRendererStatus {
    return Ops().begin_frame(renderer_.self, width, height);
  }

  auto End() -> KuraRendererStatus {
    return Ops().end_frame(renderer_.self);
  }

  auto Push(const KuraRect& rect = kBox) -> KuraRendererStatus {
    return Ops().push_clip(renderer_.self, rect);
  }

  auto Pop() -> KuraRendererStatus {
    return Ops().pop_clip(renderer_.self);
  }

  auto Fill(const KuraRect& rect = kBox, const KuraColor& color = kRed) -> KuraRendererStatus {
    return Ops().fill_rect(renderer_.self, rect, color);
  }

  auto Stroke(const KuraRect& rect = kBox, const KuraColor& color = kRed) -> KuraRendererStatus {
    return Ops().stroke_rect(renderer_.self, rect, color);
  }

  auto Line(const KuraVec2& from = {0.0f, 0.0f}, const KuraVec2& to = {5.0f, 5.0f}, const KuraColor& color = kRed)
      -> KuraRendererStatus {
    return Ops().draw_line(renderer_.self, from, to, color);
  }

  auto Text(const char* text, const size_t length, const KuraTextStyle& style = kStyle, const KuraVec2& pos = {1.0f, 1.0f},
            const KuraColor& color = kRed) -> KuraRendererStatus {
    return Ops().draw_text(renderer_.self, pos, text, length, style, color);
  }

  auto Measure(const char* text, const size_t length, const KuraTextStyle& style, KuraTextMetrics* out)
      -> KuraRendererStatus {
    return Ops().measure_text(renderer_.self, text, length, style, out);
  }

  auto Measure(const std::string& text, const float size = 16.0f) -> KuraTextMetrics {
    KuraTextMetrics metrics{};
    EXPECT_EQ(Measure(text.data(), text.size(), KuraTextStyle{size}, &metrics), KURA_RENDERER_OK);
    return metrics;
  }

 private:
  KuraRenderer renderer_;
};

using Rule = void (*)(Fixture&);

#define KURA_OK KURA_RENDERER_OK

void OpsTable(Fixture& r) {
  ASSERT_TRUE(r.Valid());
  const auto& ops = r.Ops();
  EXPECT_EQ(ops.abi_version, KURA_RENDERER_ABI_VERSION);
  // every op this version of the ABI has must be there
  EXPECT_TRUE(ops.struct_size >= sizeof(KuraRendererOps));
  EXPECT_TRUE(ops.destroy != nullptr);
  EXPECT_TRUE(ops.begin_frame != nullptr);
  EXPECT_TRUE(ops.end_frame != nullptr);
  EXPECT_TRUE(ops.push_clip != nullptr);
  EXPECT_TRUE(ops.pop_clip != nullptr);
  EXPECT_TRUE(ops.fill_rect != nullptr);
  EXPECT_TRUE(ops.stroke_rect != nullptr);
  EXPECT_TRUE(ops.draw_line != nullptr);
  EXPECT_TRUE(ops.draw_text != nullptr);
  EXPECT_TRUE(ops.measure_text != nullptr);
}

void DrawingOutsideAFrameFails(Fixture& r) {
  EXPECT_EQ(r.Push(), KURA_RENDERER_ERR_NOT_IN_FRAME);
  EXPECT_EQ(r.Pop(), KURA_RENDERER_ERR_NOT_IN_FRAME);
  EXPECT_EQ(r.Fill(), KURA_RENDERER_ERR_NOT_IN_FRAME);
  EXPECT_EQ(r.Stroke(), KURA_RENDERER_ERR_NOT_IN_FRAME);
  EXPECT_EQ(r.Line(), KURA_RENDERER_ERR_NOT_IN_FRAME);
  EXPECT_EQ(r.Text("a", 1), KURA_RENDERER_ERR_NOT_IN_FRAME);
  EXPECT_EQ(r.End(), KURA_RENDERER_ERR_NOT_IN_FRAME);
}

void DrawingInsideAFrameWorks(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  EXPECT_EQ(r.Fill(), KURA_OK);
  EXPECT_EQ(r.Stroke(), KURA_OK);
  EXPECT_EQ(r.Line(), KURA_OK);
  EXPECT_EQ(r.Text("hello", 5), KURA_OK);
  EXPECT_EQ(r.Push(), KURA_OK);
  EXPECT_EQ(r.Fill(), KURA_OK);
  EXPECT_EQ(r.Pop(), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
}

void BeginFrameInsideAFrameFails(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  EXPECT_EQ(r.Begin(), KURA_RENDERER_ERR_IN_FRAME);
  // the frame that was open is still open
  EXPECT_EQ(r.Fill(), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
}

void FramesRepeat(Fixture& r) {
  for (auto idx = 0; idx < 50; idx++) {
    ASSERT_EQ(r.Begin(32 + idx, 16), KURA_OK);
    EXPECT_EQ(r.Fill(), KURA_OK);
    ASSERT_EQ(r.End(), KURA_OK);
  }
}

void OversizedSurfaceIsRejected(Fixture& r) {
  EXPECT_EQ(r.Begin(KURA_RENDERER_MAX_SURFACE_SIZE + 1, 1), KURA_RENDERER_ERR_INVALID_ARG);
  EXPECT_EQ(r.Begin(1, KURA_RENDERER_MAX_SURFACE_SIZE + 1), KURA_RENDERER_ERR_INVALID_ARG);
  // a refused begin_frame didn't open a frame
  EXPECT_EQ(r.Fill(), KURA_RENDERER_ERR_NOT_IN_FRAME);
  EXPECT_EQ(r.Begin(1, 1), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
}

void EmptySurfaceIsAllowed(Fixture& r) {
  ASSERT_EQ(r.Begin(0, 0), KURA_OK);
  EXPECT_EQ(r.Fill(), KURA_OK);
  EXPECT_EQ(r.Line(), KURA_OK);
  EXPECT_EQ(r.Push(), KURA_OK);
  EXPECT_EQ(r.Pop(), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
}

void InvalidArgumentsAreRejected(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  const KuraRect bad_rects[] = {
      {kNaN, 0, 10, 10}, {0, kNaN, 10, 10}, {0, 0, kNaN, 10}, {0, 0, 10, kNaN},
      {kInf, 0, 10, 10}, {0, 0, kInf, 10},  {0, 0, -1, 10},   {0, 0, 10, -1},
  };
  for (const auto& rect : bad_rects) {
    EXPECT_EQ(r.Fill(rect), KURA_RENDERER_ERR_INVALID_ARG);
    EXPECT_EQ(r.Stroke(rect), KURA_RENDERER_ERR_INVALID_ARG);
    EXPECT_EQ(r.Push(rect), KURA_RENDERER_ERR_INVALID_ARG);
  }
  const KuraColor bad_colors[] = {{kNaN, 0, 0, 1}, {0, kNaN, 0, 1}, {0, 0, kNaN, 1}, {0, 0, 0, kNaN}, {kInf, 0, 0, 1}};
  for (const auto& color : bad_colors) {
    EXPECT_EQ(r.Fill(kBox, color), KURA_RENDERER_ERR_INVALID_ARG);
    EXPECT_EQ(r.Stroke(kBox, color), KURA_RENDERER_ERR_INVALID_ARG);
    EXPECT_EQ(r.Line({0, 0}, {1, 1}, color), KURA_RENDERER_ERR_INVALID_ARG);
    EXPECT_EQ(r.Text("a", 1, kStyle, {0, 0}, color), KURA_RENDERER_ERR_INVALID_ARG);
  }
  EXPECT_EQ(r.Line({kNaN, 0}, {1, 1}), KURA_RENDERER_ERR_INVALID_ARG);
  EXPECT_EQ(r.Line({0, 0}, {1, kInf}), KURA_RENDERER_ERR_INVALID_ARG);
  EXPECT_EQ(r.Text("a", 1, kStyle, {kNaN, 0}), KURA_RENDERER_ERR_INVALID_ARG);
  EXPECT_EQ(r.Text(nullptr, 3), KURA_RENDERER_ERR_INVALID_ARG);
  for (const auto size : {0.0f, -1.0f, kNaN, kInf})
    EXPECT_EQ(r.Text("a", 1, KuraTextStyle{size}), KURA_RENDERER_ERR_INVALID_ARG);

  // none of that did anything: the frame is still open and balanced
  EXPECT_EQ(r.Pop(), KURA_RENDERER_ERR_CLIP_UNDERFLOW);
  EXPECT_EQ(r.Fill(), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
}

void ColorsOutOfRangeAreClamped(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  EXPECT_EQ(r.Fill(kBox, {2.0f, -1.0f, 0.5f, 7.0f}), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
}

void AnyFiniteGeometryIsAccepted(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  // nothing visible is not an error
  EXPECT_EQ(r.Fill({5, 5, 0, 0}), KURA_OK);
  EXPECT_EQ(r.Fill({-1000, -1000, 10, 10}), KURA_OK);
  EXPECT_EQ(r.Fill({1e30f, 1e30f, 1e30f, 1e30f}), KURA_OK);
  EXPECT_EQ(r.Stroke({-1e30f, 0, 5, 5}), KURA_OK);
  EXPECT_EQ(r.Line({-1e30f, -1e30f}, {1e30f, 1e30f}), KURA_OK);
  EXPECT_EQ(r.Text("a", 1, kStyle, {1e30f, -1e30f}), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
}

void ClipsNest(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  for (auto idx = 0; idx < 100; idx++)
    ASSERT_EQ(r.Push({0, 0, 50, 50}), KURA_OK);
  for (auto idx = 0; idx < 100; idx++)
    ASSERT_EQ(r.Pop(), KURA_OK);
  EXPECT_EQ(r.Pop(), KURA_RENDERER_ERR_CLIP_UNDERFLOW);
  EXPECT_EQ(r.End(), KURA_OK);
}

void PoppingAnEmptyClipStackFails(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  EXPECT_EQ(r.Pop(), KURA_RENDERER_ERR_CLIP_UNDERFLOW);
  EXPECT_EQ(r.Push(), KURA_OK);
  EXPECT_EQ(r.Pop(), KURA_OK);
  EXPECT_EQ(r.Pop(), KURA_RENDERER_ERR_CLIP_UNDERFLOW);
  EXPECT_EQ(r.End(), KURA_OK);
}

void EndingAFrameWithClipsFailsButClosesIt(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  EXPECT_EQ(r.Push(), KURA_OK);
  EXPECT_EQ(r.Push(), KURA_OK);
  EXPECT_EQ(r.End(), KURA_RENDERER_ERR_CLIP_UNBALANCED);
  // the frame is over, and the renderer works for the next one, which starts
  // with no clips
  EXPECT_EQ(r.End(), KURA_RENDERER_ERR_NOT_IN_FRAME);
  ASSERT_EQ(r.Begin(), KURA_OK);
  EXPECT_EQ(r.Pop(), KURA_RENDERER_ERR_CLIP_UNDERFLOW);
  EXPECT_EQ(r.End(), KURA_OK);
}

void TextMayBeEmpty(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  EXPECT_EQ(r.Text(nullptr, 0), KURA_OK);
  EXPECT_EQ(r.Text("", 0), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
}

void TextIsDrawnWhateverItContains(Fixture& r) {
  ASSERT_EQ(r.Begin(), KURA_OK);
  const std::string text = "line one\nline two \xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80 \"quoted\" \\ \t";
  EXPECT_EQ(r.Text(text.data(), text.size()), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
}

void MeasuringWorksOutsideAFrame(Fixture& r) {
  KuraTextMetrics metrics{};
  EXPECT_EQ(r.Measure("hello", 5, kStyle, &metrics), KURA_OK);
  EXPECT_TRUE(metrics.width > 0.0f);
  EXPECT_TRUE(metrics.height > 0.0f);
}

void MeasuringIsDeterministic(Fixture& r) {
  const auto first = r.Measure("Hello, world", 16.0f);
  const auto second = r.Measure("Hello, world", 16.0f);
  EXPECT_EQ(first.width, second.width);
  EXPECT_EQ(first.height, second.height);
  EXPECT_EQ(first.ascent, second.ascent);
  // and it isn't affected by drawing in between
  ASSERT_EQ(r.Begin(), KURA_OK);
  EXPECT_EQ(r.Text("Hello, world", 12), KURA_OK);
  EXPECT_EQ(r.End(), KURA_OK);
  const auto third = r.Measure("Hello, world", 16.0f);
  EXPECT_EQ(first.width, third.width);
  EXPECT_EQ(first.height, third.height);
}

void MeasuringUsesTheLengthNotTheTerminator(Fixture& r) {
  const char buffer[] = "abcdef";
  KuraTextMetrics three{};
  KuraTextMetrics six{};
  ASSERT_EQ(r.Measure(buffer, 3, kStyle, &three), KURA_OK);
  ASSERT_EQ(r.Measure(buffer, 6, kStyle, &six), KURA_OK);
  EXPECT_EQ(three.width, r.Measure("abc", 16.0f).width);
  EXPECT_TRUE(six.width > three.width);
}

void MeasuringBehavesLikeText(Fixture& r) {
  const auto nothing = r.Measure("", 16.0f);
  const auto one = r.Measure("a", 16.0f);
  const auto some = r.Measure("abcdefgh", 16.0f);
  EXPECT_EQ(nothing.width, 0.0f);
  EXPECT_TRUE(nothing.height > 0.0f);
  EXPECT_TRUE(one.width > 0.0f);
  EXPECT_TRUE(some.width > one.width);

  // another line makes it taller, not wider
  const auto two_lines = r.Measure("a\na", 16.0f);
  EXPECT_TRUE(two_lines.height > one.height);
  EXPECT_EQ(two_lines.width, one.width);

  // a bigger font makes it bigger
  const auto bigger = r.Measure("abcdefgh", 32.0f);
  EXPECT_TRUE(bigger.width > some.width);
  EXPECT_TRUE(bigger.height > some.height);
  EXPECT_TRUE(bigger.ascent > some.ascent);

  // the baseline is inside the box
  EXPECT_TRUE(some.ascent > 0.0f);
  EXPECT_TRUE(some.ascent <= some.height);
}

void MeasuringChecksItsArguments(Fixture& r) {
  KuraTextMetrics metrics{};
  EXPECT_EQ(r.Measure("a", 1, kStyle, nullptr), KURA_RENDERER_ERR_INVALID_ARG);
  EXPECT_EQ(r.Measure(nullptr, 3, kStyle, &metrics), KURA_RENDERER_ERR_INVALID_ARG);
  for (const auto size : {0.0f, -1.0f, kNaN, kInf})
    EXPECT_EQ(r.Measure("a", 1, KuraTextStyle{size}, &metrics), KURA_RENDERER_ERR_INVALID_ARG);
  EXPECT_EQ(r.Measure(nullptr, 0, kStyle, &metrics), KURA_OK);
}

struct NamedRule {
  const char* name;
  Rule rule;
};

constexpr NamedRule kRules[] = {
    {"OpsTable", &OpsTable},
    {"DrawingOutsideAFrameFails", &DrawingOutsideAFrameFails},
    {"DrawingInsideAFrameWorks", &DrawingInsideAFrameWorks},
    {"BeginFrameInsideAFrameFails", &BeginFrameInsideAFrameFails},
    {"FramesRepeat", &FramesRepeat},
    {"OversizedSurfaceIsRejected", &OversizedSurfaceIsRejected},
    {"EmptySurfaceIsAllowed", &EmptySurfaceIsAllowed},
    {"InvalidArgumentsAreRejected", &InvalidArgumentsAreRejected},
    {"ColorsOutOfRangeAreClamped", &ColorsOutOfRangeAreClamped},
    {"AnyFiniteGeometryIsAccepted", &AnyFiniteGeometryIsAccepted},
    {"ClipsNest", &ClipsNest},
    {"PoppingAnEmptyClipStackFails", &PoppingAnEmptyClipStackFails},
    {"EndingAFrameWithClipsFailsButClosesIt", &EndingAFrameWithClipsFailsButClosesIt},
    {"TextMayBeEmpty", &TextMayBeEmpty},
    {"TextIsDrawnWhateverItContains", &TextIsDrawnWhateverItContains},
    {"MeasuringWorksOutsideAFrame", &MeasuringWorksOutsideAFrame},
    {"MeasuringIsDeterministic", &MeasuringIsDeterministic},
    {"MeasuringUsesTheLengthNotTheTerminator", &MeasuringUsesTheLengthNotTheTerminator},
    {"MeasuringBehavesLikeText", &MeasuringBehavesLikeText},
    {"MeasuringChecksItsArguments", &MeasuringChecksItsArguments},
};
}  // namespace

namespace {
// One rule of the ABI, run against a renderer from the factory.
class ConformanceTest : public ::testing::Test {
 public:
  ConformanceTest(RendererFactory factory, const Rule rule) :
    factory_(std::move(factory)),
    rule_(rule) {}

  void TestBody() override {
    Fixture fixture(factory_);
    ASSERT_TRUE(fixture.Valid());
    rule_(fixture);
  }

 private:
  RendererFactory factory_;
  Rule rule_;
};
}  // namespace

void RegisterConformanceSuite(const std::string& name, RendererFactory factory) {
  for (const auto& entry : kRules) {
    const auto rule = entry.rule;
    ::testing::RegisterTest(("Conformance_" + name).c_str(), entry.name, nullptr, nullptr, __FILE__, __LINE__,
                            [factory, rule]() -> ::testing::Test* { return new ConformanceTest(factory, rule); });
  }
}
}  // namespace kura::test
