#include "lsp/binder.h"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

namespace kura::lsp {
using syntax::Node;

auto GetNamespace(const DeclKind kind) -> Ns {
  switch (kind) {
    case DeclKind::kType:
    case DeclKind::kTypeParam:
    case DeclKind::kVariant:
      return Ns::kType;
    case DeclKind::kField:
      return Ns::kField;
    default:
      return Ns::kValue;
  }
}

auto ToString(const DeclKind kind) -> std::string_view {
  switch (kind) {
    case DeclKind::kFn:
      return "function";
    case DeclKind::kExtern:
      return "extern function";
    case DeclKind::kType:
      return "type";
    case DeclKind::kTypeParam:
      return "type parameter";
    case DeclKind::kVariant:
      return "variant";
    case DeclKind::kField:
      return "field";
    case DeclKind::kLet:
      return "binding";
    case DeclKind::kParam:
      return "parameter";
    case DeclKind::kLambdaParam:
      return "parameter";
    case DeclKind::kForIndex:
      return "loop index";
    case DeclKind::kForElement:
      return "loop element";
    case DeclKind::kAccumulator:
      return "accumulator";
    case DeclKind::kPatternBinding:
      return "pattern binding";
    case DeclKind::kAlias:
      return "header alias";
  }
  return "name";
}

auto GetBuiltinTypes() -> const std::vector<std::string_view>& {
  static const std::vector<std::string_view> types{
      "Bool", "None", "Int",  "Float", "Number", "String", "Color", "Rgba", "Hsla", "Hsba", "Hsva",
      "Vec2", "Vec3", "Rect", "Px",    "Pct",    "Fr",     "Node",  "Seq",  "Optional", "Size", "SystemMessage",
  };
  return types;
}

auto IsBuiltinType(const std::string_view name) -> bool {
  const auto& types = GetBuiltinTypes();
  return std::find(types.begin(), types.end(), name) != types.end();
}

// ---------------------------------------------------------------------------

class Binder {
 private:
  Bindings& out_;
  std::vector<uint32_t> stack_{};
  std::unordered_map<const Node*, uint32_t> hoisted_{};
  std::unordered_set<const Node*> variant_nodes_{};
  std::unordered_set<std::string> declared_types_{};

  struct PendingField {
    Span span{};
    std::string name{};
  };
  std::vector<PendingField> pending_fields_{};

  // Just enough type tracking to tell which record `a.b` reaches into: the
  // type as written for a parameter, an annotated `let`, or the payload of a
  // variant a pattern took apart; and the declared result type of a function.
  std::unordered_map<uint32_t, uint32_t> resolved_by_begin_{};
  std::unordered_map<uint32_t, const Node*> decl_type_{};
  std::unordered_map<std::string, uint32_t> type_decl_ids_{};
  // The type a record literal in this position is expected to have.
  const Node* expected_ = nullptr;
  const Node* expected_in_branch_ = nullptr;

  auto Current() -> uint32_t {
    return stack_.back();
  }

  void PushScope(const Span span) {
    out_.scopes_.push_back(Scope{
        .span = span,
        .parent = stack_.empty() ? -1 : static_cast<int32_t>(stack_.back()),
    });
    stack_.push_back(static_cast<uint32_t>(out_.scopes_.size() - 1));
  }

  void PopScope() {
    stack_.pop_back();
  }

  static auto IsUsable(const syntax::Name& name) -> bool {
    return !name.missing && !name.text.empty() && name.text != "_";
  }

  void AddOccurrence(Occurrence occurrence) {
    out_.occurrences_.push_back(occurrence);
  }

