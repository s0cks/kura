#include "backend/runtime_abi.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "binary_op.h"
#include "common.h"
#include "object.h"
#include "unary_op.h"

namespace kura::rt {
namespace {
enum class ObjKind : uint32_t { kString, kSeq, kRecord, kNode, kClosure };

struct Obj {
  ObjKind kind;
  explicit Obj(const ObjKind k) :
    kind(k) {}
};

struct SeqObj : Obj {
  std::vector<KuraValue> items{};
  SeqObj() :
    Obj(ObjKind::kSeq) {}
};

struct RecordObj : Obj {
  struct Field {
    uint64_t hash;
    const char* name;
    KuraValue value;
  };
  std::vector<Field> fields{};
  RecordObj() :
    Obj(ObjKind::kRecord) {}

  void Set(const uint64_t hash, const char* name, const KuraValue value) {
    for (auto& field : fields) {
      if (field.hash == hash) {
        field.value = value;
        return;
      }
    }

    fields.push_back({hash, name, value});
  }

  auto Find(const uint64_t hash) const -> const Field* {
    for (const auto& field : fields) {
      if (field.hash == hash)
        return &field;
    }

    return nullptr;
  }
};

struct NodeObj : Obj {
  std::string kind_name;
  RecordObj props{};
  SeqObj children{};
  explicit NodeObj(std::string name) :
    Obj(ObjKind::kNode),
    kind_name(std::move(name)) {}
};

struct ClosureObj : Obj {
  void* function;
  uint64_t arity;
  std::vector<KuraValue> captures;
  ClosureObj(void* fn, const uint64_t a, const uint64_t n) :
    Obj(ObjKind::kClosure),
    function(fn),
    arity(a),
    captures(n, KuraValue{kTagNone, 0}) {}
};

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

inline auto Str(const uint64_t bits) -> String* {
  return reinterpret_cast<String*>(bits);
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
  if (a->items.size() != b->items.size())
    return false;

  for (size_t idx = 0; idx < a->items.size(); idx++) {
    if (!Equal(a->items[idx], b->items[idx]))
      return false;
  }

  return true;
}

auto RecordEqual(const RecordObj* a, const RecordObj* b) -> bool {
  if (a->fields.size() != b->fields.size())
    return false;

  for (const auto& field : a->fields) {
    const auto other = b->Find(field.hash);
    if (!other || !Equal(field.value, other->value))
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
      return Str(a.bits)->GetValue() == Str(b.bits)->GetValue();
    case kTagSeq:
      return SeqEqual(reinterpret_cast<SeqObj*>(a.bits), reinterpret_cast<SeqObj*>(b.bits));
    case kTagRecord:
      return RecordEqual(reinterpret_cast<RecordObj*>(a.bits), reinterpret_cast<RecordObj*>(b.bits));
    case kTagNode: {
      const auto x = reinterpret_cast<NodeObj*>(a.bits);
      const auto y = reinterpret_cast<NodeObj*>(b.bits);
      return x->kind_name == y->kind_name && RecordEqual(&x->props, &y->props) && SeqEqual(&x->children, &y->children);
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

    return Equal(*reinterpret_cast<KuraValue*>(a.bits), *reinterpret_cast<KuraValue*>(b.bits));
  }

  return false;
}

auto Properties(const RecordObj& props) -> std::string {
  std::stringstream ss{};
  for (size_t idx = 0; idx < props.fields.size(); idx++)
    ss << (idx ? ", " : "") << props.fields[idx].name << ": " << Format(props.fields[idx].value);

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
      return Quote(Str(value.bits)->GetValue());
    case kTagColor:
      return Str(value.bits)->GetValue();
    case kTagSeq: {
      const auto seq = reinterpret_cast<SeqObj*>(value.bits);
      std::stringstream ss{};
      ss << "[";
      for (size_t idx = 0; idx < seq->items.size(); idx++)
        ss << (idx ? ", " : "") << Format(seq->items[idx]);
      ss << "]";
      return ss.str();
    }
    case kTagRecord: {
      const auto record = reinterpret_cast<RecordObj*>(value.bits);
      return record->fields.empty() ? "{}" : "{ " + Properties(*record) + " }";
    }
    case kTagNode: {
      const auto node = reinterpret_cast<NodeObj*>(value.bits);
      std::stringstream ss{};
      ss << "#" << node->kind_name;
      if (!node->props.fields.empty())
        ss << "(" << Properties(node->props) << ")";

      if (!node->children.items.empty()) {
        ss << " {";
        for (const auto& child : node->children.items)
          ss << " " << Format(child);

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
      text += "(" + Format(*reinterpret_cast<KuraValue*>(value.bits)) + ")";

    return text;
  }

  return "<unknown>";
}

auto MakeString(const std::string& value) -> KuraValue {
  return {kTagString, reinterpret_cast<uint64_t>(String::New(value))};
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
  return string ? static_cast<kura::String*>(string)->GetValue().c_str() : nullptr;
}

void* kura_rt_string_from_cstr_opt(const char* data) {
  return data ? kura::String::New(data) : nullptr;
}

void* kura_rt_string_from_cstr(const char* data, const char* what) {
  if (!data)
    Panic(std::string(what) + " returned NULL, but its result is not declared `| None`");
  return kura::String::New(std::string(data));
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
  return kura::String::New(std::string(data, length));
}

void* kura_rt_string_concat(void* a, void* b) {
  return kura::String::New(static_cast<kura::String*>(a)->GetValue() + static_cast<kura::String*>(b)->GetValue());
}

int64_t kura_rt_string_cmp(void* a, void* b) {
  const auto c = static_cast<kura::String*>(a)->GetValue().compare(static_cast<kura::String*>(b)->GetValue());
  return c < 0 ? -1 : (c > 0 ? 1 : 0);
}

void* kura_rt_seq_new(const uint64_t capacity) {
  const auto seq = new SeqObj();
  seq->items.reserve(capacity);
  return seq;
}

KuraValue kura_rt_seq_get(void* seq, const int64_t index) {
  const auto s = static_cast<SeqObj*>(seq);
  if (index < 0 || static_cast<size_t>(index) >= s->items.size())
    Panic("index " + std::to_string(index) + " is out of range for a Seq of " + std::to_string(s->items.size()));

  return s->items[index];
}

void* kura_rt_seq_concat(void* a, void* b) {
  const auto result = new SeqObj();
  const auto x = static_cast<SeqObj*>(a);
  const auto y = static_cast<SeqObj*>(b);
  result->items = x->items;
  result->items.insert(result->items.end(), y->items.begin(), y->items.end());
  return result;
}

void kura_rt_begin_frame(uint32_t width, uint32_t height) {
  std::cout << "renering frame " << width << "x" << height << std::endl;
}

void kura_rt_end_frame() {
  NOT_IMPLEMENTED;
}

void kura_rt_fill_rect(uint64_t xbits, uint64_t ybits, uint64_t wbits, uint64_t hbits, uint64_t color) {
  const uint8_t red = (color >> 24) & 0xFF;
  const uint8_t green = (color >> 16) & 0xFF;
  const uint8_t blue = (color >> 8) & 0xFF;
  const uint8_t alpha = (color) & 0xFF;
  std::cout << "rendering filled rect " << xbits << ", " << ybits << ", " << wbits << ", " << hbits << ", "
            << "r=" << ((uint32_t)red) << ", "
            << "g=" << ((uint32_t)green) << ", "
            << "b=" << ((uint32_t)blue) << ", "
            << "a=" << ((uint32_t)alpha) << std::endl;
}

void kura_rt_draw_rect() {
  NOT_IMPLEMENTED;
}

void kura_rt_draw_text(void* a) {
  std::cout << "rendering text: " << static_cast<kura::String*>(a)->GetValue() << std::endl;
}

void kura_rt_check_renderer() {
  NOT_IMPLEMENTED;
}

int64_t kura_rt_len(void* object) {
  const auto obj = static_cast<Obj*>(object);
  if (obj->kind == ObjKind::kSeq)
    return static_cast<int64_t>(static_cast<SeqObj*>(object)->items.size());

  if (obj->kind == ObjKind::kNode)
    return static_cast<int64_t>(static_cast<NodeObj*>(object)->children.items.size());

  Panic("can't take the length of this value");
  return 0;
}

void kura_rt_store_index(void* object, const int64_t index, const uint64_t tag, const uint64_t bits) {
  const auto obj = static_cast<Obj*>(object);
  const KuraValue value{tag, bits};
  if (obj->kind == ObjKind::kSeq) {
    const auto seq = static_cast<SeqObj*>(object);
    if (index == static_cast<int64_t>(seq->items.size())) {
      seq->items.push_back(value);
    } else if (index >= 0 && static_cast<size_t>(index) < seq->items.size()) {
      seq->items[index] = value;
    } else {
      Panic("index " + std::to_string(index) + " is out of range for a Seq of " + std::to_string(seq->items.size()));
    }

    return;
  }

  if (obj->kind == ObjKind::kNode) {
    const auto children = &static_cast<NodeObj*>(object)->children;
    if (tag == kTagNone)
      return;
    if (tag == kTagSeq) {
      for (const auto& item : reinterpret_cast<SeqObj*>(bits)->items) {
        if (item.tag != kTagNone)
          children->items.push_back(item);
      }

      return;
    }

    children->items.push_back(value);
    return;
  }

  Panic("can't store into this value");
}

void* kura_rt_record_new() {
  return new RecordObj();
}

static auto Props(void* object) -> RecordObj* {
  const auto obj = static_cast<Obj*>(object);
  if (obj->kind == ObjKind::kRecord)
    return static_cast<RecordObj*>(object);

  if (obj->kind == ObjKind::kNode)
    return &static_cast<NodeObj*>(object)->props;

  Panic("this value has no properties");
  return nullptr;
}

void kura_rt_record_set(void* object, const uint64_t hash, const char* name, const uint64_t tag, const uint64_t bits) {
  Props(object)->Set(hash, name, {tag, bits});
}

KuraValue kura_rt_record_get(void* object, const uint64_t hash) {
  const auto field = Props(object)->Find(hash);
  return field ? field->value : KuraValue{kTagNone, 0};
}

void kura_rt_record_spread(void* dst, void* src) {
  const auto target = Props(dst);
  for (const auto& field : Props(src)->fields)
    target->Set(field.hash, field.name, field.value);
}

void* kura_rt_record_merge(void* a, void* b) {
  const auto result = new RecordObj();
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
  return new NodeObj(kind);
}

void* kura_rt_closure_new(void* function, const uint64_t arity, const uint64_t captures) {
  return new ClosureObj(function, arity, captures);
}

void kura_rt_closure_capture(void* closure, const uint64_t index, const uint64_t tag, const uint64_t bits) {
  static_cast<ClosureObj*>(closure)->captures[index] = {tag, bits};
}

KuraValue kura_rt_closure_get_capture(void* closure, const uint64_t index) {
  return static_cast<ClosureObj*>(closure)->captures[index];
}

void* kura_rt_closure_fn(void* closure, const uint64_t arguments) {
  const auto c = static_cast<ClosureObj*>(closure);
  if (c->arity != arguments)
    Panic("expected " + std::to_string(c->arity) + " argument(s) but got " + std::to_string(arguments));

  return c->function;
}

void* kura_rt_box(const uint64_t tag, const uint64_t bits) {
  return new KuraValue{tag, bits};
}

KuraValue kura_rt_unbox(void* box) {
  if (!box)
    return {kTagNone, 0};

  return *static_cast<KuraValue*>(box);
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
