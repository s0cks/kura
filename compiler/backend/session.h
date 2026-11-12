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
};
}  // namespace kura

#endif  // KURA_SESSION_H