  auto Declare(const DeclKind kind, const syntax::Name& name, const Span span, const Node* node, const int32_t parent,
               const uint32_t visible_from, const Pun pun = Pun::kNone) -> int32_t {
    if (!IsUsable(name))
      return -1;

    const auto id = static_cast<uint32_t>(out_.decls_.size());
    out_.decls_.push_back(Decl{
        .id = id,
        .kind = kind,
        .name = name.GetValue(),
        .name_span = name.span,
        .span = span,
        .node = node,
        .parent = parent,
        .scope = Current(),
        .visible_from = visible_from,
        .raw = name.IsRaw(),
    });

    if (kind == DeclKind::kField)
      out_.fields_by_name_[out_.decls_.back().name].push_back(id);
    else
      out_.scopes_[Current()].decls.push_back(id);

    AddOccurrence(Occurrence{.span = name.span, .decl = id, .is_declaration = true, .pun = pun});
    return static_cast<int32_t>(id);
  }

  // Looks `name` up from the innermost scope outward and records the use.
  auto Resolve(const syntax::Name& name, const Ns ns, const Pun pun = Pun::kNone, const uint32_t pun_end = 0)
      -> int32_t {
    if (!IsUsable(name))
      return -1;

    const auto value = name.GetValue();
    for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
      const auto& scope = out_.scopes_[*it];
      for (auto d = scope.decls.rbegin(); d != scope.decls.rend(); ++d) {
        const auto& decl = out_.decls_[*d];
        if (GetNamespace(decl.kind) != ns || decl.name != value || decl.visible_from > name.span.begin)
          continue;

        resolved_by_begin_[name.span.begin] = decl.id;
        AddOccurrence(Occurrence{.span = name.span, .decl = decl.id, .pun = pun, .pun_end = pun_end});
        return static_cast<int32_t>(decl.id);
      }
    }

    out_.unresolved_.push_back(name.span);
    return -1;
  }

  void GuessField(const syntax::Name& name) {
    if (IsUsable(name))
      pending_fields_.push_back(PendingField{.span = name.span, .name = name.GetValue()});
  }

  // ---- which record does this reach into? --------------------------------

  enum class TypeKnowledge { kUnknown, kExternal, kUser };
  struct TypeInfo {
    TypeKnowledge knowledge = TypeKnowledge::kUnknown;
    uint32_t decl = 0;
  };

  auto TypeInfoOf(const Node* type) const -> TypeInfo {
    if (!type)
      return {};
    if (const auto* named = type->As<syntax::NamedType>()) {
      const auto it = type_decl_ids_.find(named->name.GetValue());
      if (it != type_decl_ids_.end())
        return {TypeKnowledge::kUser, it->second};
      return {TypeKnowledge::kExternal, 0};
    }
    if (type->Is<syntax::QualifiedType>())
      return {TypeKnowledge::kExternal, 0};
    return {};
  }

  // The declared type of an expression, as a type node, when it is cheap to
  // know: a name with a type, a field of a known record, a call of a function
  // that says what it returns.
  auto TypeNodeOf(const Node* expr) const -> const Node* {
    if (!expr)
      return nullptr;

    switch (expr->GetKind()) {
      case Node::kParen:
        return TypeNodeOf(expr->As<syntax::Paren>()->inner);
      case Node::kVarRef: {
        const auto it = resolved_by_begin_.find(expr->As<syntax::VarRef>()->name.span.begin);
        if (it == resolved_by_begin_.end())
          return nullptr;
        const auto type = decl_type_.find(it->second);
        return type == decl_type_.end() ? nullptr : type->second;
      }
      case Node::kMember: {
        const auto* member = expr->As<syntax::Member>();
        const auto info = TypeInfoOf(TypeNodeOf(member->object));
        if (info.knowledge != TypeKnowledge::kUser)
          return nullptr;
        const auto field = FindField(info.decl, member->member.GetValue());
        if (field < 0)
          return nullptr;
        const auto* f = out_.decls_[field].node->As<syntax::Field>();
        return f ? f->type : nullptr;
      }
      case Node::kRecordUpdate:
        return TypeNodeOf(expr->As<syntax::RecordUpdate>()->base);
      case Node::kCall: {
        const auto* callee = expr->As<syntax::Call>()->callee;
        const auto* ref = callee ? callee->As<syntax::VarRef>() : nullptr;
        if (!ref)
          return nullptr;
        const auto it = resolved_by_begin_.find(ref->name.span.begin);
        if (it == resolved_by_begin_.end())
          return nullptr;
        const auto* fn = out_.decls_[it->second].node->As<syntax::FnDecl>();
        return fn ? fn->result : nullptr;
      }
      default:
        return nullptr;
    }
  }

