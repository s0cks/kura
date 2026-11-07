#ifndef KURA_C_TYPE_H
#define KURA_C_TYPE_H

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "backend/compile_type.h"

namespace kura {
struct CRecord;
struct CSignature;

// A type as C sees it. Kura has Int, Float and so on; a C function has a
// particular width and signedness for each argument, which the call converts
// to and from (and checks, when a value wouldn't fit).
struct CType {
  enum Kind {
    kVoid,
    kBool,
    kI8,
    kI16,
    kI32,
    kI64,
    kU8,
    kU16,
    kU32,
    kU64,
    kF32,
    kF64,
    kCString,  // const char *: a String
    kHandle,   // an opaque pointer: a Handle
    kRecord,   // a struct passed by value: a record
    kOut,      // `T *` that C writes through: the call returns it
    kCallback, // a function pointer: a named Kura function
  };

  Kind kind = kVoid;
  // kHandle: what the pointer points to (`FILE`, `ptr` for `void *`).
  std::string handle{};
  // kCString and kHandle: NULL is allowed, so the Kura type is `T | None`.
  bool nullable = false;
  // kRecord: the struct's layout.
  std::shared_ptr<CRecord> record{};
  // kOut: what C writes.
  std::shared_ptr<CType> pointee{};
  // kCallback: the function C will call.
  std::shared_ptr<CSignature> callback{};

  auto IsInteger() const -> bool {
    return kind >= kI8 && kind <= kU64;
  }

  auto IsSigned() const -> bool {
    return kind >= kI8 && kind <= kI64;
  }

  auto IsFloat() const -> bool {
    return kind == kF32 || kind == kF64;
  }

  auto IsPointer() const -> bool {
    return kind == kCString || kind == kHandle;
  }

  auto Bits() const -> unsigned {
    switch (kind) {
      case kBool:
        return 1;
      case kI8:
      case kU8:
        return 8;
      case kI16:
      case kU16:
        return 16;
      case kI32:
      case kU32:
      case kF32:
        return 32;
      case kI64:
      case kU64:
      case kF64:
        return 64;
      default:
        return 0;
    }
  }

  // The name C programmers know it by, for diagnostics.
  auto ToString() const -> std::string {
    switch (kind) {
      case kVoid:
        return "void";
      case kBool:
        return "c.bool";
      case kI8:
        return "c.schar";
      case kI16:
        return "c.short";
      case kI32:
        return "c.int";
      case kI64:
        return "c.long";
      case kU8:
        return "c.uchar";
      case kU16:
        return "c.ushort";
      case kU32:
        return "c.uint";
      case kU64:
        return "c.ulong";
      case kF32:
        return "c.float";
      case kF64:
        return "c.double";
      case kCString:
        return nullable ? "const char *" : "const char * (non-null)";
      case kHandle:
        return handle + " *";
      case kRecord:
        return "a struct";
      case kOut:
        return "a pointer C writes through";
      case kCallback:
        return "a function pointer";
    }
    return "?";
  }

  // The spelling `FromName` reads back: how a type that came from a header is
  // written in the signature of an import.
  auto ToName() const -> std::string {
    switch (kind) {
      case kVoid:
        return "None";
      case kBool:
      case kI8:
      case kI16:
      case kI32:
      case kI64:
      case kU8:
      case kU16:
      case kU32:
      case kU64:
      case kF32:
      case kF64:
        return ToString();
      case kCString:
        return "String";
      case kHandle:
        return handle == "ptr" ? "c.ptr" : "c.handle:" + handle;
      case kRecord:
      case kOut:
      case kCallback:
        return "None";
    }
    return "None";
  }

  // What the type is in Kura code.
  auto ToKuraType() const -> CompileType*;

  // The same, for the scalars and pointers: the type with no fields to look at.
  auto ToSimpleKuraType() const -> CompileType* {
    CompileType* type = nullptr;
    switch (kind) {
      case kRecord:
      case kOut:
      case kCallback:
        return CompileType::None();
      case kVoid:
        return CompileType::None();
      case kBool:
        return CompileType::Bool();
      case kF32:
      case kF64:
        return CompileType::Float();
      case kCString:
        type = CompileType::String();
        break;
      case kHandle:
        type = CompileType::Handle(handle);
        break;
      default:
        return CompileType::Int();
    }
    return nullable ? CompileType::Union({type, CompileType::None()}) : type;
  }

  static auto Make(const Kind kind) -> CType {
    CType type{};
    type.kind = kind;
    return type;
  }

