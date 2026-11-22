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

// Where the objects a program allocates live. The runtime doesn't take a space with every allocation: the phase of the
// program says. While a `view` runs, what it makes is for one frame, so it goes in the frame's arena, which is reset
// when the next frame begins; while `init` or `update` run, and for anything the host makes, it goes in the model
// space, where most of it is garbage soon and the rest is the model. (A value read from the model can be used by the
// view as it is, but never the other way: see Session::Send, which moves a Message out of the frame.)
enum class Phase {
  kModel,
  kFrame,
  // The program is being loaded: what its top-level bindings make never moves and is never freed, so it goes in the
  // tenured space, with the constants.
  kConstant,
};

void SetPhase(Phase phase);
auto GetPhase() -> Phase;

// Sets the phase for as long as it lives.
class PhaseScope {
 private:
  Phase previous_;

 public:
  explicit PhaseScope(const Phase phase) :
    previous_(GetPhase()) {
    SetPhase(phase);
  }
  ~PhaseScope() {
    SetPhase(previous_);
  }
  PhaseScope(const PhaseScope&) = delete;
  auto operator=(const PhaseScope&) -> PhaseScope& = delete;
};

// A variant from the tag of one (the payload is boxed in the current phase's space), for what makes a Message of a
// variant that something else named, like what is typed into an input.
auto MakeVariantWithTag(uint64_t tag, const KuraValue& payload) -> KuraValue;

// A value that can be kept: everything it reaches that is in the frame's arena is copied into the model space, and the
// rest (the model's, the tenured space's, code) is shared, because it will outlive the frame. A Message made by `view`
// is moved this way before `update` sees it, so that nothing in the model can point into a frame that is reset.
auto MoveOutOfFrame(const KuraValue& value) -> KuraValue;

// The text of a String (or a Color) value: `bits` is its `bits`. It lives as long as the string object.
auto StringView(uint64_t bits) -> std::string_view;

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
  // The node is a scroll container: the wheel moves its content (see `kura_rt_event_scroll`), and the Message, if
  // it has one, is what it means that the content moved.
  kEventScroll = 2,
  // The node has a tooltip: the message is the String to show, when the pointer has been over it for a while.
  kEventTooltip = 3,
  // The node is an input: the runtime keeps it, as `kura_rt_draw_input` draws it, and the message is the index of the
  // input among the frame's (see runtime_input.h). A click on it gives the input the keyboard.
  kEventInput = 4,
};

auto MakeString(std::string_view value) -> KuraValue;
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
  V(kura_rt_vec_from)             \
  V(kura_rt_seq_from_vec)         \
  V(kura_rt_seq_concat)           \
  V(kura_rt_len)                  \
  V(kura_rt_store_index)          \
  V(kura_rt_record_new)           \
  V(kura_rt_record_count)         \
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
  V(kura_rt_stack_seq)            \
  V(kura_rt_stack_record)         \
  V(kura_rt_stack_closure)        \
  V(kura_rt_stack_box)            \
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
  V(kura_rt_paint_box)            \
  V(kura_rt_push_clip)            \
  V(kura_rt_pop_clip)             \
  V(kura_rt_draw_rect)            \
  V(kura_rt_draw_text)            \
  V(kura_rt_draw_input)           \
  V(kura_rt_draw_line)            \
  V(kura_rt_viewport_width)       \
  V(kura_rt_viewport_height)      \
  V(kura_rt_layout_set_text)      \
  V(kura_rt_layout_set)           \
  V(kura_rt_layout_run)           \
  V(kura_rt_layout_slot_begin)    \
  V(kura_rt_layout_slot_instance) \
  V(kura_rt_layout_slot_end)      \
  V(kura_rt_layout_rect)          \
  V(kura_rt_register_event)       \
  V(kura_rt_seq_append)           \
  V(kura_rt_destroy)              \
  V(kura_rt_record_clone)
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
// Geometry from a list: `lanes` is 2 for a Vec2, 3 for a Vec3 and 4 for a Rect, and `out` is that many floats.
// The value is a list of exactly that many numbers, all Int or all Float; anything else is a panic.
void kura_rt_vec_from(uint64_t tag, uint64_t bits, uint64_t lanes, float* out);
// And back: geometry as a value of any type is a list of its floats, `lanes` of them from `values`.
void* kura_rt_seq_from_vec(const float* values, uint64_t lanes);
void* kura_rt_seq_concat(void* a, void* b);