  auto FindField(const uint32_t type, const std::string& name) const -> int32_t {
    const auto it = out_.fields_by_name_.find(name);
    if (it == out_.fields_by_name_.end())
      return -1;
    for (const auto id : it->second) {
      if (out_.decls_[id].parent == static_cast<int32_t>(type))
        return static_cast<int32_t>(id);
    }
    return -1;
  }

  // `name` is a field of the record whose type is `type`; when the type is
  // known the use is tied to that field for certain, when it isn't, by name.
  void ResolveField(const syntax::Name& name, const Node* type, const Pun pun = Pun::kNone,
                    const uint32_t pun_end = 0) {
    if (!IsUsable(name))
      return;

    const auto info = TypeInfoOf(type);
    switch (info.knowledge) {
      case TypeKnowledge::kUser: {
        const auto field = FindField(info.decl, name.GetValue());
        if (field >= 0)
          AddOccurrence(Occurrence{
              .span = name.span, .decl = static_cast<uint32_t>(field), .pun = pun, .pun_end = pun_end});
        break;
      }
      case TypeKnowledge::kExternal:
        break;
      case TypeKnowledge::kUnknown:
        GuessField(name);
        break;
    }
  }

  // ---- first pass: the names that exist before anything is read ----------

  void DeclareFields(const Node* type, const int32_t owner) {
    if (!type)
      return;

    if (const auto* record = type->As<syntax::RecordType>()) {
      for (const auto* f : record->fields) {
        const auto* field = f->As<syntax::Field>();
        if (!field)
          continue;
        Declare(DeclKind::kField, field->name, field->span, field, owner, 0);
        DeclareFields(field->type, owner);
      }
    } else if (const auto* variant = type->As<syntax::VariantType>()) {
      DeclareFields(variant->payload, owner);
    } else if (const auto* un = type->As<syntax::UnionType>()) {
      for (const auto* member : un->members)
        DeclareFields(member, owner);
    } else if (const auto* named = type->As<syntax::NamedType>()) {
      for (const auto* arg : named->args)
        DeclareFields(arg, owner);
    }
  }

  void DeclareVariants(const syntax::TypeDecl* decl, const int32_t owner) {
    const auto declare = [&](const Node* member) {
      if (const auto* variant = member->As<syntax::VariantType>()) {
        variant_nodes_.insert(variant);
        Declare(DeclKind::kVariant, variant->name, variant->span, variant, owner, 0);
      } else if (const auto* named = member->As<syntax::NamedType>()) {
        // `Reset` in `Increment | Reset` names a variant, unless there is a
        // type called that: then it is the type.
        if (named->args.empty() && !named->name.missing && !IsBuiltinType(named->name.text) &&
            !declared_types_.contains(named->name.GetValue()) && named->name.text != decl->name.text) {
          variant_nodes_.insert(named);
          Declare(DeclKind::kVariant, named->name, named->span, named, owner, 0);
        }
      }
    };

    if (const auto* un = decl->type ? decl->type->As<syntax::UnionType>() : nullptr) {
      for (const auto* member : un->members)
        declare(member);
    } else if (decl->type && decl->type->Is<syntax::VariantType>()) {
      declare(decl->type);
    }
  }

