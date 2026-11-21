#include "backend/runtime_abi.h"

#include <bit>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "binary_op.h"
#include "common.h"
#include "heap/heap.h"
#include "heap/objects.h"
#include "object.h"
#include "unary_op.h"

namespace kura::rt {
namespace {
// The objects of a running program are the heap's (heap/objects.h): flat blocks with their elements inline, in the
// space of the phase that allocates them. A frame's are in the frame's arena, and what `init` and `update` make is in
// the model space (see Phase in runtime_abi.h).
using heap::BoxObj;
using heap::ClosureObj;
using heap::HeapObject;
using heap::NodeObj;
using heap::ObjKind;
using heap::RecordObj;
using heap::SeqObj;
using heap::StringObj;

thread_local Phase tl_phase = Phase::kModel;

auto TheHeap() -> heap::Heap& {
  const auto current = heap::Heap::Current();
  if (current == nullptr)
    kura_rt_panic("there is no heap: the host sets one (heap::Heap::SetCurrent) before it runs a program");
  return *current;
}

// Where what is allocated now lives: the frame while a view runs, the tenured space while the program loads, and the
// model otherwise.
auto AllocationSpace() -> heap::Space {
  switch (tl_phase) {
    case Phase::kFrame:
      return heap::Space::kFrame;
    case Phase::kConstant:
      return heap::Space::kTenured;
    default:
      return heap::Space::kModel;
  }
}

inline auto ToHeap(const KuraValue& value) -> heap::Value {
  return {value.tag, value.bits};
}

inline auto FromHeap(const heap::Value& value) -> KuraValue {
  return {value.tag, value.bits};
}

inline auto KindOf(void* object) -> ObjKind {
  return static_cast<HeapObject*>(object)->kind();
}

auto NewStr(const std::string_view text) -> StringObj* {
  return heap::NewString(TheHeap(), AllocationSpace(), text);
}

auto NewSeqObj(const uint64_t capacity) -> SeqObj* {
  return heap::NewSeq(TheHeap(), AllocationSpace(), capacity);
}

auto NewRecordObj(const uint64_t capacity) -> RecordObj* {
  return heap::NewRecord(TheHeap(), AllocationSpace(), static_cast<uint32_t>(capacity));
}

auto NewBoxObj(const KuraValue& value) -> BoxObj* {
  return heap::NewBox(TheHeap(), AllocationSpace(), ToHeap(value));
}

// A node is the one object that grows after it is made: it is filled in a child and a property at a time, so when its
// list or record is full it gets a bigger one (it is the node's own, so nothing else can see the old one).
auto NodeChildren(NodeObj* node, const uint64_t extra) -> SeqObj* {
  const auto old = node->children;
  const auto length = old ? old->length : 0;
  if (old && length + extra <= old->capacity)
    return old;

  const auto fresh = NewSeqObj(std::max<uint64_t>(4, (length + extra) * 2));
  for (uint64_t idx = 0; idx < length; idx++)
    fresh->Push(old->items()[idx]);
  node->children = fresh;
  return fresh;
}

auto NodeProps(NodeObj* node) -> RecordObj* {
  if (node->props == nullptr)
    node->props = NewRecordObj(4);
  return node->props;
}

// Sets a field of a node's record, growing it if it is full.
void NodeSet(NodeObj* node, const uint64_t hash, const char* name, const KuraValue value) {
  const auto props = NodeProps(node);
  if (props->Set(hash, name, ToHeap(value)))
    return;

  const auto fresh = NewRecordObj(static_cast<uint64_t>(props->capacity) * 2);
  for (uint32_t idx = 0; idx < props->count; idx++)
    fresh->Set(props->fields()[idx].hash, props->fields()[idx].name, props->fields()[idx].value);
  fresh->Set(hash, name, ToHeap(value));
  node->props = fresh;
}

auto VariantNames() -> std::unordered_map<uint64_t, std::string>& {
  static std::unordered_map<uint64_t, std::string> names{};
  return names;
}

auto TagName(const uint64_t tag) -> std::string {
  switch (tag) {
    case kTagNone:
      return "None";
    case kTagBool:
      return "Bool";
    case kTagInt:
      return "Int";
    case kTagFloat:
      return "Float";
    case kTagString:
      return "String";
    case kTagColor:
      return "Color";
    case kTagPx:
      return "Px";
    case kTagPct:
      return "Pct";
    case kTagFr:
      return "Fr";
    case kTagNode:
      return "Node";
    case kTagSeq:
      return "Seq";
    case kTagRecord:
      return "Record";
    case kTagFunction:
      return "Function";
    case kTagHandle:
      return "Handle";
    default:
      break;
  }

  if (IsVariantTag(tag)) {
    const auto it = VariantNames().find(tag);
    return it != VariantNames().end() ? it->second : "variant";
  }

  return "unknown";
}

inline auto AsDouble(const uint64_t bits) -> double {
  double value;
  static_assert(sizeof(value) == sizeof(bits));
  __builtin_memcpy(&value, &bits, sizeof(value));
  return value;
}

inline auto FromDouble(const double value) -> uint64_t {
  uint64_t bits;
  __builtin_memcpy(&bits, &value, sizeof(bits));
  return bits;
}

inline auto Str(const uint64_t bits) -> StringObj* {
  return reinterpret_cast<StringObj*>(bits);
}

inline auto IsUnit(const uint64_t tag) -> bool {
  return tag == kTagPx || tag == kTagPct || tag == kTagFr;
}

inline auto IsScalar(const uint64_t tag) -> bool {
  return tag == kTagInt || tag == kTagFloat;
}

auto Number(const double value) -> std::string {
  char buffer[64];
  const auto result = std::to_chars(buffer, buffer + sizeof(buffer), value);
  return std::string(buffer, result.ptr);
}

auto Quote(const std::string& value) -> std::string {
  std::string out = "\"";
  for (const char c : value) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += c;
    }
  }