int64_t kura_rt_len(void* object);
void kura_rt_store_index(void* object, int64_t index, uint64_t tag, uint64_t bits);
void kura_rt_destroy(void* ptr);
void* kura_rt_record_clone(void* src_ptr);

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
void kura_rt_fill_rect(const KuraRect* rect, uint64_t color);
// A box: the rect filled with `fill`, with a border of `border_width` inside its edge and corners
// rounded by `radius`, both in pixels, all with `opacity` (0 to 1) multiplied into their alpha. Colors are 0xRRGGBBAA.
// It is made of the renderer's `fill_rect`s, a row of the rounded part at a time, so it has hard edges.
void kura_rt_paint_box(const KuraRect* rect, uint64_t fill, uint64_t border_color, uint64_t border_width_bits,
                       uint64_t radius_bits, uint64_t opacity_bits);
// What is drawn until `pop_clip` stays inside the rect.
void kura_rt_push_clip(const KuraRect* rect);
void kura_rt_pop_clip();
void* kura_rt_seq_append(void* seq, uint64_t item_tag, uint64_t item_bits);

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
void kura_rt_draw_text(const void* table, uint64_t count, uint64_t node, uint64_t color, uint64_t opacity_bits,
                       uint64_t align);
// Draws the input node `node` of the layout `table`: its text in the font and size it was given with
// `kura_rt_layout_set_text`, or the placeholder (a String, or anything else for none) when that is empty, inside its
// padding and clipped to it; and when the input has the keyboard, its selection and its caret. `message` is what
// `on-input` is, an open variant or None, and `readonly` a Bool: see runtime_input.h.
void kura_rt_draw_input(const void* table, uint64_t count, uint64_t node, uint64_t color, uint64_t opacity_bits,
                        uint64_t align, uint64_t placeholder_tag, uint64_t placeholder_bits, uint64_t message_tag,
                        uint64_t message_bits, uint64_t readonly_tag, uint64_t readonly_bits);
// The renderer call under `kura_rt_draw_text`: draws one run of text with the top-left of its box at (x, y).
// `family` is the `font-family`, NULL (or a zero length) for the default font. Not called by compiled code.
void kura_rt_render_text(double x, double y, const char* utf8, uint64_t length, float size, const char* family,
                         uint64_t family_length, uint64_t color);
void kura_rt_draw_line(const KuraVec2* start, const KuraVec2* end, uint64_t color);
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
// Forgets everything the runtime kept of the layouts on this thread: the texts, the instances and how far each
// container is scrolled. A table is known by its address, and a program that is unloaded gives its addresses to
// the next, so the program does this when it goes.
void kura_rt_layout_reset();
// A dynamic hole is a `slot` node of a table. `slot_begin` starts its list of instances over, `slot_instance` makes
// the next one (a copy of the template table `instance`, or the one that was there last time if it is the same table)
// and makes the template's table mean that instance until the next call, and `slot_end` drops the instances that
// weren't made. Setting, drawing and reading boxes through a template's table works on its instance.
void kura_rt_layout_slot_begin(const void* table, uint64_t count, uint64_t slot);
void kura_rt_layout_slot_instance(const void* table, uint64_t count, uint64_t slot, const void* instance,
                                  uint64_t instance_count);
void kura_rt_layout_slot_end(const void* table, uint64_t count, uint64_t slot);
// The box the node `node` of the table was laid out in, as of the last `kura_rt_layout_run`, in the viewport's
// coordinates. A node that isn't there has an empty box at the origin.
void kura_rt_layout_rect(const void* table, uint64_t count, uint64_t node, KuraRect* out);

// A record with room for `capacity` fields: a record is as big as it is made, and a field set past that is an error.
void* kura_rt_record_new(uint64_t capacity);
uint64_t kura_rt_record_count(void* object);
void kura_rt_record_set(void* object, uint64_t hash, const char* name, uint64_t tag, uint64_t bits);
KuraValue kura_rt_record_get(void* object, uint64_t hash);
void kura_rt_record_spread(void* dst, void* src);
void* kura_rt_record_merge(void* a, void* b);
KuraValue kura_rt_get_dyn(uint64_t tag, uint64_t bits, uint64_t hash);
KuraValue kura_rt_get_safe(uint64_t tag, uint64_t bits, uint64_t hash);