  void Hoist(const syntax::Module* root) {
    for (const auto* item : root->items) {
      if (const auto* type = item->As<syntax::TypeDecl>(); type && !type->name.missing)
        declared_types_.insert(type->name.GetValue());
    }

    for (const auto* item : root->items) {
      if (const auto* fn = item->As<syntax::FnDecl>()) {
        const auto id = Declare(DeclKind::kFn, fn->name, fn->span, fn, -1, 0);
        if (id >= 0)
          hoisted_[fn] = static_cast<uint32_t>(id);
      } else if (const auto* ext = item->As<syntax::ExternDecl>()) {
        Declare(DeclKind::kExtern, ext->c_name, ext->span, ext, -1, 0);
      } else if (const auto* type = item->As<syntax::TypeDecl>()) {
        const auto id = Declare(DeclKind::kType, type->name, type->span, type, -1, 0);
        if (id >= 0)
          type_decl_ids_[type->name.GetValue()] = static_cast<uint32_t>(id);
        DeclareVariants(type, id);
        DeclareFields(type->type, id);
      } else if (const auto* use = item->As<syntax::UseDecl>()) {
        if (use->has_alias) {
          Declare(DeclKind::kAlias, use->alias, use->span, use, -1, 0);
        } else if (use->has_header) {
          // `use "math.h"` is called `math` unless it says otherwise.
          syntax::Name alias{};
          alias.span = use->header_span;
          alias.text = std::filesystem::path(use->header).stem().string();
          alias.missing = false;
          const auto id = Declare(DeclKind::kAlias, alias, use->span, use, -1, 0);
          if (id >= 0) {
            out_.decls_[id].implicit = true;
            // The made-up name has no place in the text of its own.
            out_.occurrences_.pop_back();
          }
        }
      } else if (const auto* binding = item->As<syntax::Binding>(); binding && !binding->pattern) {
        const auto id = Declare(DeclKind::kLet, binding->name, binding->span, binding, -1, 0);
        if (id >= 0)
          hoisted_[binding] = static_cast<uint32_t>(id);
      }
    }
  }

  // ---- patterns -----------------------------------------------------------

  void VisitPattern(const Node* pattern, const DeclKind kind, const uint32_t visible_from,
                    const Node* type_hint = nullptr) {
    if (!pattern)
      return;

    switch (pattern->GetKind()) {
      case Node::kBindingPattern: {
        const auto* p = pattern->As<syntax::BindingPattern>();
        const auto id = Declare(kind, p->name, p->span, p, -1, visible_from);
        if (id >= 0 && type_hint)
          decl_type_[static_cast<uint32_t>(id)] = type_hint;
        break;
      }
      case Node::kVariantPattern: {
        const auto* p = pattern->As<syntax::VariantPattern>();
        const auto variant = Resolve(p->name, Ns::kType);
        // `Resized(size)` binds what `Resized(Size)` carries.
        const Node* payload_type = nullptr;
        if (variant >= 0) {
          if (const auto* v = out_.decls_[variant].node ? out_.decls_[variant].node->As<syntax::VariantType>() : nullptr)
            payload_type = v->payload;
        }
        VisitPattern(p->payload, kind, visible_from, payload_type);
        break;
      }
      case Node::kRecordPattern: {
        const auto* p = pattern->As<syntax::RecordPattern>();
        int32_t record = -1;
        if (p->has_type)
          record = Resolve(p->type_name, Ns::kType);
        for (const auto* f : p->fields) {
          const auto* field = f->As<syntax::FieldPattern>();
          if (!field)
            continue;

          int32_t field_decl = -1;
          if (record >= 0 && IsUsable(field->name)) {
            field_decl = FindField(static_cast<uint32_t>(record), field->name.GetValue());
            if (field_decl >= 0 && field->pattern)
              AddOccurrence(Occurrence{.span = field->name.span, .decl = static_cast<uint32_t>(field_decl)});
          } else if (field->pattern) {
            GuessField(field->name);
          }

          const Node* field_type = nullptr;
          if (field_decl >= 0) {
            if (const auto* fd = out_.decls_[field_decl].node->As<syntax::Field>())
              field_type = fd->type;
          }

          if (field->pattern) {
            VisitPattern(field->pattern, kind, visible_from, field_type);
          } else {
            const auto id = Declare(kind, field->name, field->span, field, -1, visible_from, Pun::kPattern);
            if (id >= 0 && field_type)
              decl_type_[static_cast<uint32_t>(id)] = field_type;
            // The same text is also the field it was taken from.
            if (id >= 0 && field_decl >= 0) {
              AddOccurrence(Occurrence{
                  .span = field->name.span, .decl = static_cast<uint32_t>(field_decl), .pun = Pun::kPattern});
            }
          }
        }
        if (p->has_rest)
          Declare(kind, p->rest, p->span, p, -1, visible_from);
        break;
      }
      default:
        break;
    }
  }

