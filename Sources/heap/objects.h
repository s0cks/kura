#ifndef KURA_HEAP_OBJECTS_H
#define KURA_HEAP_OBJECTS_H

#include <cstdint>
#include <string_view>

#include "heap/base.h"
#include "heap/header.h"
#include "heap/value.h"

namespace kura::heap {
class Heap;

// The runtime's objects, laid out flat: a header, a few fixed fields, then the elements inline. There are no
// std::string or std::vector members and no vtables, so a heap object is a single block of memory that can be copied
// byte for byte (the model space moves its objects) and whose pointers are all findable from its kind and tag words
// (see VisitPointerSlots).
//
// Everything here is immutable once built, apart from filling in the elements of a freshly allocated object. That is
// what lets the heap skip a write barrier.
struct HeapObject {
  ObjectHeader header;

  inline auto kind() const -> ObjKind {
    return header.kind();
  }

  inline auto space() const -> Space {
    return header.space();
  }

  inline auto size() const -> uword {
    return header.size();
  }

  inline auto address() const -> uword {
    return reinterpret_cast<uword>(this);
  }

  template <typename T>
  inline auto As() -> T* {
    return static_cast<T*>(this);
  }
};
static_assert(sizeof(HeapObject) == 8);

struct StringObj : HeapObject {
  uint64_t length;
  // `length` bytes follow, then a NUL so the bytes can be handed to C as they are.

  inline auto data() -> char* {
    return reinterpret_cast<char*>(this + 1);
  }

  inline auto data() const -> const char* {
    return reinterpret_cast<const char*>(this + 1);
  }

  inline auto view() const -> std::string_view {
    return {data(), static_cast<size_t>(length)};
  }

  static constexpr auto PayloadSize(const uint64_t length) -> uword {
    return sizeof(uint64_t) + length + 1;
  }
};

struct SeqObj : HeapObject {
  uint64_t length;
  uint64_t capacity;
  // `capacity` Values follow; the first `length` are filled in.

  inline auto items() -> Value* {
    return reinterpret_cast<Value*>(this + 1);
  }

  inline auto items() const -> const Value* {
    return reinterpret_cast<const Value*>(this + 1);
  }

  inline void Push(const Value value) {
    KURA_HEAP_CHECK(length < capacity, "(seq of %llu is full)", static_cast<unsigned long long>(capacity));
    items()[length++] = value;
  }

  static constexpr auto PayloadSize(const uint64_t capacity) -> uword {
    return 2 * sizeof(uint64_t) + capacity * sizeof(Value);
  }
};

struct RecordObj : HeapObject {
  struct Field {
    uint64_t hash;
    const char* name;  // static data in the compiled module, not a heap object
    Value value;
  };
  static_assert(sizeof(Field) == 32);

  uint32_t count;
  uint32_t capacity;
  // `capacity` Fields follow; the first `count` are filled in.

  inline auto fields() -> Field* {
    return reinterpret_cast<Field*>(this + 1);
  }

  inline auto fields() const -> const Field* {
    return reinterpret_cast<const Field*>(this + 1);
  }

  auto Find(uint64_t hash) const -> const Field*;

  // A later write to a field keeps the field's first position. Returns false when the field is new and the record is
  // full: a record's size is fixed when it is allocated, so a new field means a new, bigger record.
  auto Set(uint64_t hash, const char* name, Value value) -> bool;

  static constexpr auto PayloadSize(const uint32_t capacity) -> uword {
    return 2 * sizeof(uint32_t) + static_cast<uword>(capacity) * sizeof(Field);
  }
};

struct ClosureObj : HeapObject {
  void* function;  // compiled code, not a heap object
  uint64_t arity;
  uint64_t count;
  // `count` captured Values follow.

  inline auto captures() -> Value* {
    return reinterpret_cast<Value*>(this + 1);
  }

  inline auto captures() const -> const Value* {
    return reinterpret_cast<const Value*>(this + 1);
  }

  static constexpr auto PayloadSize(const uint64_t count) -> uword {
    return 3 * sizeof(uint64_t) + count * sizeof(Value);
  }
};

// A variant's payload.
struct BoxObj : HeapObject {
  Value value;

  static constexpr auto PayloadSize() -> uword {
    return sizeof(Value);
  }
};

// Transitional: `view` is compiled to render calls and no longer builds Nodes, but the runtime ABI still has them
// (kura_rt_node_new) until the compile-time vdom replaces them completely.
struct NodeObj : HeapObject {
  const char* kind_name;  // static data
  RecordObj* props;
  SeqObj* children;

  static constexpr auto PayloadSize() -> uword {
    return 3 * sizeof(uint64_t);
  }
};

constexpr auto KindHasPointers(const ObjKind kind) -> bool {
  switch (kind) {
    case ObjKind::kSeq:
    case ObjKind::kRecord:
    case ObjKind::kNode:
    case ObjKind::kClosure:
    case ObjKind::kBox:
      return true;
    default:
      return false;
  }
}

// Calls `visit(uint64_t* slot)` for every word in the object that may hold the address of a heap object. The slot is
// passed by address so a collector can rewrite it when the target moves. Words that are not heap pointers (numbers,
// handles, code addresses, static names) are never passed. Precise: the answer comes from the object's kind and its
// values' tags, with no per-type tables.
template <typename F>
void VisitPointerSlots(HeapObject* obj, F&& visit) {
  const auto visit_value = [&](Value& value) {
    if (TagMayHoldHeapPointer(value.tag) && value.bits != 0)
      visit(&value.bits);
  };
  switch (obj->kind()) {
    case ObjKind::kSeq: {
      const auto seq = obj->As<SeqObj>();
      for (uint64_t idx = 0; idx < seq->length; idx++)
        visit_value(seq->items()[idx]);
      return;
    }
    case ObjKind::kRecord: {
      const auto record = obj->As<RecordObj>();
      for (uint32_t idx = 0; idx < record->count; idx++)
        visit_value(record->fields()[idx].value);
      return;
    }
    case ObjKind::kClosure: {
      const auto closure = obj->As<ClosureObj>();
      for (uint64_t idx = 0; idx < closure->count; idx++)
        visit_value(closure->captures()[idx]);
      return;
    }
    case ObjKind::kBox:
      visit_value(obj->As<BoxObj>()->value);
      return;
    case ObjKind::kNode: {
      const auto node = obj->As<NodeObj>();
      if (node->props)
        visit(reinterpret_cast<uint64_t*>(&node->props));
      if (node->children)
        visit(reinterpret_cast<uint64_t*>(&node->children));
      return;
    }
    default:
      return;
  }
}

// Allocation helpers. Each allocates in `space`, fills in the object's fixed fields and returns it (null only if a
// custom out-of-memory handler returned: see Heap::SetOutOfMemoryHandler). Element slots start zeroed, which reads as
// an invalid, non-pointer Value, so an object is always safe to scan, even half built.
auto NewString(Heap& heap, Space space, std::string_view data) -> StringObj*;
auto NewSeq(Heap& heap, Space space, uint64_t capacity) -> SeqObj*;
auto NewRecord(Heap& heap, Space space, uint32_t capacity) -> RecordObj*;
auto NewClosure(Heap& heap, Space space, void* function, uint64_t arity, uint64_t captures) -> ClosureObj*;
auto NewBox(Heap& heap, Space space, Value value) -> BoxObj*;
auto NewNode(Heap& heap, Space space, const char* kind_name) -> NodeObj*;
}  // namespace kura::heap

#endif  // KURA_HEAP_OBJECTS_H
