#ifndef KURA_SESSION_H
#define KURA_SESSION_H

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "backend/program.h"
#include "backend/runtime_abi.h"
#include "backend/runtime_input.h"

namespace kura {
// A running UI: a Program, the state its `init` and `update` produce, and the way Messages get in and out.
//
// Messages go both ways, and both go through the same path:
//
//   host => UI   `Send(message)`: the host builds a Message (`rt::MakeVariant("Increment")`) and sends it.
//   UI => host   `Click(x, y)`: the host says where a click landed, and the Message the view gave the
//                node there (`#button(on-click: Increment)`) is sent, as if the host had sent it.
//
// A Message, whichever way it came, is first given to the program's `update`, which makes the next state.
// After that it is given to the host's listeners, in the order they were added. A listener is secondary to
// `update`: it is told the Message after the state has changed, and sees the new state.
//
// A program with no `update` has no state to change, so a Message goes straight to the listeners. That is how
// a host can use Messages as events from a UI that doesn't keep state.
//
// A Session, like the renderer, belongs to the thread that uses it: the places where a click means a
// Message are recorded on the thread that rendered the view, so `Render` and `Click` go on the same thread.
class Session {
 public:
  // What a listener is told about a Message that has been through `update`. The references are valid
  // for the call only.
  struct Event {
    const rt::KuraValue& message;
    const rt::KuraValue& previous;  // the state before `update`
    const rt::KuraValue& state;     // the state after: the same as `previous` if `update` didn't change it
  };

  using Listener = std::function<void(const Event&)>;
  using ListenerId = uint64_t;

 private:
  Program* program_;
  rt::KuraValue state_{rt::kTagNone, 0};
  bool has_update_ = false;
  bool has_view_ = false;
  std::vector<std::pair<ListenerId, Listener>> listeners_{};
  ListenerId next_listener_ = 1;
  // A Message sent while another is being delivered (by a listener, say) waits for its turn, so that
  // Messages are delivered one at a time and in the order they were sent.
  std::deque<rt::KuraValue> queue_{};
  bool delivering_ = false;

  explicit Session(Program* program) :
    program_(program) {}

  auto Deliver(const rt::KuraValue& message, std::string* error) -> bool;

 public:
  ~Session() = default;
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;

  // Starts the program: the state is what `init` returns. A program with no `init` starts with no state, for
  // the host to give it with `SetState` (or not, if its view takes none). The program has to outlive the
  // session.
  static auto Create(Program* program, std::string* error) -> std::unique_ptr<Session>;

  auto GetState() const -> const rt::KuraValue& {
    return state_;
  }

  // The host building the state itself, as the docs allow when there is no `init`.
  void SetState(const rt::KuraValue& state) {
    state_ = state;
  }

  // host => UI. Delivers `message` through `update`, then to the listeners. False, with `error` set, if `update`
  // couldn't run: the state is as it was and the listeners aren't told. A Message sent from a listener is
  // queued and delivered once the one being delivered is done, and this returns true for it at once.
  auto Send(const rt::KuraValue& message, std::string* error) -> bool;

  // The built-in messages (see frontend/builtins.h), sent like any other. A program gets the ones its Message
  // type lists; for one that doesn't, `update` is skipped (not an error) and the listeners still hear it.
  //   Resized(Size)   the viewport is now this big, in the pixels the UI is drawn in. Set the viewport too
  //                   (`kura_rt_set_viewport`): that is what the view draws into, and this is how it finds out.
  //   Focused(Bool)   the UI gained or lost the host's input focus.
  auto Resized(double width, double height, std::string* error) -> bool;
  auto Focused(bool focused, std::string* error) -> bool;

  // Listeners hear every Message, from either side, after `update` has. Removing one while a Message is being
  // delivered takes effect for the next listener that hasn't been called yet.
  auto AddListener(Listener listener) -> ListenerId;
  auto RemoveListener(ListenerId id) -> bool;

  // Runs the program's `view` on the state, which draws a frame with the thread's renderer, at the size of the
  // viewport the host set with `kura_rt_set_viewport`, and records where a click means a Message.
  auto Render(std::string* error) -> bool;

  // UI => host. Says that a click landed at (x, y), in the coordinates of the last frame. If it landed on a
  // node with an `on-click`, that Message is sent, `*handled` is set, and the result is `Send`'s. A click that
  // landed on nothing isn't an error: `*handled` is false.
  auto Click(double x, double y, bool* handled, std::string* error) -> bool;

  // Host => UI. Inputs: a `#input` takes the keyboard when it is clicked (`Click` does that, and gives it up when the
  // click is anywhere else), and the host then says what is typed and what keys are pressed. What they change is the
  // text, and the text is the program's: the input's `on-input` Message is sent with the new text in it, as `Send`
  // does, and the next `Render` shows what `update` made of it. `*handled` is whether the keyboard was an input's, and
  // so whether the host should render again and leave the key alone; it is false when nothing has the keyboard, or
  // the input can't be changed (it is `readonly`, or has no `on-input`).
  //
  // `TextInput` is characters, as the host's window gives them: typed or pasted, in UTF-8. `KeyDown` is the keys that
  // do something else (see `rt::input::Key`), with what is held (`rt::input::Mod`). Copy and cut are the host's: it
  // reads `SelectedText` and, for a cut, sends `kBackspace`; a paste is `TextInput` of what is on its clipboard.
  //
  // A host that renders only when something changes has to render again while `InputBlinking`: the caret blinks.
  auto TextInput(std::string_view utf8, bool* handled, std::string* error) -> bool;
  auto KeyDown(rt::input::Key key, int mods, bool* handled, std::string* error) -> bool;
  auto HasTextFocus() const -> bool;
  auto SelectedText() const -> std::string;
  auto InputBlinking() const -> bool;

  // Host => UI. Tooltips: a node's `tooltip` shows next to the pointer once the pointer has been over it for a while, over
  // everything else in the frame. The host says where the pointer is and what time it is, and the frames that it
  // renders do the rest.
  //
  // `PointerMoved` is where the pointer is, in the coordinates of the frames, and `PointerLeft` that it isn't over the
  // window any more. `SetTime` is the time in seconds, from whenever the host likes; it is what `delay` is measured
  // in, and a host that never calls it gets the system's clock. A click or the wheel hides the tooltip that is
  // showing, until the pointer is over something else.
  //
  // A host that renders only when something changes has to render again while `TooltipPending`: the pointer is over
  // a tooltip that is waiting for its delay to be over.
  void PointerMoved(double x, double y);
  void PointerLeft();
  void SetTime(double seconds);
  auto TooltipPending() const -> bool;
  void SetTooltipStyle(const KuraTooltipStyle& style);
  auto GetTooltipStyle() const -> KuraTooltipStyle;

  // UI => host. Says that the wheel (or a drag) moved by (dx, dy) pixels at (x, y), in the coordinates of the last
  // frame, down and to the right being positive. The topmost `#scroll` or `#list` there that has room to move
  // that way scrolls, and `*handled` is set: the host should `Render` again to see it. If that container has an
  // `on-scroll`, its Message is sent too, as `Send` does. Nothing there to scroll isn't an error: `*handled` is
  // false, and the wheel is the host's.
  auto Scroll(double x, double y, double dx, double dy, bool* handled, std::string* error) -> bool;
};
}  // namespace kura

#endif  // KURA_SESSION_H