  // ---- types ---------------------------------------------------------------

  void VisitType(const Node* type) {
    if (!type)
      return;

    switch (type->GetKind()) {
      case Node::kNamedType: {
        const auto* t = type->As<syntax::NamedType>();
        if (!variant_nodes_.contains(t))
          Resolve(t->name, Ns::kType);
        for (const auto* arg : t->args)
          VisitType(arg);
        break;
      }
      case Node::kQualifiedType: {
        const auto* t = type->As<syntax::QualifiedType>();
        const auto alias = Resolve(t->qualifier, Ns::kValue);
        if (alias >= 0 && IsUsable(t->member)) {
          out_.qualified_.push_back(QualifiedRef{
              .span = t->member.span, .alias = static_cast<uint32_t>(alias), .name = t->member.GetValue(), .ns = Ns::kType});
        }
        for (const auto* arg : t->args)
          VisitType(arg);
        break;
      }
      case Node::kVariantType: {
        const auto* t = type->As<syntax::VariantType>();
        if (!variant_nodes_.contains(t))
          Resolve(t->name, Ns::kType);
        VisitType(t->payload);
        break;
      }
      default:
        type->ForEachChild([&](const Node* child) { VisitType(child); });
        break;
    }
  }

  // ---- everything else -----------------------------------------------------

  void VisitAll(const std::vector<Node*>& nodes) {
    for (const auto* node : nodes)
      Visit(node);
  }

  void VisitParams(const std::vector<Node*>& params, const DeclKind kind, const int32_t parent) {
    for (const auto* p : params) {
      const auto* param = p->As<syntax::Param>();
      if (!param)
        continue;
      const auto id = Declare(kind, param->name, param->span, param, parent, out_.scopes_[Current()].span.begin);
      if (id >= 0 && param->type)
        decl_type_[static_cast<uint32_t>(id)] = param->type;
      VisitType(param->type);
    }
  }

  void VisitBinding(const syntax::Binding* b) {
    VisitAll(b->directives);
    VisitType(b->annotation);
    expected_ = b->annotation;
    Visit(b->value);
    expected_ = nullptr;

    const Node* type = b->annotation ? b->annotation : TypeNodeOf(b->value);
    if (b->pattern) {
      VisitPattern(b->pattern, DeclKind::kPatternBinding, b->span.end, type);
      return;
    }

    int32_t id = -1;
    if (const auto it = hoisted_.find(b); it != hoisted_.end())
      id = static_cast<int32_t>(it->second);
    else
      id = Declare(DeclKind::kLet, b->name, b->span, b, -1, b->span.end);
    if (id >= 0 && type)
      decl_type_[static_cast<uint32_t>(id)] = type;
  }

  void VisitProps(const std::vector<Node*>& props) {
    // The names on the left are properties of the element, which are not
    // declared anywhere in the file; only the values are read.
    for (const auto* prop : props)
      Visit(prop);
  }

  void VisitCondition(const Node* condition, const Node* then_branch, const Span span) {
    const Node* expected = expected_in_branch_;
    expected_in_branch_ = nullptr;
    PushScope(span);
    Visit(condition);
    expected_ = expected;
    Visit(then_branch);
    PopScope();
  }