void* kura_rt_node_new(const char* kind);

void* kura_rt_closure_new(void* function, uint64_t arity, uint64_t captures);
// Objects in a block of the caller's stack, where the escape analysis found they don't outlive the call: `memory` is
// the block, which the code generator made as big as the object is, and these make it one (a header that says it isn't
// the heap's, and the fixed fields) and zero the rest.
void kura_rt_stack_seq(void* memory, uint64_t capacity);
void kura_rt_stack_record(void* memory, uint64_t capacity);
void kura_rt_stack_closure(void* memory, void* function, uint64_t arity, uint64_t captures);
void kura_rt_stack_box(void* memory, uint64_t tag, uint64_t bits);
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
// What is drawn inside a clip can only be hit inside it. The renderer's clips are the ones to follow: see
// `kura_rt_push_clip`, which calls these.
void kura_rt_event_clip_push(double x, double y, double width, double height);
void kura_rt_event_clip_pop();
uint64_t kura_rt_event_count();
// The Message of the last place of `kind` drawn that contains (x, y), which is the topmost one: a parent is
// drawn before its children. Returns 1 and sets `message`, or 0 and sets nothing if there is none.
int kura_rt_event_hit(uint64_t kind, double x, double y, KuraValue* message);

// How a tooltip looks and when it comes: where the host has nothing to say, these are the defaults. Sizes are in
// pixels, the colors are 0xRRGGBBAA, and `delay` is in seconds.
typedef struct KuraTooltipStyle {
  double delay;  // how long the pointer is over something before its tooltip shows
  float font_size;
  float max_width;  // the widest a tooltip is, with its padding: a longer text is wrapped
  float padding_x;
  float padding_y;
  float radius;
  float gap;     // between the node and the tooltip
  float margin;  // the least between the tooltip and the edge of the viewport
  uint64_t background;
  uint64_t color;
} KuraTooltipStyle;

// Host => UI. The pointer is at (x, y), in the coordinates of the frames, or has left the window. What is under
// the pointer when a frame ends is what its tooltip is shown for.
void kura_rt_pointer_moved(double x, double y);
void kura_rt_pointer_left();
// Host => UI. What time it is, in seconds from when the host likes: tooltips show after the pointer has been
// somewhere for a while, and this is how they know. A host that never says gets the system's clock.
void kura_rt_set_time(double seconds);
// Hides the tooltip that is showing, or is about to: a click, or the wheel. It stays hidden until the pointer is
// over another node.
void kura_rt_tooltip_dismiss();
// Whether the pointer is over a tooltip that is not showing yet, because its delay isn't over: a host that draws
// only when something changes has to draw again when it is.
int kura_rt_tooltip_pending();
void kura_rt_tooltip_configure(const KuraTooltipStyle* style);
KuraTooltipStyle kura_rt_tooltip_style();
KuraTooltipStyle kura_rt_tooltip_default_style();
// Forgets what the pointer was over, waiting for, or had hidden. A program that is unloaded does this when it goes.
void kura_rt_tooltip_reset();
// Draws the tooltip the pointer is over, if it is time: the end of a frame does, after everything else.
void kura_rt_tooltip_draw();
// The topmost place of a kind that the point is in, as of the last frame: its box and its message.
int kura_rt_event_topmost(uint64_t kind, double x, double y, KuraRect* box, KuraValue* message);

// UI => host. The wheel (or a drag) moved by (dx, dy) pixels at (x, y), in the coordinates of the last frame, down
// and to the right being positive. The topmost scroll container at that point that can move that way does, by as
// much as it has room for, and the next frame is laid out with its content moved. Returns a bit set:
//   1  something scrolled: the host should render again
//   2  `message` is set, because what scrolled has an `on-scroll`: the host sends it (the Session does that)
// 0 means nothing moved, and the wheel is the host's.
int kura_rt_event_scroll(double x, double y, double dx, double dy, KuraValue* message);
// The containers a frame's drawing registered, forgotten with the places.
void kura_rt_scrollers_clear();
}

#endif  // KURA_RUNTIME_ABI_H
