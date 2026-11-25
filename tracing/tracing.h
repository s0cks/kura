#ifndef KURA_TRACING_H
#define KURA_TRACING_H

// Tracy instrumentation for the parts of Kura that are plain C++ (the runtime, the text library, the renderers).
// Compiled code gets its zones from the compiler instead (`kurac --trace`, see runtime_tracy.cc).
//
// With tracing support off (-DKURA_ADD_TRACING_SUPPORT=OFF) every macro here is nothing, so an instrumented hot path
// costs exactly what it did before. With it on, a zone costs about 2ns while no profiler is connected to it.
//
// The names have to be string literals: Tracy keeps the pointer, not the text.
#ifdef KURA_TRACING_ENABLED
#include <tracy/Tracy.hpp>

#define KURA_ZONE(name) ZoneScopedN(name)
#define KURA_ZONE_C(name, color) ZoneScopedNC(name, color)
// A text for the zone that is open, e.g. what string is being shaped.
#define KURA_ZONE_TEXT(text, size) ZoneText(text, size)
#define KURA_FRAME_MARK() FrameMark
#define KURA_PLOT(name, value) TracyPlot(name, static_cast<int64_t>(value))
#define KURA_PLOT_F(name, value) TracyPlot(name, static_cast<double>(value))
#define KURA_MESSAGE(text) TracyMessageL(text)
#else
#define KURA_ZONE(name) ((void)0)
#define KURA_ZONE_C(name, color) ((void)0)
#define KURA_ZONE_TEXT(text, size) ((void)0)
#define KURA_FRAME_MARK() ((void)0)
#define KURA_PLOT(name, value) ((void)0)
#define KURA_PLOT_F(name, value) ((void)0)
#define KURA_MESSAGE(text) ((void)0)
#endif  // KURA_TRACING_ENABLED

// The colors the zones of one kind share, so a trace reads at a glance: what the compiler's functions are, what
// layout is, what the renderer is.
namespace kura::tracing {
constexpr unsigned kColorLayout = 0x3a9ad9;
constexpr unsigned kColorText = 0xd98a3a;
constexpr unsigned kColorRender = 0x6abf4b;
constexpr unsigned kColorHeap = 0xb04bbf;
constexpr unsigned kColorTest = 0xd9534f;
}  // namespace kura::tracing

#endif  // KURA_TRACING_H