  void Visit(const Node* node) {
    if (!node)
      return;

    // A record literal is checked against the type its position expects; that
    // only reaches it through the forms that pass a value along.
    const Node* expected = expected_;
    expected_ = nullptr;

    switch (node->GetKind()) {
      case Node::kFnDecl: {
        const auto* fn = node->As<syntax::FnDecl>();
        VisitAll(fn->directives);
        PushScope(fn->span);
        const auto it = hoisted_.find(fn);
        const int32_t id = it == hoisted_.end() ? -1 : static_cast<int32_t>(it->second);
        VisitParams(fn->params, DeclKind::kParam, id);
        VisitType(fn->result);
        VisitAll(fn->set_rules);
        expected_ = fn->result;
        Visit(fn->body);
        PopScope();
        break;
      }
      case Node::kExternDecl: {
        const auto* ext = node->As<syntax::ExternDecl>();
        VisitAll(ext->directives);
        PushScope(ext->span);
        VisitParams(ext->params, DeclKind::kParam, -1);
        VisitType(ext->result);
        Visit(ext->body);
        PopScope();
        break;
      }
      case Node::kTypeDecl: {
        const auto* type = node->As<syntax::TypeDecl>();
        VisitAll(type->directives);
        PushScope(type->span);
        for (const auto& param : type->type_params)
          Declare(DeclKind::kTypeParam, param, param.span, type, -1, type->span.begin);
        VisitType(type->type);
        PopScope();
        break;
      }
      case Node::kBinding:
        VisitBinding(node->As<syntax::Binding>());
        break;
      case Node::kArg: {
        const auto* arg = node->As<syntax::Arg>();
        if (arg->form == syntax::Arg::kPun)
          Resolve(arg->name, Ns::kValue, Pun::kArg, arg->span.end);
        else
          Visit(arg->value);
        break;
      }
      case Node::kRecordLit: {
        const auto* record = node->As<syntax::RecordLit>();
        for (const auto* e : record->entries) {
          const auto* entry = e->As<syntax::Arg>();
          if (!entry) {
            Visit(e);
            continue;
          }
          if (entry->form == syntax::Arg::kNamed)
            ResolveField(entry->name, expected);
          else if (entry->form == syntax::Arg::kPun)
            ResolveField(entry->name, expected, Pun::kArg, entry->span.end);
          Visit(entry);
        }
        break;
      }
      case Node::kRecordUpdate: {
        const auto* update = node->As<syntax::RecordUpdate>();
        Visit(update->base);
        for (const auto* e : update->entries) {
          const auto* entry = e->As<syntax::UpdateEntry>();
          if (!entry)
            continue;
          // Only the first name of `pos.x` is a field of the record being
          // updated; the rest belong to another type.
          if (!entry->path.empty())
            ResolveField(entry->path.front(), TypeNodeOf(update->base));
          Visit(entry->value);
        }
        break;
      }
      case Node::kVarRef:
        Resolve(node->As<syntax::VarRef>()->name, Ns::kValue);
        break;
      case Node::kVariantRef:
        Resolve(node->As<syntax::VariantRef>()->name, Ns::kType);
        break;
      case Node::kMember: {
        const auto* member = node->As<syntax::Member>();
        Visit(member->object);
        // `alias.name` reaches into a header, which is not in this file.
        const auto* object = member->object ? member->object->As<syntax::VarRef>() : nullptr;
        bool qualified = false;
        if (object && !out_.occurrences_.empty()) {
          const auto& last = out_.occurrences_.back();
          qualified = last.span == object->name.span && out_.decls_[last.decl].kind == DeclKind::kAlias;
          if (qualified && IsUsable(member->member)) {
            out_.qualified_.push_back(QualifiedRef{
                .span = member->member.span, .alias = last.decl, .name = member->member.GetValue(), .ns = Ns::kValue});
          }
        }
        if (!qualified)
          ResolveField(member->member, TypeNodeOf(member->object));
        break;
      }
      case Node::kBlock: {
        const auto* block = node->As<syntax::Block>();
        PushScope(block->span);
        VisitAll(block->statements);
        expected_ = expected;
        Visit(block->result);
        PopScope();
        break;
      }
      case Node::kIfExpr: {
        const auto* expr = node->As<syntax::IfExpr>();
        const auto end = expr->then_branch ? expr->then_branch->span.end : expr->span.end;
        expected_in_branch_ = expected;
        VisitCondition(expr->condition, expr->then_branch, Span(expr->span.begin, end));
        expected_ = expected;
        Visit(expr->else_branch);
        break;
      }
      case Node::kWhenExpr: {
        const auto* expr = node->As<syntax::WhenExpr>();
        VisitCondition(expr->condition, expr->body, expr->span);
        break;
      }
      case Node::kIsExpr: {
        const auto* expr = node->As<syntax::IsExpr>();
        Visit(expr->value);
        VisitPattern(expr->pattern, DeclKind::kPatternBinding, expr->pattern ? expr->pattern->span.end : 0);
        break;
      }
      case Node::kMatchExpr: {
        const auto* match = node->As<syntax::MatchExpr>();
        Visit(match->scrutinee);
        for (const auto* arm : match->arms) {
          expected_ = expected;
          Visit(arm);
        }
        break;
      }
      case Node::kParen:
        expected_ = expected;
        Visit(node->As<syntax::Paren>()->inner);
        break;
      case Node::kMatchArm: {
        const auto* arm = node->As<syntax::MatchArm>();
        PushScope(arm->span);
        VisitPattern(arm->pattern, DeclKind::kPatternBinding, arm->span.begin);
        Visit(arm->guard);
        expected_ = expected;
        Visit(arm->body);
        PopScope();
        break;
      }
      case Node::kForExpr: {
        const auto* expr = node->As<syntax::ForExpr>();
        Visit(expr->iterable);
        Visit(expr->initial);
        PushScope(expr->span);
        Declare(DeclKind::kForIndex, expr->first, expr->span, expr, -1, expr->span.begin);
        if (expr->second && expr->second->Is<syntax::BindingPattern>())
          VisitPattern(expr->second, DeclKind::kForElement, expr->span.begin);
        else
          VisitPattern(expr->second, DeclKind::kPatternBinding, expr->span.begin);
        if (expr->has_fold)
          Declare(DeclKind::kAccumulator, expr->accumulator, expr->span, expr, -1, expr->span.begin);
        Visit(expr->guard);
        Visit(expr->body);
        PopScope();
        break;
      }
      case Node::kLambda: {
        const auto* lambda = node->As<syntax::Lambda>();
        PushScope(lambda->span);
        for (const auto& param : lambda->params)
          Declare(DeclKind::kLambdaParam, param, param.span, lambda, -1, lambda->span.begin);
        Visit(lambda->body);
        PopScope();
        break;
      }
      case Node::kUiElement: {
        const auto* element = node->As<syntax::UiElement>();
        VisitAll(element->directives);
        VisitProps(element->props);
        PushScope(element->span);
        VisitAll(element->children);
        PopScope();
        break;
      }
      case Node::kSetRule:
        VisitProps(node->As<syntax::SetRule>()->props);
        break;
      case Node::kTestDecl: {
        const auto* test = node->As<syntax::TestDecl>();
        VisitAll(test->directives);
        PushScope(test->span);
        Visit(test->body);
        PopScope();
        break;
      }
      case Node::kUseDecl:
        break;
      default:
        node->ForEachChild([&](const Node* child) { Visit(child); });
        break;
    }
  }

