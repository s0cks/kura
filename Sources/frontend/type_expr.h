#ifndef KURA_TYPE_EXPR_H
#define KURA_TYPE_EXPR_H

#include <sstream>
#include <string>
#include <vector>

#include "common.h"

namespace kura::expr {
class Expr;
class StorePropertyExpr;

// ╭───────────────────╮
// │ Type annotations  │
// ╰───────────────────╯
// The *syntax* of a type, exactly as written (`Seq<Px>`, `Click(Position) | Tick`,
// `{ x: Number, z?: Number }`, `(Number) => Number`). Resolving names to
// kura::Type is a later pass; nothing here is checked.
#define FOR_EACH_TYPE_EXPR(V) \
  V(Named)                    \
  V(Variant)                  \
  V(Record)                   \
  V(Function)                 \
  V(Union)

#define FORWARD_DECLARE(Name) class Name##TypeExpr;
FOR_EACH_TYPE_EXPR(FORWARD_DECLARE)
#undef FORWARD_DECLARE

class TypeExpr {
 public:
  virtual ~TypeExpr() = default;
  virtual auto GetName() const -> std::string_view = 0;
  virtual auto ToString() const -> std::string = 0;

#define DEFINE_AS(Name)                          \
  virtual auto As##Name() -> Name##TypeExpr* {   \
    return nullptr;                              \
  }                                              \
  inline auto Is##Name() -> bool {               \
    return As##Name() != nullptr;                \
  }
  FOR_EACH_TYPE_EXPR(DEFINE_AS)
#undef DEFINE_AS
};

#define DECLARE_TYPE_EXPR(Name)                                \
 public:                                                       \
  auto GetName() const -> std::string_view override {          \
    return #Name;                                              \
  }                                                            \
  auto As##Name() -> Name##TypeExpr* override {                \
    return this;                                               \
  }

// `Number`, `Seq<Px>`, `None`, `Optional<Node>`
class NamedTypeExpr : public TypeExpr {
 private:
  std::string name_;
  std::vector<TypeExpr*> args_;

 public:
  NamedTypeExpr(const std::string name, const std::vector<TypeExpr*> args = {}) :
    name_(std::move(name)),
    args_(std::move(args)) {}
  ~NamedTypeExpr() override = default;

  auto GetTypeName() const -> const std::string& {
    return name_;
  }

  auto GetArgs() const -> const std::vector<TypeExpr*>& {
    return args_;
  }

  auto ToString() const -> std::string override {
    std::stringstream ss{};
    ss << name_;
    if (!args_.empty()) {
      ss << '<';
      for (size_t i = 0; i < args_.size(); i++)
        ss << (i ? ", " : "") << args_[i]->ToString();
      ss << '>';
    }
    return ss.str();
  }

  DECLARE_TYPE_EXPR(Named);
};

// `Click(Position)`. A member of a union that carries a payload. A payload-free
// variant (`Tick`) is written, and parsed, as a NamedTypeExpr: telling the two
// apart needs name resolution.
class VariantTypeExpr : public TypeExpr {
 private:
  std::string name_;
  TypeExpr* payload_;

 public:
  VariantTypeExpr(const std::string name, TypeExpr* payload) :
    name_(std::move(name)),
    payload_(payload) {}
  ~VariantTypeExpr() override = default;

  auto GetTypeName() const -> const std::string& {
    return name_;
  }

  auto GetPayload() const -> TypeExpr* {
    return payload_;
  }

  auto ToString() const -> std::string override {
    return name_ + "(" + payload_->ToString() + ")";
  }

  DECLARE_TYPE_EXPR(Variant);
};

// `{ x: Number, z?: Number }`. `z?: T` is shorthand for `z: Optional<T>`.
class RecordTypeExpr : public TypeExpr {
 public:
  struct Field {
    std::string name;
    TypeExpr* type;
    bool optional;
  };

 private:
  std::vector<Field> fields_;