  // Resolves the spelling of a C type in Kura code: the Kura types that have
  // one C counterpart (`Int` is a 64-bit integer, `Float` a double, `String` a
  // `const char *`) and the `c.` names for the rest.
  static auto FromName(const std::string& name) -> std::optional<CType> {
    static const std::unordered_map<std::string, Kind> kNames{
        {"Bool", kBool},      {"Int", kI64},        {"Float", kF64},       {"String", kCString},
        {"c.bool", kBool},    {"c.char", kI8},      {"c.schar", kI8},      {"c.uchar", kU8},
        {"c.short", kI16},    {"c.ushort", kU16},   {"c.int", kI32},       {"c.uint", kU32},
        {"c.long", kI64},     {"c.ulong", kU64},    {"c.longlong", kI64},  {"c.ulonglong", kU64},
        {"c.size_t", kU64},   {"c.ssize_t", kI64},  {"c.intptr_t", kI64},  {"c.uintptr_t", kU64},
        {"c.float", kF32},    {"c.double", kF64},
    };
    if (name.starts_with("c.handle:") && name.size() > 9) {
      CType type = Make(kHandle);
      type.handle = name.substr(9);
      return type;
    }
    if (name == "c.ptr") {
      CType type = Make(kHandle);
      type.handle = "ptr";
      return type;
    }
    if (name == "None")
      return Make(kVoid);
    const auto it = kNames.find(name);
    if (it == kNames.end())
      return std::nullopt;
    return Make(it->second);
  }
};

// One field of a struct: where it is and what it holds.
struct CField {
  std::string name{};
  CType type{};
  uint64_t offset = 0;  // in bytes
};

// A struct as the C compiler lays it out. Only plain structs are described: no
// unions, bitfields, arrays or packing.
struct CRecord {
  std::string spelling{};  // how C code names it: `Vector2`, `struct tm`
  uint64_t size = 0;
  uint64_t align = 1;
  std::vector<CField> fields{};
};

// The C symbol of an `extern fn` that exports a module function when none is
// named: its name, with `-` as `_`.
inline auto CSymbolName(const std::string& function) -> std::string {
  std::string name{};
  for (const char c : function)
    name += (c == '-' ? '_' : c);
  return name;
}

// The C view of a function: what the extern declares, or what a header says.
struct CSignature {
  std::string symbol{};
  std::vector<std::string> names{};  // of the parameters, for the header
  std::vector<CType> params{};
  CType result{};
  bool variadic = false;
  // No effects: the header says `const`/`pure`, or the program says `@pure`.
  bool pure = false;
  // Structs go through a small C function that takes everything by pointer, so
  // the C compiler does the calling convention: the header it includes, the
  // function's name, and its text. Empty when the function is called directly.
  std::string header{};
  std::string shim_symbol{};
  std::string shim_source{};

  auto HasOuts() const -> bool {
    for (const auto& param : params) {
      if (param.kind == CType::kOut)
        return true;
    }
    return false;
  }

  auto HasCallbacks() const -> bool {
    for (const auto& param : params) {
      if (param.kind == CType::kCallback)
        return true;
    }
    return false;
  }

  // What a Kura call passes: every parameter but the ones C writes through.
  auto KuraParams() const -> std::vector<CompileType*> {
    std::vector<CompileType*> result{};
    for (const auto& param : params) {
      if (param.kind != CType::kOut)
        result.push_back(param.ToKuraType());
    }
    return result;
  }

  // What a Kura call gives back: C's result, or when C writes through pointers
  // a record of the result (as `result`, unless void) and each of them (by the
  // parameter's name).
  auto KuraResult() const -> CompileType* {
    if (!HasOuts())
      return result.ToKuraType();
    std::vector<CompileType::Field> fields{};
    if (result.kind != CType::kVoid)
      fields.push_back({"result", result.ToKuraType(), false});
    for (size_t idx = 0; idx < params.size(); idx++) {
      if (params[idx].kind == CType::kOut)
        fields.push_back({OutName(idx), params[idx].ToKuraType(), false});
    }
    return CompileType::Record(std::move(fields));
  }

  auto OutName(const size_t idx) const -> std::string {
    return idx < names.size() && !names[idx].empty() ? names[idx] : "out" + std::to_string(idx);
  }

  // A callback is a function in Kura.
  auto ToKuraFunction() const -> CompileType* {
    return CompileType::Function(KuraParams(), KuraResult());
  }
};

inline auto CType::ToKuraType() const -> CompileType* {
  if (kind == kOut)
    return pointee->ToKuraType();
  if (kind == kCallback)
    return callback->ToKuraFunction();
  if (kind != kRecord)
    return ToSimpleKuraType();
  std::vector<CompileType::Field> fields{};
  for (const auto& field : record->fields)
    fields.push_back({field.name, field.type.ToKuraType(), false});
  return CompileType::Record(std::move(fields));
}

}  // namespace kura

#endif  // KURA_C_TYPE_H