  return out + "\"";
}

auto Equal(const KuraValue& a, const KuraValue& b) -> bool;

auto SeqEqual(const SeqObj* a, const SeqObj* b) -> bool {
  const auto x = a ? a->length : 0;
  const auto y = b ? b->length : 0;
  if (x != y)
    return false;

  for (uint64_t idx = 0; idx < x; idx++) {
    if (!Equal(FromHeap(a->items()[idx]), FromHeap(b->items()[idx])))
      return false;
  }

  return true;
}

auto RecordEqual(const RecordObj* a, const RecordObj* b) -> bool {
  const auto x = a ? a->count : 0;
  const auto y = b ? b->count : 0;
  if (x != y)
    return false;

  for (uint32_t idx = 0; idx < x; idx++) {
    const auto& field = a->fields()[idx];
    const auto other = b->Find(field.hash);
    if (!other || !Equal(FromHeap(field.value), FromHeap(other->value)))
      return false;
  }

  return true;
}

auto Equal(const KuraValue& a, const KuraValue& b) -> bool {
  if (IsScalar(a.tag) && IsScalar(b.tag) && a.tag != b.tag) {
    const auto x = a.tag == kTagInt ? static_cast<double>(static_cast<int64_t>(a.bits)) : AsDouble(a.bits);
    const auto y = b.tag == kTagInt ? static_cast<double>(static_cast<int64_t>(b.bits)) : AsDouble(b.bits);
    return x == y;
  }

  if (a.tag != b.tag)
    return false;

  switch (a.tag) {
    case kTagNone:
      return true;
    case kTagBool:
    case kTagInt:
      return a.bits == b.bits;
    case kTagFloat:
    case kTagPx:
    case kTagPct:
    case kTagFr:
      return AsDouble(a.bits) == AsDouble(b.bits);
    case kTagString:
    case kTagColor:
      return Str(a.bits)->view() == Str(b.bits)->view();
    case kTagSeq:
      return SeqEqual(reinterpret_cast<SeqObj*>(a.bits), reinterpret_cast<SeqObj*>(b.bits));
    case kTagRecord:
      return RecordEqual(reinterpret_cast<RecordObj*>(a.bits), reinterpret_cast<RecordObj*>(b.bits));
    case kTagNode: {
      const auto x = reinterpret_cast<NodeObj*>(a.bits);
      const auto y = reinterpret_cast<NodeObj*>(b.bits);
      return std::string_view(x->kind_name) == std::string_view(y->kind_name) && RecordEqual(x->props, y->props) &&
             SeqEqual(x->children, y->children);
    }
    case kTagFunction:
    case kTagHandle:
      return a.bits == b.bits;
    default:
      break;
  }

  if (IsVariantTag(a.tag)) {
    if (a.bits == 0 || b.bits == 0)
      return a.bits == b.bits;

    return Equal(FromHeap(reinterpret_cast<BoxObj*>(a.bits)->value), FromHeap(reinterpret_cast<BoxObj*>(b.bits)->value));
  }

  return false;
}

auto Properties(const RecordObj* props) -> std::string {
  std::stringstream ss{};
  const auto count = props ? props->count : 0;
  for (uint32_t idx = 0; idx < count; idx++)
    ss << (idx ? ", " : "") << props->fields()[idx].name << ": " << Format(FromHeap(props->fields()[idx].value));

  return ss.str();
}
}  // namespace

