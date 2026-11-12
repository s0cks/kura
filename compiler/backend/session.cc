#include "backend/session.h"

#include <algorithm>
#include <format>
#include <print>

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

    std::println("init state => {}", kura::rt::Format(session->state_));
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
  queue_.push_back(message);
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
  // the places of the last frame mean nothing to this one
  kura_rt_events_clear();
  std::vector<rt::KuraValue> args{};
  if (program_->GetArity("view") > 0)
    args.push_back(state_);
  rt::KuraValue ignored{};
  return program_->Call("view", args, &ignored, error);
}

auto Session::Click(const double x, const double y, bool* handled, std::string* error) -> bool {
  rt::KuraValue message{};
  const auto hit = kura_rt_event_hit(rt::kEventClick, x, y, &message) != 0;
  if (handled != nullptr)
    *handled = hit;
  return !hit || Send(message, error);
}
}  // namespace kura
