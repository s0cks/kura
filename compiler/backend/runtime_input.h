#ifndef KURA_RUNTIME_INPUT_H
#define KURA_RUNTIME_INPUT_H

// Inputs, as the runtime has them. An `#input` is a box whose text the view says (`value`) and the user changes: the
// view stays what the text is, so a change is a Message that goes through `update` and comes back as the next frame's
// `value`, like any other change in the program. What the runtime keeps is what the program has no say in: which
// input has the keyboard, where the caret and the selection are in it, and how far it is scrolled.
//
// Drawing (`kura_rt_draw_input`) tells it where each input is, as the view draws. The host tells it what happens:
// a click (`Click`), the characters that are typed (`Text`) and the keys that edit (`Press`), and is given back the
// Message that the change is, if there is one. The Session in session.h does that.
#include <string>
#include <string_view>

#include "backend/runtime_abi.h"

namespace kura::rt::input {
// The keys that mean something to an input. The host turns its own key codes into these.
enum Key : int {
  kBackspace = 1,
  kDelete,
  kLeft,
  kRight,
  kHome,
  kEnd,
  kSelectAll,
  kEscape,
};

// What is held down with a key. Control also makes the arrows, backspace and delete go by words.
enum Mod : int {
  kShift = 1,
  kControl = 2,
};

// What `Text` and `Press` did, as a bit set: 0 is nothing, and the input is not what the key was for.
enum Result : int {
  // the key was an input's: the host should draw again
  kHandled = 1,
  // the text changed, and `message` is what it means: the host sends it
  kMessage = 2,
};

// An input as it is drawn: where it is, what is in it, and what it is made of.
struct Field {
  // what the input is called while the program runs, among the others: a layout context and a node in it
  const void* owner = nullptr;
  uint64_t node = 0;
  KuraRect box{};
  float padding_left = 0;
  float padding_right = 0;
  std::string text{};
  float size = 16;
  std::string family{};
  // 0xRRGGBBAA, with the opacity in it already
  KuraColor color = KuraColor{.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f};
  // 0 at the start, 1 in the middle and 2 at the end: for text that fits
  int align = 0;
  std::string placeholder{};
  // the open variant that `on-input` is, or None
  KuraValue message{kTagNone, 0};
  bool readonly = false;
};

// Draws the input, and says where it is for the frame's clicks.
void Draw(const Field& field);

// The frame that is drawn begins and ends: an input that was not drawn is gone, and its state with it.
void BeginFrame();
void EndFrame();
// Forgets where the inputs are, as `kura_rt_events_clear` forgets where the events are.
void ClearPlaces();
// Forgets everything, the focus too.
void Reset();

// The pointer was pressed at (x, y), in the coordinates of the last frame. If that is on an input, it has the keyboard,
// with the caret where it was pressed, and true is returned; anywhere else, nothing has it.
auto Click(double x, double y) -> bool;
// What was typed or pasted. Returns the bits of `Result`.
auto Text(std::string_view utf8, KuraValue* message) -> int;
// A key, with what was held (the bits of `Mod`). Returns the bits of `Result`.
auto Press(Key key, int mods, KuraValue* message) -> int;

// Whether an input has the keyboard.
auto HasFocus() -> bool;
// What is selected in the input that has the keyboard, for a copy or a cut: empty if there is none.
auto SelectedText() -> std::string;
// Whether the input that has the keyboard is blinking its caret: a host that draws only when something changes has to
// draw again for it.
auto Blinking() -> bool;
// What time it is, in seconds: the caret blinks by it. A host that never says gets the system's clock.
void SetClock(double seconds);
}  // namespace kura::rt::input

#endif  // KURA_RUNTIME_INPUT_H