auto Format(const KuraValue& value) -> std::string {
  switch (value.tag) {
    case kTagNone:
      return "None";
    case kTagBool:
      return value.bits ? "true" : "false";
    case kTagInt:
      return std::to_string(static_cast<int64_t>(value.bits));
    case kTagFloat: {
      auto text = Number(AsDouble(value.bits));
      if (text.find_first_of(".einf") == std::string::npos)
        text += ".0";

      return text;
    }
    case kTagPx:
      return Number(AsDouble(value.bits)) + "px";
    case kTagPct:
      return Number(AsDouble(value.bits)) + "%";
    case kTagFr:
      return Number(AsDouble(value.bits)) + "fr";
    case kTagString:
      return Quote(std::string(Str(value.bits)->view()));
    case kTagColor:
      return std::string(Str(value.bits)->view());
    case kTagSeq: {
      const auto seq = reinterpret_cast<SeqObj*>(value.bits);
      std::stringstream ss{};
      ss << "[";
      for (uint64_t idx = 0; idx < seq->length; idx++)
        ss << (idx ? ", " : "") << Format(FromHeap(seq->items()[idx]));
      ss << "]";
      return ss.str();
    }
    case kTagRecord: {
      const auto record = reinterpret_cast<RecordObj*>(value.bits);
      return record->count == 0 ? "{}" : "{ " + Properties(record) + " }";
    }
    case kTagNode: {
      const auto node = reinterpret_cast<NodeObj*>(value.bits);
      std::stringstream ss{};
      ss << "#" << node->kind_name;
      if (node->props && node->props->count != 0)
        ss << "(" << Properties(node->props) << ")";

      if (node->children && node->children->length != 0) {
        ss << " {";
        for (uint64_t idx = 0; idx < node->children->length; idx++)
          ss << " " << Format(FromHeap(node->children->items()[idx]));

        ss << " }";
      }

      return ss.str();
    }
    case kTagFunction:
      return "<function>";
    case kTagHandle: {
      std::stringstream ss{};
      ss << "<handle 0x" << std::hex << value.bits << ">";
      return ss.str();
    }
    default:
      break;
  }

  if (IsVariantTag(value.tag)) {
    auto text = TagName(value.tag);
    if (value.bits)
      text += "(" + Format(FromHeap(reinterpret_cast<BoxObj*>(value.bits)->value)) + ")";

    return text;
  }

  return "<unknown>";
}

void SetPhase(const Phase phase) {
  tl_phase = phase;
}

auto GetPhase() -> Phase {
  return tl_phase;
}

auto StringView(const uint64_t bits) -> std::string_view {
  return bits == 0 ? std::string_view{} : Str(bits)->view();
}

auto MakeString(const std::string_view value) -> KuraValue {
  return {kTagString, reinterpret_cast<uint64_t>(NewStr(value))};
}

auto MakeInt(const int64_t value) -> KuraValue {
  return {kTagInt, static_cast<uint64_t>(value)};
}

auto MakeFloat(const double value) -> KuraValue {
  return {kTagFloat, std::bit_cast<uint64_t>(value)};
}

auto MakeBool(const bool value) -> KuraValue {
  return {kTagBool, value ? 1ULL : 0ULL};
}

auto MakeSize(const double width, const double height) -> KuraValue {
  const auto record = kura_rt_record_new(2);
  const auto w = MakeFloat(width);
  const auto h = MakeFloat(height);
  kura_rt_record_set(record, HashName("width"), "width", w.tag, w.bits);
  kura_rt_record_set(record, HashName("height"), "height", h.tag, h.bits);
  return {kTagRecord, reinterpret_cast<uint64_t>(record)};
}

auto MakeResized(const double width, const double height) -> KuraValue {
  const auto size = MakeSize(width, height);
  return MakeVariant("Resized", &size);
}

auto MakeFocused(const bool focused) -> KuraValue {
  const auto value = MakeBool(focused);
  return MakeVariant("Focused", &value);
}

auto IsBuiltinMessage(const KuraValue& message) -> bool {
  return message.tag == VariantTag("Resized") || message.tag == VariantTag("Focused");
}

namespace {
// Copies what is in the frame's arena into the model space, once each; everything else is shared.
class FrameMover {
 private:
  heap::Heap& heap_;
  std::unordered_map<uword, uword> moved_{};

  auto InFrame(const uint64_t address) const -> bool {
    return address != 0 && heap_.SpaceOf(address) == heap::Space::kFrame;
  }

 public:
  explicit FrameMover(heap::Heap& heap) :
    heap_(heap) {}

  auto Move(const heap::Value value) -> heap::Value {
    if (!heap::TagMayHoldHeapPointer(value.tag) || !InFrame(value.bits))
      return value;
    return {value.tag, MoveObject(reinterpret_cast<HeapObject*>(value.bits))};
  }