 public:
  explicit Binder(Bindings& out) :
    out_(out) {}

  void Run(const syntax::Module* root) {
    PushScope(Span(0, UINT32_MAX));
    Hoist(root);
    for (const auto* item : root->items)
      Visit(item);
    PopScope();

    out_.decl_types_ = std::move(decl_type_);

    // A field is only found by its name, so it is only found when the name
    // belongs to a single record.
    for (const auto& pending : pending_fields_) {
      const auto it = out_.fields_by_name_.find(pending.name);
      if (it == out_.fields_by_name_.end() || it->second.size() != 1)
        continue;
      out_.occurrences_.push_back(Occurrence{.span = pending.span, .decl = it->second.front(), .is_guess = true});
    }

    std::stable_sort(out_.occurrences_.begin(), out_.occurrences_.end(), [](const Occurrence& a, const Occurrence& b) {
      if (a.span.begin != b.span.begin)
        return a.span.begin < b.span.begin;
      return a.is_guess < b.is_guess;
    });
  }
};

auto Bindings::Build(const syntax::Module* root) -> std::unique_ptr<Bindings> {
  auto bindings = std::make_unique<Bindings>();
  if (!root)
    return bindings;

  Binder binder(*bindings);
  binder.Run(root);
  return bindings;
}

auto Bindings::FindOccurrenceAt(const uint32_t offset) const -> const Occurrence* {
  // Names don't overlap (apart from puns, which share a span), so the only
  // candidate is the last occurrence that starts at or before the offset.
  // Cursors at the end of a name count.
  auto it = std::upper_bound(occurrences_.begin(), occurrences_.end(), offset,
                             [](const uint32_t value, const Occurrence& o) { return value < o.span.begin; });
  if (it == occurrences_.begin())
    return nullptr;

  --it;
  if (!it->span.Touches(offset))
    return nullptr;

  // Prefer the one that is not a guess when several share the span.
  auto best = it;
  while (best != occurrences_.begin() && (best - 1)->span == it->span)
    --best;
  return &*best;
}

