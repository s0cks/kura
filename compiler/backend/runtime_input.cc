// Inputs: see runtime_input.h.
#include "backend/runtime_input.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <vector>

#include "backend/text_edit.h"
#include "view.h"

namespace kura::rt::input {
namespace {
// How long the caret is shown, and then hidden, in seconds.
constexpr double kBlinkPeriod = 1.0;
// What a selection is painted with, over the text it is behind (0xRRGGBBAA).
constexpr KuraColor kSelectionColor{
    .r = 74.0f / 255.0f,
    .g = 144.0f / 255.0f,
    .b = 226.0f / 255.0f,
    .a = 102.0f / 255.0f,
};

struct Id {
  const void* owner = nullptr;
  uint64_t node = 0;

  auto operator==(const Id& rhs) const -> bool {
    return owner == rhs.owner && node == rhs.node;
  }
};

struct KeyHash {
  auto operator()(const Id& key) const -> size_t {
    return std::hash<const void*>{}(key.owner) * 31 + std::hash<uint64_t>{}(key.node);
  }
};

struct State {
  TextEdit edit{};
  // how far the text is moved left to keep the caret in view, when it is wider than the input
  float scroll = 0;
  // the caret is shown for the first half of a period from here, so that it is while it moves
  double blink_from = 0;
  bool seen = false;
};

// An input drawn in the frame: what a click or a key on it needs.
struct Place {
  Id key{};
  // where the text starts, with the scroll in it
  float origin = 0;
  float size = 16;
  std::string family{};
  bool editable = false;
  KuraValue message{kTagNone, 0};
};

// What the inputs of one view need: their text, and which has focus. It is kept in the view.
struct Registry : kura::View::ViewState {
  std::unordered_map<Id, State, KeyHash> states{};
  std::vector<Place> places{};
  std::optional<Id> focus{};
  bool clock_set = false;
  double clock = 0;
};

inline auto Reg() -> Registry& {
  return kura::View::Current().GetState<Registry>();
}

auto Now() -> double {
  if (Reg().clock_set)
    return Reg().clock;
  using Clock = std::chrono::steady_clock;
  return std::chrono::duration<double>(Clock::now().time_since_epoch()).count();
}

auto FamilyData(const std::string& family) -> const char* {
  return family.empty() ? nullptr : family.data();
}

auto WidthOf(const std::string& text, const float size, const std::string& family) -> float {
  if (text.empty())
    return 0;
  float width = 0;
  float height = 0;
  if (kura_rt_measure_text(text.data(), text.size(), size, FamilyData(family), family.size(), &width, &height) != 0)
    return static_cast<float>(text.size()) * size * 0.5f;
  return width;
}

auto LineHeightOf(const float size, const std::string& family) -> float {
  float width = 0;
  float height = 0;
  if (kura_rt_measure_text("Mg", 2, size, FamilyData(family), family.size(), &width, &height) != 0)
    return size * 1.2f;
  return height;
}

// How far in from the start of the text a byte offset is.
auto OffsetX(const std::string& text, const size_t offset, const float size, const std::string& family) -> float {
  return WidthOf(text.substr(0, std::min(offset, text.size())), size, family);
}

auto IsBoundary(const std::string& text, const size_t offset) -> bool {
  return offset >= text.size() || (static_cast<unsigned char>(text[offset]) & 0xC0) != 0x80;
}

// The offset between two letters that is nearest to `x`, measured from the start of the text.
auto OffsetAt(const std::string& text, const float x, const float size, const std::string& family) -> size_t {
  if (x <= 0 || text.empty())
    return 0;
  std::vector<size_t> boundaries{};
  for (size_t i = 0; i <= text.size(); i++) {
    if (IsBoundary(text, i))
      boundaries.push_back(i);
  }

  // the first boundary at or after x: widths only grow along the text
  size_t lo = 0;
  size_t hi = boundaries.size() - 1;
  while (lo < hi) {
    const auto mid = (lo + hi) / 2;
    if (OffsetX(text, boundaries[mid], size, family) < x)
      lo = mid + 1;
    else
      hi = mid;
  }
  if (lo == 0)
    return boundaries[0];
  const auto after = OffsetX(text, boundaries[lo], size, family);
  const auto before = OffsetX(text, boundaries[lo - 1], size, family);
  return x - before < after - x ? boundaries[lo - 1] : boundaries[lo];
}

auto FocusedPlace() -> Place* {
  if (!Reg().focus)
    return nullptr;
  for (auto& place : Reg().places) {
    if (place.key == *Reg().focus)
      return &place;
  }
  return nullptr;
}

auto FocusedState() -> State* {
  if (!Reg().focus)
    return nullptr;
  const auto it = Reg().states.find(*Reg().focus);
  return it == Reg().states.end() ? nullptr : &it->second;
}

// What the text became is a Message: the variant `on-input` names, with the text in it.
void MakeMessage(const Place& place, const State& state, KuraValue* message) {
  if (message == nullptr)
    return;
  // made in the model space, with what update will take: nothing in a frame
  const PhaseScope phase(Phase::kModel);
  *message = MakeVariantWithTag(place.message.tag, MakeString(state.edit.GetText()));
}

void Touch(State* state) {
  state->blink_from = Now();
}

auto Faded(KuraColor color, const double factor) -> KuraColor {
  color.a *= static_cast<float>(factor);
  return color;
}
}  // namespace

void Draw(const Field& field) {
  const Id key{field.owner, field.node};
  auto& state = Reg().states[key];
  state.seen = true;
  // the view is what the text is: what was typed since is gone if the program didn't take it
  if (state.edit.GetText() != field.text)
    state.edit.SetText(field.text);

  const auto focused = Reg().focus && *Reg().focus == key;
  const auto editable = !field.readonly && field.message.tag != kTagNone;
  const auto& text = state.edit.GetText();
  const auto line = LineHeightOf(field.size, field.family);
  const auto room = std::max(0.0f, field.box.width - field.padding_left - field.padding_right);
  const auto top = field.box.y + (field.box.height - line) / 2;
  const auto text_width = WidthOf(text, field.size, field.family);

  auto origin = field.box.x + field.padding_left;
  if (text_width <= room) {
    state.scroll = 0;
    const auto slack = room - text_width;
    origin += field.align == 1 ? slack / 2 : field.align == 2 ? slack : 0.0f;
  } else {
    // wider than the input: it scrolls to keep the caret in it
    if (focused) {
      const auto caret = OffsetX(text, state.edit.GetCaret(), field.size, field.family);
      if (caret - state.scroll < 0)
        state.scroll = caret;
      else if (caret - state.scroll > room - 1)
        state.scroll = caret - room + 1;
    }
    state.scroll = std::clamp(state.scroll, 0.0f, text_width - room + 1);
    origin -= state.scroll;
  }

  // where a click on it goes: the clips it is drawn in apply
  Reg().places.push_back(Place{key, origin, field.size, field.family, editable, field.message});
  kura_rt_event_add(field.box.x, field.box.y, field.box.width, field.box.height, kEventInput, kTagInt,
                    Reg().places.size() - 1);

  // what is in the input stays in it
  const KuraRect clip{field.box.x + field.padding_left, field.box.y, room, field.box.height};
  kura_rt_push_clip(&clip);

  if (focused && state.edit.HasSelection()) {
    const auto from = origin + OffsetX(text, state.edit.SelectionBegin(), field.size, field.family);
    const auto to = origin + OffsetX(text, state.edit.SelectionEnd(), field.size, field.family);
    const KuraRect selection{from, top, to - from, line};
    kura_rt_fill_rect(&selection, &kSelectionColor);
  }

  if (!text.empty()) {
    kura_rt_render_text(origin, top, text.data(), text.size(), field.size, FamilyData(field.family),
                        field.family.size(), &field.color);
  } else if (!field.placeholder.empty()) {
    // what is there to say, in a lighter color, from where the text would start
    const auto placeholder_color = Faded(field.color, 0.4);
    kura_rt_render_text(field.box.x + field.padding_left, top, field.placeholder.data(), field.placeholder.size(),
                        field.size, FamilyData(field.family), field.family.size(), &placeholder_color);
  }

  if (focused && editable) {
    const auto phase = std::fmod(std::max(0.0, Now() - state.blink_from), kBlinkPeriod);
    if (phase < kBlinkPeriod / 2) {
      const auto at = origin + OffsetX(text, state.edit.GetCaret(), field.size, field.family);
      const KuraRect caret{at, top, 1.0f + field.size / 24.0f, line};
      kura_rt_fill_rect(&caret, &field.color);
    }
  }

  kura_rt_pop_clip();
}

void BeginFrame() {
  for (auto& [key, state] : Reg().states)
    state.seen = false;
}

void EndFrame() {
  // an input that wasn't drawn is gone, and the keyboard with it
  for (auto it = Reg().states.begin(); it != Reg().states.end();) {
    if (it->second.seen)
      ++it;
    else
      it = Reg().states.erase(it);
  }
  if (Reg().focus && Reg().states.find(*Reg().focus) == Reg().states.end())
    Reg().focus.reset();
}

void ClearPlaces() {
  Reg().places.clear();
}

void Reset() {
  Reg().states.clear();
  Reg().places.clear();
  Reg().focus.reset();
}

auto Click(const double x, const double y) -> bool {
  KuraRect box{};
  KuraValue found{};
  if (!kura_rt_event_topmost(kEventInput, x, y, &box, &found) || found.bits >= Reg().places.size()) {
    Reg().focus.reset();
    return false;
  }

  const auto& place = Reg().places[found.bits];
  const auto it = Reg().states.find(place.key);
  if (it == Reg().states.end()) {
    Reg().focus.reset();
    return false;
  }

  auto& state = it->second;
  Reg().focus = place.key;
  const auto& text = state.edit.GetText();
  state.edit.SetCaret(OffsetAt(text, static_cast<float>(x) - place.origin, place.size, place.family));
  Touch(&state);
  return true;
}

auto Text(const std::string_view utf8, KuraValue* message) -> int {
  const auto place = FocusedPlace();
  const auto state = FocusedState();
  if (place == nullptr || state == nullptr || !place->editable)
    return 0;

  const auto clean = TextEdit::Sanitize(utf8);
  if (clean.empty() || !state->edit.Insert(clean))
    return 0;
  Touch(state);
  MakeMessage(*place, *state, message);
  return kHandled | kMessage;
}

auto Press(const Key key, const int mods, KuraValue* message) -> int {
  const auto place = FocusedPlace();
  const auto state = FocusedState();
  if (place == nullptr || state == nullptr)
    return 0;

  const auto extend = (mods & kShift) != 0;
  const auto word = (mods & kControl) != 0;
  auto& edit = state->edit;
  switch (key) {
    case kLeft:
      edit.Left(extend, word);
      break;
    case kRight:
      edit.Right(extend, word);
      break;
    case kHome:
      edit.Home(extend);
      break;
    case kEnd:
      edit.End(extend);
      break;
    case kSelectAll:
      edit.SelectAll();
      break;
    case kEscape:
      Reg().focus.reset();
      return kHandled;
    case kBackspace:
    case kDelete: {
      if (!place->editable)
        return 0;
      if (!(key == kBackspace ? edit.Backspace(word) : edit.Delete(word)))
        return 0;
      Touch(state);
      MakeMessage(*place, *state, message);
      return kHandled | kMessage;
    }
    default:
      return 0;
  }
  Touch(state);
  return kHandled;
}

auto HasFocus() -> bool {
  return FocusedState() != nullptr;
}

auto SelectedText() -> std::string {
  const auto state = FocusedState();
  return state == nullptr ? std::string{} : state->edit.SelectedText();
}

auto Blinking() -> bool {
  const auto place = FocusedPlace();
  return place != nullptr && place->editable;
}

void SetClock(const double seconds) {
  Reg().clock_set = true;
  Reg().clock = seconds;
}
}  // namespace kura::rt::input
