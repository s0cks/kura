#ifndef KURA_VIEW_H
#define KURA_VIEW_H

#include <cstddef>
#include <memory>
#include <unordered_map>

#include "function.h"

namespace kura {
using ViewId = uint64_t;

static constexpr const auto kInvalidViewId = 0;
static constexpr const auto kFirstViewId = 1;

// A view is the window a program runs in, and the state that belongs to it: the layout of what it drew, where its
// elements are on the screen, what has focus in it. The host makes one for each program it runs (a `Session` does),
// and compiled `init`, `update` and `view` are given it as a hidden first argument, like a `this`: Kura code can't
// name it and doesn't take it as a parameter, but the runtime calls it makes are for the view it was given.
//
// The runtime's parts keep what they need for a view in it, each as a `State` of its own type, and find it with
// `View::Current()`: the view the running code was given, or else the view the host has made the base of the thread,
// which is what the host's own calls (an event from the window) are about.
class View {
 public:
  // What a part of the runtime keeps for a view. Each part has a type of its own, and `GetState<T>()` makes it when
  // the view first needs it.
  class ViewState {
   public:
    virtual ~ViewState() = default;
  };

 private:
  ViewId id_;
  Function* init_ = nullptr;
  std::unordered_map<const void*, std::unique_ptr<ViewState>> states_{};

  explicit View(const ViewId id) :
    id_(id) {}

  inline void SetInit(Function* rhs) {
    init_ = rhs;
  }

 public:
  ~View() = default;
  View(const View&) = delete;
  View& operator=(const View&) = delete;

  auto GetViewId() const -> ViewId {
    return id_;
  }

  auto GetInit() const -> Function* {
    return init_;
  }

  auto HasInit() const -> bool {
    return GetInit() != nullptr;
  }

  template <typename T>
  auto GetState() -> T& {
    // one key for each T, made by the first call for it
    static const char kKey = 0;
    auto& slot = states_[&kKey];
    if (!slot)
      slot = std::make_unique<T>();
    return static_cast<T&>(*slot);
  }

  auto Equals(const View& rhs) const -> bool;
  auto ToString() const -> std::string;

 public:
  static auto Get(const ViewId rhs) -> View*;
  static auto New() -> View*;
  // The view is gone, and what the runtime kept for it with it. It stops being the base of its thread.
  static void Destroy(View* view);

  // The view the running code was given, else the base of this thread, else a view of the thread's own, which is what
  // a program that isn't run by a host (a test, say) has.
  static auto Current() -> View&;
  // What the host's own calls into the runtime are about: the view of the program it is driving.
  static void SetBase(View* view);
  static auto GetBase() -> View*;

  // Compiled code that was given a view runs inside it: `Enter(view)` makes it current until the matching `Leave()`.
  // Code that wasn't given one (a test calling `update`) enters with null, and stays in the view it was called from.
  static void Enter(View* view);
  static void Leave();
  // How many are entered, and leaving the innermost until `depth` are: a panic jumps out of the functions it was in.
  static auto Depth() -> size_t;
  static void Unwind(size_t depth);
};
}  // namespace kura

#endif  // KURA_VIEW_H
