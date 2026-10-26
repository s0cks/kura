#ifndef KURA_TESTS_CONFORMANCE_H
#define KURA_TESTS_CONFORMANCE_H

// The renderer ABI conformance suite: the rules of renderer_abi.h that every
// renderer has to follow, written as tests that run against any renderer.
//
// A platform renderer's test executable links kura-renderer-conformance and
// registers itself:
//
//   kura::test::RegisterConformanceSuite("OpenGL", [] {
//     KuraRenderer renderer{};
//     CreateMyOffscreenRenderer(&renderer);
//     return renderer;
//   });
//
// The factory is called once per test, and each test calls `ops->destroy` on
// what it got. What the suite checks is the contract (return codes, frame and
// clip state, argument checking, text measuring), never pixels: GPUs differ at
// the edges of shapes, and the headless renderer's own tests check those.

#include <functional>
#include <string>

#include "abi_print.h"
#include "renderer/renderer_abi.h"

namespace kura::test {
using RendererFactory = std::function<KuraRenderer()>;

// Registers one GTest test per rule, in the suite "Conformance_<name>" (so
// --gtest_filter=Conformance_<name>.* runs them). Call it before the tests run:
// from a static initializer or from main.
void RegisterConformanceSuite(const std::string& name, RendererFactory factory);
}  // namespace kura::test

#endif  // KURA_TESTS_CONFORMANCE_H