  auto MoveObject(HeapObject* from) -> uword {
    if (from == nullptr || !InFrame(from->address()))
      return from ? from->address() : 0;

    if (const auto it = moved_.find(from->address()); it != moved_.end())
      return it->second;

    const auto space = heap::Space::kModel;
    uword to = 0;
    switch (from->kind()) {
      case ObjKind::kString:
        to = reinterpret_cast<uword>(heap::NewString(heap_, space, from->As<StringObj>()->view()));
        break;
      case ObjKind::kSeq: {
        const auto seq = from->As<SeqObj>();
        const auto copy = heap::NewSeq(heap_, space, seq->length);
        moved_[from->address()] = reinterpret_cast<uword>(copy);
        for (uint64_t idx = 0; idx < seq->length; idx++)
          copy->Push(Move(seq->items()[idx]));
        return reinterpret_cast<uword>(copy);
      }
      case ObjKind::kRecord: {
        const auto record = from->As<RecordObj>();
        const auto copy = heap::NewRecord(heap_, space, record->count);
        moved_[from->address()] = reinterpret_cast<uword>(copy);
        for (uint32_t idx = 0; idx < record->count; idx++) {
          const auto& field = record->fields()[idx];
          copy->Set(field.hash, field.name, Move(field.value));
        }
        return reinterpret_cast<uword>(copy);
      }
      case ObjKind::kClosure: {
        const auto closure = from->As<ClosureObj>();
        const auto copy = heap::NewClosure(heap_, space, closure->function, closure->arity, closure->count);
        moved_[from->address()] = reinterpret_cast<uword>(copy);
        for (uint64_t idx = 0; idx < closure->count; idx++)
          copy->captures()[idx] = Move(closure->captures()[idx]);
        return reinterpret_cast<uword>(copy);
      }
      case ObjKind::kBox: {
        const auto copy = heap::NewBox(heap_, space, {});
        moved_[from->address()] = reinterpret_cast<uword>(copy);
        copy->value = Move(from->As<BoxObj>()->value);
        return reinterpret_cast<uword>(copy);
      }
      case ObjKind::kNode: {
        const auto node = from->As<NodeObj>();
        const auto copy = heap::NewNode(heap_, space, node->kind_name);
        moved_[from->address()] = reinterpret_cast<uword>(copy);
        copy->props = reinterpret_cast<RecordObj*>(MoveObject(node->props));
        copy->children = reinterpret_cast<SeqObj*>(MoveObject(node->children));
        return reinterpret_cast<uword>(copy);
      }
      default:
        // not an object a program makes: left where it is
        return from->address();
    }
    moved_[from->address()] = to;
    return to;
  }
};
}  // namespace

auto MoveOutOfFrame(const KuraValue& value) -> KuraValue {
  FrameMover mover(TheHeap());
  return FromHeap(mover.Move(ToHeap(value)));
}

auto MakeVariantWithTag(const uint64_t tag, const KuraValue& payload) -> KuraValue {
  return {tag, reinterpret_cast<uint64_t>(NewBoxObj(payload))};
}

auto MakeVariant(const std::string_view name, const KuraValue* payload) -> KuraValue {
  const auto tag = VariantTag(name);
  // so that Format can name it, the way the compiled module registers the variants it uses
  VariantNames()[tag] = std::string(name);
  return {tag, payload == nullptr ? 0ULL : reinterpret_cast<uint64_t>(NewBoxObj(*payload))};
}
}  // namespace kura::rt

using namespace kura::rt;

