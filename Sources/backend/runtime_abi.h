#ifndef KURA_RUNTIME_ABI_H
#define KURA_RUNTIME_ABI_H

#include <cstdint>
#include <string>
#include <string_view>

// The interface between compiled kura code and the runtime library.
//
// Compiled code keeps a value unboxed (an i64, a double, an i1) while its static
// type is a single concrete scalar. Where the type is `Number`, `Any`, a union
// or a variant, the value is a KuraValue: a tag saying what it is, and 64 bits
// of payload. The heap objects (strings, seqs, records, UI nodes, closures) are
// runtime-managed and hold their elements as KuraValues, so converting a
// `Seq<Int>` to a `Seq<Number>`, or a record to a wider record type, never
// changes representation.
//
// Functions that take or return a KuraValue take its two words as separate
// arguments and return it in the pair of return registers, which is how a C
// struct of two u64 is passed on x86-64, so generated code can call them as plain
// functions.
//
// The runtime never frees anything: a program's heap is reclaimed when the host
// exits.

namespace kura::rt {
struct KuraValue {
  uint64_t tag;
  uint64_t bits;
};

// What a tagged value is. A variant's tag is the hash of its name with the top
// bit set, so it never collides with these.
enum Tag : uint64_t {
  kTagInvalid = 0,
  kTagNone = 1,
  kTagBool = 2,
  kTagInt = 3,
  kTagFloat = 4,
  kTagString = 5,
  kTagColor = 6,
  kTagPx = 7,
  kTagPct = 8,
  kTagFr = 9,
  kTagNode = 10,
  kTagSeq = 11,
  kTagRecord = 12,
  kTagFunction = 13,
};

static constexpr const uint64_t kVariantBit = 1ULL << 63;

// FNV-1a. Property names and variant names are identified by their hash, so the
// compiled code doesn't depend on any registry in the compiling process.
static inline constexpr auto HashName(const std::string_view name) -> uint64_t {
  uint64_t hash = 14695981039346656037ULL;
  for (const char c : name) {
    hash ^= static_cast<uint8_t>(c);
    hash *= 1099511628211ULL;
  }
  return hash;
}

static inline constexpr auto VariantTag(const std::string_view name) -> uint64_t {
  return HashName(name) | kVariantBit;
}

static inline constexpr auto IsVariantTag(const uint64_t tag) -> bool {
  return (tag & kVariantBit) != 0;
}

// Renders a value the way `kurac --run` prints it.
auto Format(const KuraValue& value) -> std::string;

// Makes a value from a string for the host: used by the tests.
auto MakeString(const std::string& value) -> KuraValue;
}  // namespace kura::rt

// The functions compiled code calls. Each is listed once: the table below
// declares them, and the JIT uses it to bind them by name.
//
// clang-format off
#define KURA_RUNTIME_FUNCTIONS(V) \
  V(kura_rt_panic)                \
  V(kura_rt_expect_tag)           \
  V(kura_rt_register_variant)     \
  V(kura_rt_string_new)           \
  V(kura_rt_string_concat)        \
  V(kura_rt_string_cmp)           \
  V(kura_rt_seq_new)              \
  V(kura_rt_seq_get)              \
  V(kura_rt_seq_concat)           \
  V(kura_rt_len)                  \
  V(kura_rt_store_index)          \
  V(kura_rt_record_new)           \
  V(kura_rt_record_set)           \
  V(kura_rt_record_get)           \
  V(kura_rt_record_spread)        \
  V(kura_rt_record_merge)         \
  V(kura_rt_get_dyn)              \
  V(kura_rt_get_safe)             \
  V(kura_rt_node_new)             \
  V(kura_rt_closure_new)          \
  V(kura_rt_closure_capture)      \
  V(kura_rt_closure_get_capture)  \
  V(kura_rt_closure_fn)           \
  V(kura_rt_box)                  \
  V(kura_rt_unbox)                \
  V(kura_rt_binop)                \
  V(kura_rt_unop)                 \
  V(kura_rt_eq)                   \
  V(kura_rt_sdiv)                 \
  V(kura_rt_srem)
// clang-format on

extern "C" {
using kura::rt::KuraValue;

[[noreturn]] void kura_rt_panic(const char* message);
void kura_rt_expect_tag(uint64_t tag, uint64_t expected);
void kura_rt_register_variant(uint64_t hash, const char* name);

void* kura_rt_string_new(const char* data, uint64_t length);
void* kura_rt_string_concat(void* a, void* b);
int64_t kura_rt_string_cmp(void* a, void* b);

void* kura_rt_seq_new(uint64_t capacity);
KuraValue kura_rt_seq_get(void* seq, int64_t index);
void* kura_rt_seq_concat(void* a, void* b);

// Seqs and nodes (children)
int64_t kura_rt_len(void* object);
void kura_rt_store_index(void* object, int64_t index, uint64_t tag, uint64_t bits);

// Records and nodes (properties)
void* kura_rt_record_new();
void kura_rt_record_set(void* object, uint64_t hash, const char* name, uint64_t tag, uint64_t bits);
KuraValue kura_rt_record_get(void* object, uint64_t hash);
void kura_rt_record_spread(void* dst, void* src);
void* kura_rt_record_merge(void* a, void* b);
KuraValue kura_rt_get_dyn(uint64_t tag, uint64_t bits, uint64_t hash);
KuraValue kura_rt_get_safe(uint64_t tag, uint64_t bits, uint64_t hash);

void* kura_rt_node_new(const char* kind);

void* kura_rt_closure_new(void* function, uint64_t arity, uint64_t captures);
void kura_rt_closure_capture(void* closure, uint64_t index, uint64_t tag, uint64_t bits);
KuraValue kura_rt_closure_get_capture(void* closure, uint64_t index);
void* kura_rt_closure_fn(void* closure, uint64_t arguments);

// A variant's payload is a boxed value.
void* kura_rt_box(uint64_t tag, uint64_t bits);
KuraValue kura_rt_unbox(void* box);

KuraValue kura_rt_binop(uint64_t op, uint64_t a_tag, uint64_t a_bits, uint64_t b_tag, uint64_t b_bits);
KuraValue kura_rt_unop(uint64_t op, uint64_t tag, uint64_t bits);
uint64_t kura_rt_eq(uint64_t a_tag, uint64_t a_bits, uint64_t b_tag, uint64_t b_bits);
int64_t kura_rt_sdiv(int64_t a, int64_t b);
int64_t kura_rt_srem(int64_t a, int64_t b);
}

#endif  // KURA_RUNTIME_ABI_H
