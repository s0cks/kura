#ifndef KURA_HEAP_HEADER_H
#define KURA_HEAP_HEADER_H

#include <cstdint>

#include "heap/base.h"

namespace kura::heap {
// What a heap object is. Mirrors the runtime ABI's object kinds; the collector uses it to find an object's slots.
enum class ObjKind : uint8_t {
  kInvalid = 0,
  kString,
  kSeq,
  kRecord,
  kNode,
  kClosure,
  kBox,
  // A free block inside the tenured space. Never reachable from a Value.
  kFree = 0xFE,
};

// Which part of the heap an object lives in. This is the lifetime the compiler chose for it:
//
//   kStatic  - not managed by the heap: constants in the compiled module's data. Never moved, never freed.
//   kFrame   - lives until the end of the frame (`view`'s temporaries). Reset in one step.
//   kTick    - lives until the end of the update that created it (a Message, `update`'s temporaries).
//   kModel   - the model semispace: bump-allocated, moved by the collector when it flips.
//   kTenured - large and promoted objects: never moved, reclaimed by the sweeper.
//
// kFrame and kTick objects must never be referenced from kModel/kTenured objects: that is the compiler's escape
// analysis, and Heap::Verify checks it.
enum class Space : uint8_t {
  kStatic = 0,
  kFrame,
  kTick,
  kModel,
  kTenured,
};

constexpr auto SpaceName(const Space space) -> const char* {
  switch (space) {
    case Space::kStatic:
      return "static";
    case Space::kFrame:
      return "frame";
    case Space::kTick:
      return "tick";
    case Space::kModel:
      return "model";
    case Space::kTenured:
      return "tenured";
  }
  return "unknown";
}

// The 8 bytes in front of every heap object.
//
//    63                32 31        18 17  14   13    12     11    10..8   7..0
//   +--------------------+------------+-----+-----+-----+------+--------+------+
//   |    size (bytes)    |  reserved  | age | rem | fwd | mark | space  | kind |
//   +--------------------+------------+-----+-----+-----+------+--------+------+
//
// `size` is the whole object, header included, rounded up to the object alignment: walking a space is just adding
// each header's size to its address. `fwd` is for the collector: a forwarded object's first payload word holds its
// new address. `rem` marks a tenured object that may hold pointers into the model space (see Heap::remembered).
class ObjectHeader {
 private:
  uint64_t raw_{0};

  static constexpr uint64_t kKindShift = 0;
  static constexpr uint64_t kSpaceShift = 8;
  static constexpr uint64_t kMarkedBit = 1ULL << 11;
  static constexpr uint64_t kForwardedBit = 1ULL << 12;
  static constexpr uint64_t kRememberedBit = 1ULL << 13;
  static constexpr uint64_t kAgeShift = 14;
  static constexpr uint64_t kSizeShift = 32;

  constexpr void Set(const uint64_t bit, const bool value) {
    raw_ = value ? (raw_ | bit) : (raw_ & ~bit);
  }

 public:
  static constexpr uword kMaxSize = 0xFFFFFFFFULL;
  static constexpr uint8_t kMaxAge = 15;

  constexpr ObjectHeader() = default;

  static constexpr auto Make(const ObjKind kind, const Space space, const uword size) -> ObjectHeader {
    ObjectHeader header{};
    header.raw_ = (static_cast<uint64_t>(kind) << kKindShift) | (static_cast<uint64_t>(space) << kSpaceShift) |
                  (static_cast<uint64_t>(size) << kSizeShift);
    return header;
  }

  constexpr auto raw() const -> uint64_t {
    return raw_;
  }

  constexpr auto kind() const -> ObjKind {
    return static_cast<ObjKind>((raw_ >> kKindShift) & 0xFF);
  }

  constexpr auto space() const -> Space {
    return static_cast<Space>((raw_ >> kSpaceShift) & 0x7);
  }

  constexpr auto size() const -> uword {
    return static_cast<uword>(raw_ >> kSizeShift);
  }

  constexpr void set_size(const uword size) {
    raw_ = (raw_ & 0xFFFFFFFFULL) | (static_cast<uint64_t>(size) << kSizeShift);
  }

  constexpr auto IsFree() const -> bool {
    return kind() == ObjKind::kFree;
  }

  constexpr auto marked() const -> bool {
    return (raw_ & kMarkedBit) != 0;
  }

  constexpr void set_marked(const bool value = true) {
    Set(kMarkedBit, value);
  }

  constexpr auto forwarded() const -> bool {
    return (raw_ & kForwardedBit) != 0;
  }

  constexpr void set_forwarded(const bool value = true) {
    Set(kForwardedBit, value);
  }

  constexpr auto remembered() const -> bool {
    return (raw_ & kRememberedBit) != 0;
  }

  constexpr void set_remembered(const bool value = true) {
    Set(kRememberedBit, value);
  }

  // How many times the object has survived a flip of the model space; the collector promotes it to the tenured space
  // once it is old enough.
  constexpr auto age() const -> uint8_t {
    return static_cast<uint8_t>((raw_ >> kAgeShift) & 0xF);
  }

  constexpr void set_age(const uint8_t age) {
    raw_ = (raw_ & ~(0xFULL << kAgeShift)) | (static_cast<uint64_t>(age & 0xF) << kAgeShift);
  }

  constexpr auto operator==(const ObjectHeader& rhs) const -> bool {
    return raw_ == rhs.raw_;
  }
};
static_assert(sizeof(ObjectHeader) == 8);

static constexpr uword kHeaderSize = sizeof(ObjectHeader);

// The total size of an object with `payload` bytes after its header.
constexpr auto ObjectSizeFor(const uword payload) -> uword {
  const auto body = payload < kWordSize ? kWordSize : payload;
  return RoundUp(kHeaderSize + body, kObjectAlignment);
}
}  // namespace kura::heap

#endif  // KURA_HEAP_HEADER_H
