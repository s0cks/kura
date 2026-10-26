// The headless renderer against the ABI conformance suite, with and without a
// framebuffer: the contract doesn't depend on whether pixels are drawn.
#include "conformance.h"
#include "renderer/renderer_headless.h"

namespace {
auto Create(const bool rasterize) -> KuraRenderer {
  KuraHeadlessOptions options{};
  options.rasterize = rasterize ? 1 : 0;
  KuraRenderer renderer{};
  if (kura_headless_renderer_create(&options, &renderer) != KURA_RENDERER_OK)
    return KuraRenderer{};
  return renderer;
}

struct Registration {
  Registration() {
    kura::test::RegisterConformanceSuite("Headless", [] { return Create(true); });
    kura::test::RegisterConformanceSuite("HeadlessWithoutPixels", [] { return Create(false); });
  }
};

const Registration registration{};
}  // namespace
