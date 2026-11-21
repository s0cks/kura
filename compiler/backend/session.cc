#include "backend/session.h"

#include <algorithm>
#include <format>
#include <iostream>
#include <print>

#include "heap/heap.h"

namespace kura {
auto Session::Create(Program* program, std::string* error) -> std::unique_ptr<Session> {
  if (program == nullptr) {
    *error = "there is no program";
    return nullptr;
  }

  std::unique_ptr<Session> session(new Session(program));
  session->has_view_ = program->HasFunction("view");
  session->has_update_ = program->HasFunction("update");
  if (program->HasFunction("init")) {
    if (program->GetArity("init") != 0) {
      *error = "`init` takes no arguments, but it takes " + std::to_string(program->GetArity("init"));
      return nullptr;
    }

    if (!program->Call("init", {}, &session->state_, error))
      return nullptr;
  }

  if (session->has_update_ && program->GetArity("update") != 2) {
    *error = "`update` takes a Message and the state, but it takes " + std::to_string(program->GetArity("update")) +
             " argument(s)";
    return nullptr;
  }
  return session;
}

auto Session::AddListener(Listener listener) -> ListenerId {
  const auto id = next_listener_++;
  listeners_.emplace_back(id, std::move(listener));
  return id;
}

auto Session::RemoveListener(const ListenerId id) -> bool {
  const auto it = std::find_if(listeners_.begin(), listeners_.end(), [id](const auto& entry) {
    return entry.first == id;
  });
  if (it == listeners_.end())
    return false;
  listeners_.erase(it);
  return true;
}

auto Session::Send(const rt::KuraValue& message, std::string* error) -> bool {
  // A Message from a click was made by `view`, in the frame's arena, and `update` may keep what is in it: it is
  // moved out of the frame before anything else sees it.
  const rt::PhaseScope model(rt::Phase::kModel);
  queue_.push_back(rt::MoveOutOfFrame(message));
  if (delivering_)
    return true;

  delivering_ = true;
  while (!queue_.empty()) {
    const auto next = queue_.front();
    queue_.pop_front();
    if (!Deliver(next, error)) {
      // what was queued behind it was sent in the belief that the one before it had been delivered
      queue_.clear();
      delivering_ = false;
      return false;
    }
  }
  delivering_ = false;
  return true;
}

auto Session::Deliver(const rt::KuraValue& message, std::string* error) -> bool {
  const auto previous = state_;
  // A built-in message (see rt::IsBuiltinMessage) is sent to every UI, whether or not it asked for it, so one
  // that isn't in the program's Message type isn't an error: `update` can't take it, and never sees it. The
  // listeners still do, since the host is the one that knows what it sent.
  const auto wanted = !rt::IsBuiltinMessage(message) || program_->Accepts("update", 0, message);
  if (has_update_ && wanted) {
    rt::KuraValue next{};
    if (!program_->Call("update", {message, state_}, &next, error))
      return false;
    state_ = next;
  }

  // Listeners come after update. They are called from a copy of the list, so one that adds or removes
  // listeners doesn't disturb the walk; a listener removed before its turn isn't called.
  const auto snapshot = listeners_;
  for (const auto& [id, listener] : snapshot) {
    const auto live = std::any_of(listeners_.begin(), listeners_.end(), [id = id](const auto& entry) {
      return entry.first == id;
    });
    if (live)
      listener(Event{message, previous, state_});
  }
  return true;
}

auto Session::Resized(const double width, const double height, std::string* error) -> bool {
  return Send(rt::MakeResized(width, height), error);
}

auto Session::Focused(const bool focused, std::string* error) -> bool {
  return Send(rt::MakeFocused(focused), error);
}

auto Session::Render(std::string* error) -> bool {
  if (!has_view_) {
    *error = "there is no function `view`";
    return false;
  }
  // the places of the last frame mean nothing to this one, and neither does what it made: the frame's arena is reset
  kura_rt_events_clear();
  if (const auto heap = heap::Heap::Current())
    heap->ResetFrame();

  std::vector<rt::KuraValue> args{};
  if (program_->GetArity("view") > 0)
    args.push_back(state_);
  rt::KuraValue ignored{};
  // everything `view` allocates is for this frame
  const rt::PhaseScope frame(rt::Phase::kFrame);
  const auto ok = program_->Call("view", args, &ignored, error);
#ifndef NDEBUG
  // the model and the tenured space never point into the frame: a bug in what decides where things live shows up here
  if (const auto heap = heap::Heap::Current(); heap && !heap->Verify(&std::cerr)) {
    *error = "the heap is inconsistent after the frame (see above)";
    return false;
  }
#endif
  return ok;
}

void Session::PointerMoved(const double x, const double y) {
  kura_rt_pointer_moved(x, y);
}

void Session::PointerLeft() {
  kura_rt_pointer_left();
}

void Session::SetTime(const double seconds) {
  kura_rt_set_time(seconds);
}

auto Session::TooltipPending() const -> bool {
  return kura_rt_tooltip_pending() != 0;
}

void Session::SetTooltipStyle(const KuraTooltipStyle& style) {
  kura_rt_tooltip_configure(&style);
}

auto Session::GetTooltipStyle() const -> KuraTooltipStyle {
  return kura_rt_tooltip_style();
}

auto Session::Click(const double x, const double y, bool* handled, std::string* error) -> bool {
  kura_rt_tooltip_dismiss();
  // an input under the click has the keyboard, and one that isn't has lost it
  const auto focused = rt::input::Click(x, y);
  rt::KuraValue message{};
  const auto hit = kura_rt_event_hit(rt::kEventClick, x, y, &message) != 0;
  if (handled != nullptr)
    *handled = hit || focused;
  return !hit || Send(message, error);
}

auto Session::TextInput(const std::string_view utf8, bool* handled, std::string* error) -> bool {
  rt::KuraValue message{};
  const auto result = rt::input::Text(utf8, &message);
  if (handled != nullptr)
    *handled = (result & rt::input::kHandled) != 0;
  return (result & rt::input::kMessage) == 0 || Send(message, error);
}

auto Session::KeyDown(const rt::input::Key key, const int mods, bool* handled, std::string* error) -> bool {
  rt::KuraValue message{};
  const auto result = rt::input::Press(key, mods, &message);
  if (handled != nullptr)
    *handled = (result & rt::input::kHandled) != 0;
  return (result & rt::input::kMessage) == 0 || Send(message, error);
}

auto Session::HasTextFocus() const -> bool {
  return rt::input::HasFocus();
}

auto Session::SelectedText() const -> std::string {
  return rt::input::SelectedText();
}

auto Session::InputBlinking() const -> bool {
  return rt::input::Blinking();
}

auto Session::Scroll(const double x, const double y, const double dx, const double dy, bool* handled,
                     std::string* error) -> bool {
  rt::KuraValue message{};
  const auto result = kura_rt_event_scroll(x, y, dx, dy, &message);
  if ((result & 1) != 0)
    kura_rt_tooltip_dismiss();
  if (handled != nullptr)
    *handled = (result & 1) != 0;
  return (result & 2) == 0 || Send(message, error);
}
}  // namespace kura