extern "C" {
void kura_rt_panic(const char* message) {
  std::fflush(stdout);
  std::fprintf(stderr, "kura: panic: %s\n", message);
  std::fflush(stderr);
  std::_Exit(101);
}

[[noreturn]] static inline void Panic(const std::string& message) {
  kura_rt_panic(message.c_str());
}

const char* kura_rt_string_cstr(void* string) {
  return string ? static_cast<StringObj*>(string)->data() : nullptr;
}

void* kura_rt_string_from_cstr_opt(const char* data) {
  return data ? NewStr(data) : nullptr;
}

void* kura_rt_string_from_cstr(const char* data, const char* what) {
  if (!data)
    Panic(std::string(what) + " returned NULL, but its result is not declared `| None`");
  return NewStr(data);
}

void kura_rt_ffi_overflow(const int64_t value, const char* what) {
  Panic(std::to_string(value) + " doesn't fit " + what);
}

void kura_rt_ffi_error(const char* message) {
  Panic(message);
}

void kura_rt_expect_tag(const uint64_t tag, const uint64_t expected) {
  if (tag != expected)
    Panic("expected " + TagName(expected) + " but got " + TagName(tag));
}

void kura_rt_register_variant(const uint64_t hash, const char* name) {
  VariantNames()[hash | kVariantBit] = name;
}

void* kura_rt_string_new(const char* data, const uint64_t length) {
  return NewStr(std::string_view(data, length));
}

void* kura_rt_string_concat(void* a, void* b) {
  const auto x = static_cast<StringObj*>(a);
  const auto y = static_cast<StringObj*>(b);
  std::string joined{};
  joined.reserve(x->length + y->length);
  joined.append(x->view()).append(y->view());
  return NewStr(joined);
}

int64_t kura_rt_string_cmp(void* a, void* b) {
  const auto c = static_cast<StringObj*>(a)->view().compare(static_cast<StringObj*>(b)->view());
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

void* kura_rt_seq_new(const uint64_t capacity) {
  return NewSeqObj(capacity);
}

KuraValue kura_rt_seq_get(void* seq, const int64_t index) {
  const auto s = static_cast<SeqObj*>(seq);
  if (index < 0 || static_cast<uint64_t>(index) >= s->length)
    Panic("index " + std::to_string(index) + " is out of range for a Seq of " + std::to_string(s->length));

  return FromHeap(s->items()[index]);
}

void kura_rt_vec_from(const uint64_t tag, const uint64_t bits, const uint64_t lanes, float* out) {
  const std::string name = lanes == 2 ? "Vec2" : lanes == 3 ? "Vec3" : "Rect";
  if (tag != kTagSeq)
    Panic("a " + name + " is made from a list of " + std::to_string(lanes) + " numbers, not from a " + TagName(tag));

  const auto seq = reinterpret_cast<SeqObj*>(bits);
  const auto items = seq->items();
  if (seq->length != lanes)
    Panic("a " + name + " takes " + std::to_string(lanes) + " numbers, but this list has " +
          std::to_string(seq->length));

  for (size_t i = 0; i < seq->length; i++) {
    const auto& item = items[i];
    if (item.tag != kTagInt && item.tag != kTagFloat)
      Panic("a " + name + " takes numbers, but element " + std::to_string(i + 1) + " is a " + TagName(item.tag));
    if (item.tag != items[0].tag)
      Panic("a " + name + " takes numbers of one type, but this list mixes " + TagName(items[0].tag) + " and " +
            TagName(item.tag));

    if (item.tag == kTagInt) {
      out[i] = static_cast<float>(static_cast<int64_t>(item.bits));
    } else {
      double value = 0;
      std::memcpy(&value, &item.bits, sizeof(value));
      out[i] = static_cast<float>(value);
    }
  }
}

void* kura_rt_seq_from_vec(const float* values, const uint64_t lanes) {
  const auto seq = NewSeqObj(lanes);
  for (uint64_t i = 0; i < lanes; i++)
    seq->Push(ToHeap(MakeFloat(static_cast<double>(values[i]))));
  return seq;
}

void* kura_rt_seq_concat(void* a, void* b) {
  const auto x = static_cast<SeqObj*>(a);
  const auto y = static_cast<SeqObj*>(b);
  const auto result = NewSeqObj(x->length + y->length);
  for (uint64_t i = 0; i < x->length; i++)
    result->Push(x->items()[i]);
  for (uint64_t i = 0; i < y->length; i++)
    result->Push(y->items()[i]);
  return result;
}

void kura_rt_draw_rect() {
  NOT_IMPLEMENTED;
}

int64_t kura_rt_len(void* object) {
  if (KindOf(object) == ObjKind::kSeq)
    return static_cast<int64_t>(static_cast<SeqObj*>(object)->length);

  if (KindOf(object) == ObjKind::kNode) {
    const auto children = static_cast<NodeObj*>(object)->children;
    return children ? static_cast<int64_t>(children->length) : 0;
  }

  Panic("can't take the length of this value");
  return 0;
}

void kura_rt_store_index(void* object, const int64_t index, const uint64_t tag, const uint64_t bits) {
  const kura::heap::Value value{tag, bits};
  if (KindOf(object) == ObjKind::kSeq) {
    const auto seq = static_cast<SeqObj*>(object);
    if (index == static_cast<int64_t>(seq->length) && seq->length < seq->capacity) {
      seq->Push(value);
    } else if (index >= 0 && static_cast<uint64_t>(index) < seq->length) {
      seq->items()[index] = value;
    } else {
      Panic("index " + std::to_string(index) + " is out of range for a Seq of " + std::to_string(seq->length) +
            " (room for " + std::to_string(seq->capacity) + ")");
    }

    return;
  }

  if (KindOf(object) == ObjKind::kNode) {
    const auto node = static_cast<NodeObj*>(object);
    if (tag == kTagNone)
      return;
    if (tag == kTagSeq) {
      const auto from = reinterpret_cast<SeqObj*>(bits);
      for (uint64_t idx = 0; idx < from->length; idx++) {
        const auto item = from->items()[idx];
        if (item.tag != kTagNone)
          NodeChildren(node, 1)->Push(item);
      }

      return;
    }

    NodeChildren(node, 1)->Push(value);
    return;
  }

  Panic("can't store into this value");
}

void* kura_rt_record_new(const uint64_t capacity) {
  return NewRecordObj(capacity);
}

uint64_t kura_rt_record_count(void* object) {
  if (KindOf(object) == ObjKind::kRecord)
    return static_cast<RecordObj*>(object)->count;

  if (KindOf(object) == ObjKind::kNode) {
    const auto props = static_cast<NodeObj*>(object)->props;
    return props ? props->count : 0;
  }

  Panic("this value has no properties");
  return 0;
}

// What a record is for reading: a record, or the properties of a node (null if it has none yet).
static auto Props(void* object) -> const RecordObj* {
  if (KindOf(object) == ObjKind::kRecord)
    return static_cast<RecordObj*>(object);

  if (KindOf(object) == ObjKind::kNode)
    return static_cast<NodeObj*>(object)->props;

  Panic("this value has no properties");
  return nullptr;
}

static void SetProperty(void* object, const uint64_t hash, const char* name, const KuraValue value) {
  if (KindOf(object) == ObjKind::kNode)
    return NodeSet(static_cast<NodeObj*>(object), hash, name, value);

  if (KindOf(object) != ObjKind::kRecord)
    Panic("this value has no properties");

  // a record is as big as it was made: the compiler counts the fields it will have, so this is its bug
  if (!static_cast<RecordObj*>(object)->Set(hash, name, ToHeap(value)))
    Panic(std::string("internal error: no room for the field `") + name + "` in a record of " +
          std::to_string(static_cast<RecordObj*>(object)->capacity));
}

void kura_rt_record_set(void* object, const uint64_t hash, const char* name, const uint64_t tag, const uint64_t bits) {
  SetProperty(object, hash, name, {tag, bits});
}

KuraValue kura_rt_record_get(void* object, const uint64_t hash) {
  const auto props = Props(object);
  const auto field = props ? props->Find(hash) : nullptr;
  return field ? FromHeap(field->value) : KuraValue{kTagNone, 0};
}

void kura_rt_record_spread(void* dst, void* src) {
  const auto from = Props(src);
  if (from == nullptr)
    return;

  for (uint32_t idx = 0; idx < from->count; idx++) {
    const auto& field = from->fields()[idx];
    SetProperty(dst, field.hash, field.name, FromHeap(field.value));
  }
}

void* kura_rt_record_merge(void* a, void* b) {
  const auto result = NewRecordObj(kura_rt_record_count(a) + kura_rt_record_count(b));
  kura_rt_record_spread(result, a);
  kura_rt_record_spread(result, b);
  return result;
}

KuraValue kura_rt_get_dyn(const uint64_t tag, const uint64_t bits, const uint64_t hash) {
  if (tag == kTagNone)
    Panic("can't read a property of None; use `?.`");

  if (tag != kTagRecord && tag != kTagNode)
    Panic("can't read a property of a " + TagName(tag));

  return kura_rt_record_get(reinterpret_cast<void*>(bits), hash);
}

KuraValue kura_rt_get_safe(const uint64_t tag, const uint64_t bits, const uint64_t hash) {
  if (tag == kTagNone)
    return {kTagNone, 0};

  return kura_rt_get_dyn(tag, bits, hash);
}

void* kura_rt_node_new(const char* kind) {
  return kura::heap::NewNode(TheHeap(), AllocationSpace(), kind);
}

void* kura_rt_closure_new(void* function, const uint64_t arity, const uint64_t captures) {
  return kura::heap::NewClosure(TheHeap(), AllocationSpace(), function, arity, captures);
}

void kura_rt_closure_capture(void* closure, const uint64_t index, const uint64_t tag, const uint64_t bits) {
  static_cast<ClosureObj*>(closure)->captures()[index] = {tag, bits};
}

KuraValue kura_rt_closure_get_capture(void* closure, const uint64_t index) {
  return FromHeap(static_cast<ClosureObj*>(closure)->captures()[index]);
}

void* kura_rt_closure_fn(void* closure, const uint64_t arguments) {
  const auto c = static_cast<ClosureObj*>(closure);
  if (c->arity != arguments)
    Panic("expected " + std::to_string(c->arity) + " argument(s) but got " + std::to_string(arguments));

  return c->function;
}

static void InitStackObject(void* memory, const kura::heap::ObjKind kind, const uint64_t payload) {
  const auto total = (sizeof(kura::heap::HeapObject) + payload + kura::kObjectAlignment - 1) &
                     ~static_cast<uint64_t>(kura::kObjectAlignment - 1);
  std::memset(memory, 0, total);
  static_cast<kura::heap::HeapObject*>(memory)->header =
      kura::heap::ObjectHeader::Make(kind, kura::heap::Space::kStatic, total);
}

void kura_rt_stack_seq(void* memory, const uint64_t capacity) {
  InitStackObject(memory, kura::heap::ObjKind::kSeq, kura::heap::SeqObj::PayloadSize(capacity));
  static_cast<kura::heap::SeqObj*>(memory)->capacity = capacity;
}

void kura_rt_stack_record(void* memory, const uint64_t capacity) {
  InitStackObject(memory, kura::heap::ObjKind::kRecord,
                  kura::heap::RecordObj::PayloadSize(static_cast<uint32_t>(capacity)));
  static_cast<kura::heap::RecordObj*>(memory)->capacity = static_cast<uint32_t>(capacity);
}

void kura_rt_stack_closure(void* memory, void* function, const uint64_t arity, const uint64_t captures) {
  InitStackObject(memory, kura::heap::ObjKind::kClosure, kura::heap::ClosureObj::PayloadSize(captures));
  const auto closure = static_cast<kura::heap::ClosureObj*>(memory);
  closure->function = function;
  closure->arity = arity;
  closure->count = captures;
}

void kura_rt_stack_box(void* memory, const uint64_t tag, const uint64_t bits) {
  InitStackObject(memory, kura::heap::ObjKind::kBox, kura::heap::BoxObj::PayloadSize());
  static_cast<kura::heap::BoxObj*>(memory)->value = {tag, bits};
}

void* kura_rt_box(const uint64_t tag, const uint64_t bits) {
  return NewBoxObj({tag, bits});
}

KuraValue kura_rt_unbox(void* box) {
  if (!box)
    return {kTagNone, 0};

  return FromHeap(static_cast<BoxObj*>(box)->value);
}

int64_t kura_rt_sdiv(const int64_t a, const int64_t b) {
  if (b == 0)
    Panic("division by zero");

  if (b == -1)
    return static_cast<int64_t>(0 - static_cast<uint64_t>(a));

  return a / b;
}

int64_t kura_rt_srem(const int64_t a, const int64_t b) {
  if (b == 0)
    Panic("division by zero");

  if (b == -1)
    return 0;

  return a % b;
}

uint64_t kura_rt_eq(const uint64_t a_tag, const uint64_t a_bits, const uint64_t b_tag, const uint64_t b_bits) {
  return Equal({a_tag, a_bits}, {b_tag, b_bits}) ? 1 : 0;
}

static auto Bool(const bool value) -> KuraValue {
  return {kTagBool, value ? 1ULL : 0ULL};
}

static auto Arith(const kura::BinaryOp op, const KuraValue& a, const KuraValue& b) -> KuraValue {
  using kura::BinaryOp;
  const auto bad = [&]() -> KuraValue {
    Panic(std::string("can't apply `") + std::string(kura::ToString(op)) + "` to " + TagName(a.tag) + " and " +
          TagName(b.tag));
    return KuraValue{};
  };

  if (a.tag == kTagInt && b.tag == kTagInt) {
    const auto x = static_cast<int64_t>(a.bits);
    const auto y = static_cast<int64_t>(b.bits);
    switch (op) {
      case BinaryOp::kAdd:
        return {kTagInt, static_cast<uint64_t>(x) + static_cast<uint64_t>(y)};
      case BinaryOp::kSubtract:
        return {kTagInt, static_cast<uint64_t>(x) - static_cast<uint64_t>(y)};
      case BinaryOp::kMultiply:
        return {kTagInt, static_cast<uint64_t>(x) * static_cast<uint64_t>(y)};
      case BinaryOp::kDivide:
        return {kTagInt, static_cast<uint64_t>(kura_rt_sdiv(x, y))};
      case BinaryOp::kModulus:
        return {kTagInt, static_cast<uint64_t>(kura_rt_srem(x, y))};
      default:
        return bad();
    }
  }

  if (IsScalar(a.tag) && IsScalar(b.tag) && a.tag != b.tag)
    Panic("can't mix Int and Float; convert one of them explicitly");

  const auto number = [](const KuraValue& v) {
    return v.tag == kTagInt ? static_cast<double>(static_cast<int64_t>(v.bits)) : AsDouble(v.bits);
  };
  const auto make = [](const uint64_t tag, const double value) {
    return KuraValue{tag, FromDouble(value)};
  };

  if (a.tag == kTagFloat && b.tag == kTagFloat) {
    const auto x = AsDouble(a.bits);
    const auto y = AsDouble(b.bits);
    switch (op) {
      case BinaryOp::kAdd:
        return make(kTagFloat, x + y);
      case BinaryOp::kSubtract:
        return make(kTagFloat, x - y);
      case BinaryOp::kMultiply:
        return make(kTagFloat, x * y);
      case BinaryOp::kDivide:
        return make(kTagFloat, x / y);
      case BinaryOp::kModulus:
        return make(kTagFloat, std::fmod(x, y));
      default:
        return bad();
    }
  }

  if (IsUnit(a.tag) && a.tag == b.tag) {
    const auto x = AsDouble(a.bits);
    const auto y = AsDouble(b.bits);
    switch (op) {
      case BinaryOp::kAdd:
        return make(a.tag, x + y);
      case BinaryOp::kSubtract:
        return make(a.tag, x - y);
      case BinaryOp::kModulus:
        return make(a.tag, std::fmod(x, y));
      case BinaryOp::kDivide:
        return make(kTagFloat, x / y);
      default:
        return bad();
    }
  }

  if (IsUnit(a.tag) && IsScalar(b.tag)) {
    if (op == BinaryOp::kMultiply)
      return make(a.tag, AsDouble(a.bits) * number(b));

    if (op == BinaryOp::kDivide)
      return make(a.tag, AsDouble(a.bits) / number(b));
  }

  if (IsScalar(a.tag) && IsUnit(b.tag) && op == BinaryOp::kMultiply)
    return make(b.tag, number(a) * AsDouble(b.bits));

  return bad();
}

static auto Order(const kura::BinaryOp op, const KuraValue& a, const KuraValue& b) -> KuraValue {
  using kura::BinaryOp;
  int cmp = 0;
  if (a.tag == kTagInt && b.tag == kTagInt) {
    const auto x = static_cast<int64_t>(a.bits);
    const auto y = static_cast<int64_t>(b.bits);
    cmp = x < y ? -1 : (x > y ? 1 : 0);
  } else if ((a.tag == kTagFloat && b.tag == kTagFloat) || (IsUnit(a.tag) && a.tag == b.tag)) {
    const auto x = AsDouble(a.bits);
    const auto y = AsDouble(b.bits);
    if (std::isnan(x) || std::isnan(y))
      return Bool(false);

    cmp = x < y ? -1 : (x > y ? 1 : 0);
  } else if (a.tag == kTagString && b.tag == kTagString) {
    cmp = static_cast<int>(kura_rt_string_cmp(reinterpret_cast<void*>(a.bits), reinterpret_cast<void*>(b.bits)));
  } else if (IsScalar(a.tag) && IsScalar(b.tag)) {
    Panic("can't mix Int and Float; convert one of them explicitly");
  } else {
    Panic(std::string("can't order ") + TagName(a.tag) + " and " + TagName(b.tag));
  }

  switch (op) {
    case BinaryOp::kLessThan:
      return Bool(cmp < 0);
    case BinaryOp::kLessThanEqual:
      return Bool(cmp <= 0);
    case BinaryOp::kGreaterThan:
      return Bool(cmp > 0);
    default:
      return Bool(cmp >= 0);
  }
}

KuraValue kura_rt_binop(const uint64_t op_code, const uint64_t a_tag, const uint64_t a_bits, const uint64_t b_tag,
                        const uint64_t b_bits) {
  using kura::BinaryOp;
  const auto op = static_cast<BinaryOp>(op_code);
  const KuraValue a{a_tag, a_bits};
  const KuraValue b{b_tag, b_bits};
  switch (op) {
    case BinaryOp::kEq:
      return Bool(Equal(a, b));
    case BinaryOp::kNeq:
      return Bool(!Equal(a, b));
    case BinaryOp::kLessThan:
    case BinaryOp::kLessThanEqual:
    case BinaryOp::kGreaterThan:
    case BinaryOp::kGreaterThanEqual:
      return Order(op, a, b);
    case BinaryOp::kBinaryAnd:
    case BinaryOp::kBinaryOr:
      if (a.tag != kTagBool || b.tag != kTagBool)
        Panic("`&&` and `||` need Bool operands");

      return Bool(op == BinaryOp::kBinaryAnd ? (a.bits && b.bits) : (a.bits || b.bits));
    case BinaryOp::kAdd:
      if (a.tag == kTagString && b.tag == kTagString)
        return {kTagString, reinterpret_cast<uint64_t>(kura_rt_string_concat(reinterpret_cast<void*>(a.bits),
                                                                             reinterpret_cast<void*>(b.bits)))};
      if (a.tag == kTagSeq && b.tag == kTagSeq)
        return {kTagSeq, reinterpret_cast<uint64_t>(
                             kura_rt_seq_concat(reinterpret_cast<void*>(a.bits), reinterpret_cast<void*>(b.bits)))};

      if (a.tag == kTagRecord && b.tag == kTagRecord)
        return {kTagRecord, reinterpret_cast<uint64_t>(kura_rt_record_merge(reinterpret_cast<void*>(a.bits),
                                                                            reinterpret_cast<void*>(b.bits)))};

      return Arith(op, a, b);
    default:
      return Arith(op, a, b);
  }
}

KuraValue kura_rt_unop(const uint64_t op_code, const uint64_t tag, const uint64_t bits) {
  using kura::UnaryOp;
  switch (static_cast<UnaryOp>(op_code)) {
    case UnaryOp::kBang:
      if (tag != kTagBool)
        Panic("`!` needs a Bool, not " + TagName(tag));

      return Bool(bits == 0);
    case UnaryOp::kStringify:
      // what `{...}` in a string puts there: a string is itself, and any other value as it is written
      if (tag == kTagString)
        return {tag, bits};
      return MakeString(Format({tag, bits}));
    case UnaryOp::kPlus:
      if (!IsScalar(tag) && !IsUnit(tag))
        Panic("can't apply `+` to " + TagName(tag));

      return {tag, bits};
    default:
      if (tag == kTagInt)
        return {kTagInt, 0 - bits};

      if (tag == kTagFloat || IsUnit(tag))
        return {tag, FromDouble(-AsDouble(bits))};

      Panic("can't negate " + TagName(tag));
      return {};
  }
}
}