 public:
  explicit RecordTypeExpr(const std::vector<Field> fields) :
    fields_(std::move(fields)) {}
  ~RecordTypeExpr() override = default;

  auto GetFields() const -> const std::vector<Field>& {
    return fields_;
  }

  auto ToString() const -> std::string override {
    std::stringstream ss{};
    ss << "{ ";
    for (size_t i = 0; i < fields_.size(); i++)
      ss << (i ? ", " : "") << fields_[i].name << (fields_[i].optional ? "?" : "") << ": " << fields_[i].type->ToString();
    ss << " }";
    return ss.str();
  }

  DECLARE_TYPE_EXPR(Record);
};

// `(Number, Number) => Number`
class FunctionTypeExpr : public TypeExpr {
 private:
  std::vector<TypeExpr*> params_;
  TypeExpr* result_;

 public:
  FunctionTypeExpr(const std::vector<TypeExpr*> params, TypeExpr* result) :
    params_(std::move(params)),
    result_(result) {}
  ~FunctionTypeExpr() override = default;

  auto GetParams() const -> const std::vector<TypeExpr*>& {
    return params_;
  }

  auto GetResult() const -> TypeExpr* {
    return result_;
  }

  auto ToString() const -> std::string override {
    std::stringstream ss{};
    ss << '(';
    for (size_t i = 0; i < params_.size(); i++)
      ss << (i ? ", " : "") << params_[i]->ToString();
    ss << ") => " << result_->ToString();
    return ss.str();
  }

  DECLARE_TYPE_EXPR(Function);
};

// `Node | None`, `Click(Position) | Tick`
class UnionTypeExpr : public TypeExpr {
 private:
  std::vector<TypeExpr*> members_;

 public:
  explicit UnionTypeExpr(const std::vector<TypeExpr*> members) :
    members_(std::move(members)) {}
  ~UnionTypeExpr() override = default;

  auto GetMembers() const -> const std::vector<TypeExpr*>& {
    return members_;
  }

  auto ToString() const -> std::string override {
    std::stringstream ss{};
    for (size_t i = 0; i < members_.size(); i++)
      ss << (i ? " | " : "") << members_[i]->ToString();
    return ss.str();
  }

  DECLARE_TYPE_EXPR(Union);
};
#undef DECLARE_TYPE_EXPR

// `type Optional<T> = Some(T) | None`
class TypeDecl;

// ╭────────────╮
// │ Directives │
// ╰────────────╯
// `@vec`, `@meta(tags: [ Button, User ])`. Hints only: the compiler may ignore
// them, so they never change program meaning. Arguments are kept as
// `name: value` (a positional argument is stored under the name `value`).
struct Directive {
  std::string name;
  std::vector<StorePropertyExpr*> args;
};
using DirectiveList = std::vector<Directive*>;

class TypeDecl {
 private:
  std::string name_;
  std::vector<std::string> params_;
  TypeExpr* definition_;
  DirectiveList directives_;

 public:
  TypeDecl(const std::string name, const std::vector<std::string> params, TypeExpr* definition,
           const DirectiveList directives = {}) :
    name_(std::move(name)),
    params_(std::move(params)),
    definition_(definition),
    directives_(std::move(directives)) {}
  ~TypeDecl() = default;

  auto GetName() const -> const std::string& {
    return name_;
  }

  auto GetParams() const -> const std::vector<std::string>& {
    return params_;
  }

  auto GetDefinition() const -> TypeExpr* {
    return definition_;
  }

  auto GetDirectives() const -> const DirectiveList& {
    return directives_;
  }

  auto ToString() const -> std::string {
    std::stringstream ss{};
    ss << "type " << name_;
    if (!params_.empty()) {
      ss << '<';
      for (size_t i = 0; i < params_.size(); i++)
        ss << (i ? ", " : "") << params_[i];
      ss << '>';
    }
    ss << " = " << definition_->ToString();
    return ss.str();
  }
};
}  // namespace kura::expr

#endif  // KURA_TYPE_EXPR_H
