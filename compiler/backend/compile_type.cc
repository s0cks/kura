#include "backend/compile_type.h"

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace kura {
static auto Table() -> std::unordered_map<std::string, CompileType*>& {
  static std::unordered_map<std::string, CompileType*> table{};
  return table;
}

auto CompileType::Intern(CompileType* proto) -> CompileType* {
  std::stringstream ss{};
  switch (proto->kind_) {
    case kAny:
      ss << "Any";
      break;
    case kNever:
      ss << "Never";
      break;
    case kNone:
      ss << "None";
      break;
    case kBool:
      ss << "Bool";
      break;
    case kInt:
      ss << "Int";
      break;
    case kFloat:
      ss << "Float";
      break;
    case kNumber:
      ss << "Number";
      break;
    case kString:
      ss << "String";
      break;
    case kColor:
      ss << "Color";
      break;
    case kPx:
      ss << "Px";
      break;
    case kPct:
      ss << "Pct";
      break;
    case kFr:
      ss << "Fr";
      break;
    case kNode:
      ss << "Node";
      break;
    case kSeq:
      ss << "Seq<" << proto->element_->ToString() << ">";
      break;
    case kRecord: {
      ss << "{ ";
      for (size_t idx = 0; idx < proto->fields_.size(); idx++) {
        const auto& field = proto->fields_[idx];
        ss << (idx ? ", " : "") << field.name << (field.optional ? "?" : "") << ": " << field.type->ToString();
      }
      ss << " }";
      break;
    }
    case kVariant:
      ss << proto->name_;
      if (proto->payload_)
        ss << "(" << proto->payload_->ToString() << ")";
      break;
    case kFunction: {
      ss << "(";
      for (size_t idx = 0; idx < proto->params_.size(); idx++)
        ss << (idx ? ", " : "") << proto->params_[idx]->ToString();
      ss << ") => " << proto->result_->ToString();
      break;
    }
    case kUnion: {
      for (size_t idx = 0; idx < proto->members_.size(); idx++)
        ss << (idx ? " | " : "") << proto->members_[idx]->ToString();
      break;
    }
    case kHandle:
      ss << "Handle<" << proto->name_ << ">";
      break;
  }
  proto->repr_ = ss.str();
  // a function inside a union needs parentheses to read unambiguously
  auto& table = Table();
  const auto it = table.find(proto->repr_);
  if (it != table.end()) {
    delete proto;
    return it->second;
  }
  table[proto->repr_] = proto;
  return proto;
}

#define DEFINE_SIMPLE(Name)                    \
  auto CompileType::Name() -> CompileType* {   \
    static CompileType* type = nullptr;        \
    if (!type)                                 \
      type = Intern(new CompileType(k##Name)); \
    return type;                               \
  }

DEFINE_SIMPLE(Any)
DEFINE_SIMPLE(Never)
DEFINE_SIMPLE(None)
DEFINE_SIMPLE(Bool)
DEFINE_SIMPLE(Int)
DEFINE_SIMPLE(Float)
DEFINE_SIMPLE(Number)
DEFINE_SIMPLE(String)
DEFINE_SIMPLE(Color)
DEFINE_SIMPLE(Px)
DEFINE_SIMPLE(Pct)
DEFINE_SIMPLE(Fr)
DEFINE_SIMPLE(Node)
#undef DEFINE_SIMPLE

auto CompileType::Seq(CompileType* element) -> CompileType* {
  const auto type = new CompileType(kSeq);
  type->element_ = element;
  return Intern(type);
}

auto CompileType::Handle(const std::string& name) -> CompileType* {
  const auto type = new CompileType(kHandle);
  type->name_ = name;
  return Intern(type);
}

auto CompileType::Record(std::vector<Field> fields) -> CompileType* {
  std::sort(fields.begin(), fields.end(), [](const Field& a, const Field& b) {
    return a.name < b.name;
  });
  const auto type = new CompileType(kRecord);
  type->fields_ = std::move(fields);
  return Intern(type);
}

auto CompileType::Variant(const std::string& name, CompileType* payload) -> CompileType* {
  const auto type = new CompileType(kVariant);
  type->name_ = name;
  type->payload_ = payload;
  return Intern(type);
}

auto CompileType::Function(std::vector<CompileType*> params, CompileType* result) -> CompileType* {
  const auto type = new CompileType(kFunction);
  type->params_ = std::move(params);
  type->result_ = result;
  return Intern(type);
}

auto CompileType::Union(std::vector<CompileType*> members) -> CompileType* {
  // flatten, drop Never, and let Any absorb everything
  std::vector<CompileType*> flat{};
  for (const auto member : members) {
    if (member->IsAny())
      return Any();
    if (member->IsNever())
      continue;
    if (member->IsUnion()) {
      for (const auto inner : member->members_)
        flat.push_back(inner);
    } else {
      flat.push_back(member);
    }
  }
  std::sort(flat.begin(), flat.end(), [](CompileType* a, CompileType* b) {
    return a->ToString() < b->ToString();
  });
  flat.erase(std::unique(flat.begin(), flat.end()), flat.end());

  // Int | Float is Number, and Number already includes both
  const auto has = [&](const Kind kind) {
    return std::any_of(flat.begin(), flat.end(), [&](CompileType* t) {
      return t->kind_ == kind;
    });
  };
  if (has(kNumber) || (has(kInt) && has(kFloat))) {
    std::erase_if(flat, [](CompileType* t) {
      return t->kind_ == kInt || t->kind_ == kFloat || t->kind_ == kNumber;
    });
    flat.push_back(Number());
    std::sort(flat.begin(), flat.end(), [](CompileType* a, CompileType* b) {
      return a->ToString() < b->ToString();
    });
  }

  if (flat.empty())
    return Never();
  if (flat.size() == 1)
    return flat.front();
  const auto type = new CompileType(kUnion);
  type->members_ = std::move(flat);
  return Intern(type);
}

auto CompileType::FindField(const std::string& name) const -> const Field* {
  for (const auto& field : fields_) {
    if (field.name == name)
      return &field;
  }
  return nullptr;
}

auto CompileType::GetAlternatives() const -> std::vector<CompileType*> {
  if (IsUnion())
    return members_;
  return {const_cast<CompileType*>(this)};
}

auto CompileType::IsNullable() const -> bool {
  for (const auto alt : GetAlternatives()) {
    if (alt->IsNone())
      return true;
  }
  return false;
}

auto CompileType::WithoutNone() const -> CompileType* {
  std::vector<CompileType*> rest{};
  for (const auto alt : GetAlternatives()) {
    if (!alt->IsNone())
      rest.push_back(alt);
  }
  return Union(rest);
}

auto CompileType::FindVariant(const std::string& name) const -> CompileType* {
  for (const auto alt : GetAlternatives()) {
    if (alt->IsVariant() && alt->name_ == name)
      return alt;
  }
  return nullptr;
}

auto CompileType::IsAssignable(CompileType* from, CompileType* to) -> bool {
  if (from == to || from->IsAny() || to->IsAny() || from->IsNever())
    return true;
  if (from->IsUnion()) {
    return std::all_of(from->members_.begin(), from->members_.end(), [&](CompileType* member) {
      return IsAssignable(member, to);
    });
  }
  if (to->IsUnion()) {
    return std::any_of(to->members_.begin(), to->members_.end(), [&](CompileType* member) {
      return IsAssignable(from, member);
    });
  }
  switch (to->kind_) {
    case kNumber:
      return from->IsNumeric();
    case kSeq:
      return from->IsSeq() && IsAssignable(from->element_, to->element_);
    case kRecord: {
      if (!from->IsRecord())
        return false;
      // records are exact: every field the source has must be in the target
      for (const auto& field : from->fields_) {
        const auto target = to->FindField(field.name);
        if (!target || !IsAssignable(field.type, target->type))
          return false;
        if (field.optional && !target->optional)
          return false;
      }
      for (const auto& field : to->fields_) {
        if (!field.optional && !from->FindField(field.name))
          return false;
      }
      return true;
    }
    case kVariant: {
      if (!from->IsVariant() || from->name_ != to->name_)
        return false;
      if (!from->payload_ || !to->payload_)
        return from->payload_ == to->payload_;
      return IsAssignable(from->payload_, to->payload_);
    }
    case kFunction: {
      if (!from->IsFunction() || from->params_.size() != to->params_.size())
        return false;
      for (size_t idx = 0; idx < from->params_.size(); idx++) {
        // parameters are contravariant
        if (!IsAssignable(to->params_[idx], from->params_[idx]))
          return false;
      }
      return IsAssignable(from->result_, to->result_);
    }
    default:
      return false;
  }
}

auto CompileType::Join(CompileType* a, CompileType* b) -> CompileType* {
  if (a == b)
    return a;
  if (a->IsNever())
    return b;
  if (b->IsNever())
    return a;
  if (a->IsAny() || b->IsAny())
    return Any();
  if (a->IsNumeric() && b->IsNumeric())
    return Number();
  if (a->IsSeq() && b->IsSeq())
    return Seq(Join(a->element_, b->element_));
  if (a->IsRecord() && b->IsRecord() && a->fields_.size() == b->fields_.size()) {
    std::vector<Field> fields{};
    for (size_t idx = 0; idx < a->fields_.size(); idx++) {
      const auto& fa = a->fields_[idx];
      const auto& fb = b->fields_[idx];
      if (fa.name != fb.name)
        return Union({a, b});
      fields.push_back({fa.name, Join(fa.type, fb.type), fa.optional || fb.optional});
    }
    return Record(fields);
  }
  return Union({a, b});
}
}  // namespace kura
