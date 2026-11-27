#include "heap/objects.h"

#include <cstring>

#include "heap/heap.h"

namespace kura::heap {
static_assert(sizeof(StringObj) == 16);
static_assert(sizeof(SeqObj) == 24);
static_assert(sizeof(RecordObj) == 16);
static_assert(sizeof(ClosureObj) == 32);
static_assert(sizeof(BoxObj) == 24);
static_assert(sizeof(NodeObj) == 32);

auto RecordObj::Find(const uint64_t hash) const -> const Field* {
  for (uint32_t idx = 0; idx < count; idx++) {
    if (fields()[idx].hash == hash)
      return &fields()[idx];
  }
  return nullptr;
}

auto RecordObj::Set(const uint64_t hash, const char* name, const Value value) -> bool {
  for (uint32_t idx = 0; idx < count; idx++) {
    if (fields()[idx].hash == hash) {
      fields()[idx].value = value;
      return true;
    }
  }

  if (count >= capacity)
    return false;

  // Keep fields sorted alphabetically by `name` to match `CompileType::Record`'s canonical field order
  uint32_t pos = 0;
  while (pos < count && std::strcmp(fields()[pos].name, name) < 0) {
    pos++;
  }

  if (pos < count)
    std::memmove(&fields()[pos + 1], &fields()[pos], (count - pos) * sizeof(Field));
  fields()[pos] = Field{hash, name, value};
  count++;
  return true;
}

template <typename T>
static inline auto New(Heap& heap, const Space space, const ObjKind kind, const uword payload) -> T* {
  return static_cast<T*>(heap.AllocateObject(space, kind, payload));
}

auto NewString(Heap& heap, const Space space, const std::string_view data) -> StringObj* {
  const auto str = New<StringObj>(heap, space, ObjKind::kString, StringObj::PayloadSize(data.size()));
  if (!str)
    return nullptr;
  str->length = data.size();
  std::memcpy(str->data(), data.data(), data.size());
  str->data()[data.size()] = '\0';
  return str;
}

auto NewTuple(Heap& heap, Space space, uint64_t count) -> TupleObj* {
  const auto tuple = New<TupleObj>(heap, space, ObjKind::kTuple, TupleObj::PayloadSize(count));
  if (!tuple)
    return nullptr;
  tuple->count = count;
  return tuple;
}

static_assert(sizeof(ColorObj) == 32);

auto NewColor(Heap& heap, const Space space, const ColorModel model, const float* components) -> ColorObj* {
  const auto color = New<ColorObj>(heap, space, ObjKind::kColor, ColorObj::PayloadSize());
  if (!color)
    return nullptr;
  color->model = model;
  color->c0 = components[0];
  color->c1 = components[1];
  color->c2 = components[2];
  color->a = components[3];
  return color;
}

auto NewSeq(Heap& heap, const Space space, const uint64_t capacity) -> SeqObj* {
  const auto seq = New<SeqObj>(heap, space, ObjKind::kSeq, SeqObj::PayloadSize(capacity));
  if (!seq)
    return nullptr;
  seq->capacity = capacity;
  return seq;
}

auto NewRecord(Heap& heap, const Space space, const uint32_t capacity) -> RecordObj* {
  const auto record = New<RecordObj>(heap, space, ObjKind::kRecord, RecordObj::PayloadSize(capacity));
  if (!record)
    return nullptr;
  record->capacity = capacity;
  return record;
}

auto NewClosure(Heap& heap, const Space space, void* function, const uint64_t arity, const uint64_t captures)
    -> ClosureObj* {
  const auto closure = New<ClosureObj>(heap, space, ObjKind::kClosure, ClosureObj::PayloadSize(captures));
  if (!closure)
    return nullptr;
  closure->function = function;
  closure->arity = arity;
  closure->count = captures;
  for (uint64_t idx = 0; idx < captures; idx++)
    closure->captures()[idx] = {tag::kNone, 0};
  return closure;
}

auto NewBox(Heap& heap, const Space space, const Value value) -> BoxObj* {
  const auto box = New<BoxObj>(heap, space, ObjKind::kBox, BoxObj::PayloadSize());
  if (!box)
    return nullptr;
  box->value = value;
  return box;
}

auto NewNode(Heap& heap, const Space space, const char* kind_name) -> NodeObj* {
  const auto node = New<NodeObj>(heap, space, ObjKind::kNode, NodeObj::PayloadSize());
  if (!node)
    return nullptr;
  node->kind_name = kind_name;
  return node;
}
}  // namespace kura::heap