auto Bindings::GetOccurrences(const uint32_t decl, const bool include_guesses) const -> std::vector<const Occurrence*> {
  std::vector<const Occurrence*> result{};
  for (const auto& o : occurrences_) {
    if (o.decl != decl || (o.is_guess && !include_guesses))
      continue;
    result.push_back(&o);
  }
  std::stable_sort(result.begin(), result.end(),
                   [](const Occurrence* a, const Occurrence* b) { return a->is_declaration > b->is_declaration; });
  return result;
}

auto Bindings::FindScopeAt(const uint32_t offset) const -> int32_t {
  int32_t best = -1;
  uint32_t best_size = UINT32_MAX;
  for (size_t i = 0; i < scopes_.size(); i++) {
    const auto& span = scopes_[i].span;
    if (!span.Touches(offset))
      continue;
    const auto size = span.end - span.begin;
    if (size <= best_size) {
      best = static_cast<int32_t>(i);
      best_size = size;
    }
  }
  return best;
}

auto Bindings::GetVisibleAt(const uint32_t offset, const Ns ns) const -> std::vector<const Decl*> {
  std::vector<const Decl*> result{};
  std::unordered_set<std::string> seen{};
  for (auto scope = FindScopeAt(offset); scope >= 0; scope = scopes_[scope].parent) {
    const auto& decls = scopes_[scope].decls;
    for (auto it = decls.rbegin(); it != decls.rend(); ++it) {
      const auto& decl = decls_[*it];
      if (GetNamespace(decl.kind) != ns || decl.visible_from > offset)
        continue;
      if (seen.insert(decl.name).second)
        result.push_back(&decl);
    }
  }
  return result;
}

auto Bindings::Lookup(const std::string_view name, const Ns ns, const uint32_t offset) const -> const Decl* {
  for (const auto* decl : GetVisibleAt(offset, ns)) {
    if (decl->name == name)
      return decl;
  }
  return nullptr;
}

auto Bindings::GetChildren(const uint32_t decl) const -> std::vector<const Decl*> {
  std::vector<const Decl*> result{};
  for (const auto& d : decls_) {
    if (d.parent == static_cast<int32_t>(decl) && (d.kind == DeclKind::kVariant || d.kind == DeclKind::kField))
      result.push_back(&d);
  }
  return result;
}

auto Bindings::GetFieldsNamed(const std::string_view name) const -> const std::vector<uint32_t>* {
  const auto it = fields_by_name_.find(std::string(name));
  return it == fields_by_name_.end() ? nullptr : &it->second;
}
}  // namespace kura::lsp
