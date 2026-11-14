#ifndef KURA_RUNTIME_ABI_H
#define KURA_RUNTIME_ABI_H

#include <cstdint>
#include <string>
#include <string_view>

#include "renderer/renderer_abi.h"

namespace kura::rt {
struct KuraValue {
  uint64_t tag;
  uint64_t bits;
};

enum Tag : uint64_t {
  kTagInvalid = 0,
  kTagNone = 1,
  kTagBool = 2,
  kTagInt = 3,
  kTagFloat = 4,
  kTagString = 5,
  kTagColor = 6,
  kTagPx = 7,
  kTagPct = 8,
  kTagFr = 9,
  kTagNode = 10,
  kTagSeq = 11,
  kTagRecord = 12,
  kTagFunction = 13,
  kTagHandle = 14,
  kTagVoid = 15,
};

static constexpr const uint64_t kVariantBit = 1ULL << 63;

// FNV-1a
static inline constexpr auto HashName(const std::string_view name) -> uint64_t {
  uint64_t hash = 14695981039346656037ULL;
  for (const char c : name) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

static inline constexpr auto VariantTag(const std::string_view name) -> uint64_t {
  return HashName(name) | kVariantBit;
}

static inline constexpr auto IsVariantTag(const uint64_t tag) -> bool {
  return (tag & kVariantBit) != 0;
}

auto Format(const KuraValue& value) -> std::string;

// A variant, which is what a Message is: `MakeVariant("Increment")`, or with a payload,
// `MakeVariant("Set", &five)`. The payload is copied. It is the same value compiled code builds for
// `Increment` or `Set(5)`, so a host can send it to `update`.
auto MakeVariant(std::string_view name, const KuraValue* payload = nullptr) -> KuraValue;

// ── Built-in messages ──
// What the runtime and the host tell a UI about the world around it (see frontend/builtins.h for the types, and
// the MVU guide for how a program asks for them). The host builds them with these, and sends them with
// `Session::Send` like any other Message, or with the Session's `Resized` and `Focused`.
auto MakeInt(int64_t value) -> KuraValue;
auto MakeFloat(double value) -> KuraValue;
auto MakeBool(bool value) -> KuraValue;
// The built-in `Size`: `{ width, height }`, in pixels.
auto MakeSize(double width, double height) -> KuraValue;
// `Resized(Size)`: the viewport is now this big.
auto MakeResized(double width, double height) -> KuraValue;
// `Focused(Bool)`: the UI gained (true) or lost (false) the host's input focus.
auto MakeFocused(bool focused) -> KuraValue;
// Whether `message` is one of the built-in messages.
auto IsBuiltinMessage(const KuraValue& message) -> bool;

// What a place on the screen can mean to the view. Only clicks are routed so far.
enum EventKind : uint64_t {
  kEventClick = 1,
};

auto MakeString(const std::string& value) -> KuraValue;
}  // namespace kura::rt

// clang-format off
#define KURA_RUNTIME_FUNCTIONS(V) \
  V(kura_rt_panic)                \
  V(kura_rt_expect_tag)           \
  V(kura_rt_register_variant)     \
  V(kura_rt_string_new)           \
  V(kura_rt_string_concat)        \
  V(kura_rt_string_cmp)           \
  V(kura_rt_seq_new)              \
  V(kura_rt_seq_get)              \
  V(kura_rt_seq_concat)           \
  V(kura_rt_len)                  \
  V(kura_rt_store_index)          \
  V(kura_rt_record_new)           \
  V(kura_rt_record_set)           \
  V(kura_rt_record_get)           \
  V(kura_rt_record_spread)        \
  V(kura_rt_record_merge)         \
  V(kura_rt_get_dyn)              \
  V(kura_rt_get_safe)             \
  V(kura_rt_node_new)             \
  V(kura_rt_closure_new)          \
  V(kura_rt_closure_capture)      \
  V(kura_rt_closure_get_capture)  \
  V(kura_rt_closure_fn)           \
  V(kura_rt_box)                  \
  V(kura_rt_unbox)                \
  V(kura_rt_binop)                \
  V(kura_rt_unop)                 \
  V(kura_rt_eq)                   \
  V(kura_rt_sdiv)                 \
  V(kura_rt_srem)                 \
  V(kura_rt_string_cstr)          \
  V(kura_rt_string_from_cstr)     \
  V(kura_rt_string_from_cstr_opt) \
  V(kura_rt_ffi_overflow)         \
  V(kura_rt_ffi_error)            \
  V(kura_rt_check_renderer)       \
  V(kura_rt_begin_frame)          \
  V(kura_rt_end_frame)            \
  V(kura_rt_fill_rect)            \
  V(kura_rt_draw_rect)            \
  V(kura_rt_draw_text)            \
  V(kura_rt_viewport_width)       \
  V(kura_rt_viewport_height)      \
  V(kura_rt_layout_set_text)      \
  V(kura_rt_layout_set)           \
  V(kura_rt_layout_run)           \
  V(kura_rt_layout_slot_begin)    \
  V(kura_rt_layout_slot_instance) \
  V(kura_rt_layout_slot_end)      \
  V(kura_rt_layout_get)           \
  V(kura_rt_register_event)
// clang-format on

extern "C" {
using kura::rt::KuraValue;

[[noreturn]] void kura_rt_panic(const char* message);
void kura_rt_expect_tag(uint64_t tag, uint64_t expected);
void kura_rt_register_variant(uint64_t hash, const char* name);

void* kura_rt_string_new(const char* data, uint64_t length);
void* kura_rt_string_concat(void* a, void* b);
int64_t kura_rt_string_cmp(void* a, void* b);

void* kura_rt_seq_new(uint64_t capacity);
KuraValue kura_rt_seq_get(void* seq, int64_t index);
void* kura_rt_seq_concat(void* a, void* b);

int64_t kura_rt_len(void* object);
void kura_rt_store_index(void* object, int64_t index, uint64_t tag, uint64_t bits);

// ── rendering ──────────────────────────────────────────────────────────────
// The draw calls compiled code makes. They forward to the renderer the host gave this thread (see
// kura_rt_set_renderer), so the runtime knows nothing about which renderer that is.
//
// A draw call's arguments are what compiled code has: coordinates are the bit patterns of f64s,
// and the color is a packed RGBA8 (0xRRGGBBAA). The status the renderer returns is kept, and
// `kura_rt_check_renderer` panics if the last draw call failed (debug builds emit a call to it
// after every draw call).
void kura_rt_check_renderer();
void kura_rt_begin_frame(uint32_t width, uint32_t height);
void kura_rt_end_frame();
void kura_rt_fill_rect(uint64_t xbits, uint64_t ybits, uint64_t wbits, uint64_t hbits, uint64_t color);

// Called by the host, not by compiled code. Sets the renderer this thread draws with; the host
// creates it, owns it and destroys it, and the runtime keeps only a copy of the handle. The
// renderer is thread-local: a thread that draws has to set its own. A NULL `renderer` clears it.
// Returns KURA_RENDERER_ERR_INVALID_ARG, and leaves the current renderer alone, if the ops
// table isn't one this runtime can use.
KuraRendererStatus kura_rt_set_renderer(const KuraRenderer* renderer);
// The renderer set on this thread, or NULL.
const KuraRenderer* kura_rt_get_renderer();
void kura_rt_draw_rect();
// Draws the text node `node` of the layout `table` (see below): its content, in the font and size it was given
// with `kura_rt_layout_set_text`, wrapped to its box and placed in it. `color` is a packed RGBA8.
void kura_rt_draw_text(const void* table, uint64_t count, uint64_t node, uint64_t color);
// The renderer call under `kura_rt_draw_text`: draws one run of text with the top-left of its box at (x, y).
// `family` is the `font-family`, NULL (or a zero length) for the default font. Not called by compiled code.
void kura_rt_render_text(double x, double y, const char* utf8, uint64_t length, float size, const char* family,
                         uint64_t family_length, uint64_t color);
// ── viewport and layout ────────────────────────────────────────────────────
// The size of the surface a view lays out in and draws into. The host sets it (a window's size, say)
// and a view reads it at the start of every frame, so changing it reflows the next frame. It is
// thread-local like the renderer, and defaults to 1024x1024.
void kura_rt_set_viewport(uint32_t width, uint32_t height);
uint32_t kura_rt_viewport_width();
uint32_t kura_rt_viewport_height();

// Measures text with this thread's renderer, outside of a frame. Returns 0 and sets `width` and
// `height`, or non-zero (and sets nothing) when there is no renderer or it couldn't measure. `family` is as
// for `kura_rt_render_text`.
int kura_rt_measure_text(const char* utf8, uint64_t length, float size, const char* family, uint64_t family_length,
                         float* width, float* height);

// What compiled code lays a view out with. `table` is the constant table of `count` layout nodes
// (layout/layout.h) that the compiler emitted for one DOM tree; it also keys the layout state kept
// for that tree on this thread, so the tree is only laid out again when something it depends on
// changed: a text, or the viewport.
//
// `set_text` gives a text node its content, font size and font family (the family's bytes, NULL for the default
// font); `run` lays the tree out if it needs it;
// `get` reads a node's box back (component 0, 1, 2, 3 is x, y, width, height).
void kura_rt_layout_set_text(const void* table, uint64_t count, uint64_t node, void* text, double size,
                             const char* family, uint64_t family_length);
// `set` changes a node's style property (a layout::Property) to a value known only at run time.
void kura_rt_layout_set(const void* table, uint64_t count, uint64_t node, uint64_t property, uint64_t tag,
                        uint64_t bits);
void kura_rt_layout_run(const void* table, uint64_t count);
// A dynamic hole is a `slot` node of a table. `slot_begin` starts its list of instances over, `slot_instance` makes
// the next one (a copy of the template table `instance`, or the one that was there last time if it is the same table)
// and makes the template's table mean that instance until the next call, and `slot_end` drops the instances that
// weren't made. Setting, drawing and reading boxes through a template's table works on its instance.
void kura_rt_layout_slot_begin(const void* table, uint64_t count, uint64_t slot);
void kura_rt_layout_slot_instance(const void* table, uint64_t count, uint64_t slot, const void* instance,
                                  uint64_t instance_count);
void kura_rt_layout_slot_end(const void* table, uint64_t count, uint64_t slot);
double kura_rt_layout_get(const void* table, uint64_t count, uint64_t node, uint64_t component);

void* kura_rt_record_new();
void kura_rt_record_set(void* object, uint64_t hash, const char* name, uint64_t tag, uint64_t bits);
KuraValue kura_rt_record_get(void* object, uint64_t hash);
void kura_rt_record_spread(void* dst, void* src);
void* kura_rt_record_merge(void* a, void* b);
KuraValue kura_rt_get_dyn(uint64_t tag, uint64_t bits, uint64_t hash);
KuraValue kura_rt_get_safe(uint64_t tag, uint64_t bits, uint64_t hash);

void* kura_rt_node_new(const char* kind);

void* kura_rt_closure_new(void* function, uint64_t arity, uint64_t captures);
void kura_rt_closure_capture(void* closure, uint64_t index, uint64_t tag, uint64_t bits);
KuraValue kura_rt_closure_get_capture(void* closure, uint64_t index);
void* kura_rt_closure_fn(void* closure, uint64_t arguments);
void* kura_rt_box(uint64_t tag, uint64_t bits);
KuraValue kura_rt_unbox(void* box);

KuraValue kura_rt_binop(uint64_t op, uint64_t a_tag, uint64_t a_bits, uint64_t b_tag, uint64_t b_bits);
KuraValue kura_rt_unop(uint64_t op, uint64_t tag, uint64_t bits);
uint64_t kura_rt_eq(uint64_t a_tag, uint64_t a_bits, uint64_t b_tag, uint64_t b_bits);
int64_t kura_rt_sdiv(int64_t a, int64_t b);
int64_t kura_rt_srem(int64_t a, int64_t b);

const char* kura_rt_string_cstr(void* string);
void* kura_rt_string_from_cstr(const char* data, const char* what);
void* kura_rt_string_from_cstr_opt(const char* data);
[[noreturn]] void kura_rt_ffi_overflow(int64_t value, const char* what);
[[noreturn]] void kura_rt_ffi_error(const char* message);
}

// ── events ─────────────────────────────────────────────────────────────────
// UI => host. A node with an event property (`#button(on-click: Increment)`) tells the runtime, as the
// view draws it, that its box means a Message when that kind of event happens inside it. The runtime keeps
// those places for the frame, in drawing order, and a host asks which one an event landed on. The
// Session in session.h does that, and sends the Message it finds through `update` like any other.
//
// The places belong to the thread that ran the view, like the renderer, and a frame forgets them: a view
// begins its frame with `kura_rt_begin_frame`, which clears them, before it records its own.
// `kura_rt_events_clear` does the same for a host that wants them gone sooner. A Message is valid until the places
// are cleared.
extern "C" {
// Called by compiled code, after layout, with the Message the node's property evaluated to.
void kura_rt_register_event(const void* table, uint64_t count, uint64_t node, uint64_t kind, uint64_t tag,
                            uint64_t bits);
// The registration under it: the place is the rectangle (x, y, width, height), in the coordinates the view
// was drawn in.
void kura_rt_event_add(double x, double y, double width, double height, uint64_t kind, uint64_t tag, uint64_t bits);
void kura_rt_events_clear();
uint64_t kura_rt_event_count();
// The Message of the last place of `kind` drawn that contains (x, y), which is the topmost one: a parent is
// drawn before its children. Returns 1 and sets `message`, or 0 and sets nothing if there is none.
int kura_rt_event_hit(uint64_t kind, double x, double y, KuraValue* message);
}

#endif  // KURA_RUNTIME_ABI_H
