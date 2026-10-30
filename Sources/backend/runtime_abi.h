#ifndef KURA_RUNTIME_ABI_H
#define KURA_RUNTIME_ABI_H

#include <cstdint>
#include <string>
#include <string_view>

namespace kura::rt {
struct KuraValue {
  uint64_t tag;
  uint64_t bits;
};

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
  kTagHandle = 14,
  kTagVoid = 15,
};

static constexpr const uint64_t kVariantBit = 1ULL << 63;

// FNV-1a
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

auto Format(const KuraValue& value) -> std::string;

auto MakeString(const std::string& value) -> KuraValue;
}  // namespace kura::rt

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
  V(kura_rt_srem)                 \
  V(kura_rt_string_cstr)          \
  V(kura_rt_string_from_cstr)     \
  V(kura_rt_string_from_cstr_opt) \
  V(kura_rt_ffi_overflow)         \
  V(kura_rt_ffi_error)            \
  V(kura_rt_check_renderer)       \
  V(kura_rt_begin_frame)          \
  V(kura_rt_end_frame)            \
  V(kura_rt_fill_rect)            \
  V(kura_rt_draw_rect)            \
  V(kura_rt_draw_text)
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

int64_t kura_rt_len(void* object);
void kura_rt_store_index(void* object, int64_t index, uint64_t tag, uint64_t bits);

void kura_rt_check_renderer();
void kura_rt_begin_frame(uint32_t width, uint32_t height);
void kura_rt_end_frame();
void kura_rt_fill_rect(uint64_t xbits, uint64_t ybits, uint64_t wbits, uint64_t hbits, uint64_t color);
void kura_rt_draw_rect();
void kura_rt_draw_text(void* text);

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
void* kura_rt_box(uint64_t tag, uint64_t bits);
KuraValue kura_rt_unbox(void* box);

KuraValue kura_rt_binop(uint64_t op, uint64_t a_tag, uint64_t a_bits, uint64_t b_tag, uint64_t b_bits);
KuraValue kura_rt_unop(uint64_t op, uint64_t tag, uint64_t bits);
uint64_t kura_rt_eq(uint64_t a_tag, uint64_t a_bits, uint64_t b_tag, uint64_t b_bits);
int64_t kura_rt_sdiv(int64_t a, int64_t b);
int64_t kura_rt_srem(int64_t a, int64_t b);

const char* kura_rt_string_cstr(void* string);
void* kura_rt_string_from_cstr(const char* data, const char* what);
void* kura_rt_string_from_cstr_opt(const char* data);
[[noreturn]] void kura_rt_ffi_overflow(int64_t value, const char* what);
[[noreturn]] void kura_rt_ffi_error(const char* message);
}

#endif  // KURA_RUNTIME_ABI_H
