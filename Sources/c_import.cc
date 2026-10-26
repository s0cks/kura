#include "c_import.h"

#include <clang/AST/ASTContext.h>
#include <clang/AST/Attr.h>
#include <clang/AST/Decl.h>
#include <clang/AST/NestedNameSpecifierBase.h>
#include <clang/AST/RecordLayout.h>
#include <clang/AST/Type.h>
#include <clang/AST/TypeBase.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Lex/MacroInfo.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Tooling/Tooling.h>
#include <cstdlib>
#include <optional>

namespace kura {
auto CImportConfig::Get() -> CImportConfig& {
  static CImportConfig config{};
  return config;
}

namespace {
#ifndef KURA_CLANG_RESOURCE_DIR
#define KURA_CLANG_RESOURCE_DIR ""
#endif

auto ResourceDir() -> std::string {
  if (const auto env = std::getenv("KURA_CLANG_RESOURCE_DIR"); env && *env)
    return env;
  return KURA_CLANG_RESOURCE_DIR;
}

// What a clang type is to C interop: the CType it converts to, or why it can't
// be passed to C yet.
struct Converted {
  std::optional<CType> type{};
  std::string problem{};
};

class Importer {
 private:
  clang::ASTContext& ctx_;

  static auto Unsupported(const std::string& why) -> Converted {
    return {std::nullopt, why};
  }

  static auto Of(const CType::Kind kind) -> Converted {
    return {CType::Make(kind), {}};
  }

  static auto HandleNamed(const std::string& name) -> Converted {
    auto type = CType::Make(CType::kHandle);
    type.handle = name;
    type.nullable = true;
    return {type, {}};
  }

  // `FILE *`, `struct tm *`, `FooRef`: the name the program uses for what it
  // points at. Handles are told apart by it.
  auto PointeeName(const clang::QualType written) const -> std::string {
    const auto canonical = written.getCanonicalType();
    // a typedef of the pointer itself (`typedef struct Foo* FooRef`)
    if (const auto typedef_type = written->getAs<clang::TypedefType>()) {
      if (canonical->isPointerType())
        return typedef_type->getDecl()->getName().str();
    }
    const auto pointee = written->isPointerType() ? written->getPointeeType() : written->getPointeeType();
    if (const auto typedef_type = pointee->getAs<clang::TypedefType>())
      return typedef_type->getDecl()->getName().str();
    if (const auto record = pointee->getAs<clang::RecordType>())
      return record->getDecl()->getName().str();
    return "ptr";
  }

 public:
  explicit Importer(clang::ASTContext& ctx) :
    ctx_(ctx) {}

  auto Convert(const clang::QualType written, const bool is_result) const -> Converted {
    const auto type = written.getCanonicalType();
    if (type->isVoidType())
      return is_result ? Of(CType::kVoid) : Unsupported("a void parameter");
    if (type->isBooleanType())
      return Of(CType::kBool);
    if (const auto enumeral = type->getAs<clang::EnumType>())
      return Convert(enumeral->getDecl()->getIntegerType(), is_result);
    if (type->isIntegerType()) {
      const auto width = ctx_.getIntWidth(type);
      const auto is_signed = type->isSignedIntegerType();
      switch (width) {
        case 8:
          return Of(is_signed ? CType::kI8 : CType::kU8);
        case 16:
          return Of(is_signed ? CType::kI16 : CType::kU16);
        case 32:
          return Of(is_signed ? CType::kI32 : CType::kU32);
        case 64:
          return Of(is_signed ? CType::kI64 : CType::kU64);
        default:
          return Unsupported("a " + std::to_string(width) + "-bit integer (" + written.getAsString() + ")");
      }
    }
    if (type->isRealFloatingType()) {
      switch (ctx_.getTypeSize(type)) {
        case 32:
          return Of(CType::kF32);
        case 64:
          return Of(CType::kF64);
        default:
          return Unsupported("`" + written.getAsString() + "`, which Kura has no equivalent for");
      }
    }
    if (type->isPointerType()) {
      const auto pointee = type->getPointeeType();
      if (pointee->isCharType()) {
        if (!is_result && !pointee.isConstQualified())
          return Unsupported("a mutable `" + written.getAsString() + "` (a buffer C writes into)");
        auto string = CType::Make(CType::kCString);
        string.nullable = true;
        return {string, {}};
      }
      if (pointee->isVoidType())
        return HandleNamed("ptr");
      if (const auto proto = pointee->getAs<clang::FunctionProtoType>()) {
        if (proto->isVariadic())
          return Unsupported("a variadic function pointer (`" + written.getAsString() + "`)");
        auto callback = std::make_shared<CSignature>();
        for (const auto param : proto->param_types()) {
          const auto converted = Convert(param, false);
          if (!converted.type || converted.type->kind == CType::kRecord || converted.type->kind == CType::kCallback)
            return Unsupported("a function pointer (`" + written.getAsString() + "`) with a parameter that is " +
                               (converted.type ? "a struct or a callback" : converted.problem));
          callback->params.push_back(*converted.type);
        }
        const auto result = Convert(proto->getReturnType(), true);
        if (!result.type || result.type->kind == CType::kRecord || result.type->kind == CType::kCallback)
          return Unsupported("a function pointer (`" + written.getAsString() + "`) whose result is " +
                             (result.type ? "a struct or a callback" : result.problem));
        callback->result = *result.type;
        auto type = CType::Make(CType::kCallback);
        type.callback = callback;
        return {type, {}};
      }
      if (pointee->isFunctionType())
        return Unsupported("a function pointer (`" + written.getAsString() + "`)");
      if (pointee->isRecordType())
        return HandleNamed(PointeeName(written));
      return Unsupported("a pointer to `" + pointee.getAsString() + "`");
    }
    if (type->isRecordType())
      return ConvertRecord(written);
    return Unsupported("`" + written.getAsString() + "`");
  }

  // A struct passed or returned by value: its fields, where the C compiler put
  // them.
  auto ConvertRecord(const clang::QualType written) const -> Converted {
    const auto name = written.getUnqualifiedType().getAsString(ctx_.getPrintingPolicy());
    const auto declaration = written.getCanonicalType()->getAs<clang::RecordType>()->getDecl();
    const auto definition = declaration->getDefinition();
    if (!definition)
      return Unsupported("the incomplete struct `" + name + "` passed by value");
    if (definition->isUnion())
      return Unsupported("the union `" + name + "`");
    if (definition->hasFlexibleArrayMember() || definition->hasAttr<clang::PackedAttr>())
      return Unsupported("the struct `" + name + "`, which is packed or ends in a flexible array");
    const auto& layout = ctx_.getASTRecordLayout(definition);
    const auto record = std::make_shared<CRecord>();
    record->spelling = name;
    record->size = static_cast<uint64_t>(layout.getSize().getQuantity());
    record->align = static_cast<uint64_t>(layout.getAlignment().getQuantity());
    unsigned index = 0;
    for (const auto field : definition->fields()) {
      const auto field_name = field->getName().str();
      if (field->isBitField() || field_name.empty())
        return Unsupported("the struct `" + name + "`, which has a bitfield or an unnamed member");
      if (field->getType()->isArrayType())
        return Unsupported("the struct `" + name + "`, whose field `" + field_name + "` is an array");
      const auto converted = Convert(field->getType(), true);
      if (!converted.type)
        return Unsupported("the struct `" + name + "`, whose field `" + field_name + "` is " + converted.problem);
      record->fields.push_back({field_name, *converted.type, layout.getFieldOffset(index) / 8});
      index++;
    }
    auto type = CType::Make(CType::kRecord);
    type.record = record;
    return {type, {}};
  }
};

auto Spell(const clang::Token& token, clang::Preprocessor& pp) -> std::string {
  return pp.getSpelling(token);
}

// `0x10`, `42UL`, `1.5f`, `3e8`
auto ParseNumber(std::string text, CHeader::Constant* out) -> bool {
  const auto hex = text.starts_with("0x") || text.starts_with("0X");
  while (!text.empty() && (text.back() == 'u' || text.back() == 'U' || text.back() == 'l' || text.back() == 'L' ||
                           (!hex && (text.back() == 'f' || text.back() == 'F'))))
    text.pop_back();
  if (text.empty())
    return false;
  const auto is_float = !hex && (text.find('.') != std::string::npos || text.find('e') != std::string::npos ||
                                 text.find('E') != std::string::npos);
  char* end = nullptr;
  if (is_float) {
    out->kind = CHeader::Constant::kFloat;
    out->real = std::strtod(text.c_str(), &end);
  } else {
    out->kind = CHeader::Constant::kInt;
    out->integer = static_cast<int64_t>(std::strtoull(text.c_str(), &end, 0));
  }
  return end && *end == '\0';
}

auto Unescape(const std::string& spelling) -> std::optional<std::string> {
  if (spelling.size() < 2 || spelling.front() != '"' || spelling.back() != '"')
    return std::nullopt;
  std::string text{};
  for (size_t idx = 1; idx + 1 < spelling.size(); idx++) {
    const auto c = spelling[idx];
    if (c != '\\') {
      text += c;
      continue;
    }
    if (++idx + 1 >= spelling.size() + 0)
      return std::nullopt;
    switch (spelling[idx]) {
      case 'n':
        text += '\n';
        break;
      case 't':
        text += '\t';
        break;
      case '\\':
      case '"':
      case '\'':
        text += spelling[idx];
        break;
      default:
        return std::nullopt;
    }
  }
  return text;
}
}  // namespace

auto CHeader::Load(const std::string& header, std::string* error) -> std::shared_ptr<CHeader> {
  const auto& config = CImportConfig::Get();
  std::vector<std::string> args{"-x", "c", "-std=gnu17", "-w"};
  if (const auto resource = ResourceDir(); !resource.empty())
    args.push_back("-resource-dir=" + resource);
  for (const auto& dir : config.include_dirs)
    args.push_back("-I" + dir);
  for (const auto& define : config.defines)
    args.push_back("-D" + define);

  auto ast = clang::tooling::buildASTFromCodeWithArgs("#include \"" + header + "\"\n", args, "kura-import.c");
  if (!ast) {
    if (error)
      *error = "clang could not read '" + header + "'";
    return nullptr;
  }
  if (ast->getDiagnostics().hasErrorOccurred()) {
    std::string first = "clang rejected '" + header + "'";
    for (auto it = ast->stored_diag_begin(); it != ast->stored_diag_end(); ++it) {
      if (it->getLevel() >= clang::DiagnosticsEngine::Error) {
        first = it->getMessage().str();
        break;
      }
    }
    if (error)
      *error = first;
    return nullptr;
  }

  const std::shared_ptr<CHeader> result(new CHeader(header));
  auto& ctx = ast->getASTContext();
  const Importer importer(ctx);

  for (const auto decl : ctx.getTranslationUnitDecl()->decls()) {
    if (const auto fn = llvm::dyn_cast<clang::FunctionDecl>(decl)) {
      const std::string name = fn->getName().str();
      if (name.empty() || result->functions_.contains(name))
        continue;
      Function function{};
      function.signature.symbol = name;
      function.signature.variadic = fn->getType()->getAs<clang::FunctionProtoType>() &&
                                    fn->getType()->getAs<clang::FunctionProtoType>()->isVariadic();
      function.signature.pure = fn->hasAttr<clang::ConstAttr>() || fn->hasAttr<clang::PureAttr>();
      function.signature.header = header;
      const auto proto = fn->getType()->getAs<clang::FunctionProtoType>();
      // a static or inline-only function has no symbol to link against
      bool shim = fn->getStorageClass() == clang::SC_Static || (fn->isInlined() && !fn->isExternallyVisible());
      if (!proto) {
        function.problem = "it has no prototype";
      } else if (shim && function.signature.variadic) {
        function.problem = "it is a static variadic function, which has no symbol to link against";
      } else {
        for (const auto param : fn->parameters()) {
          const auto converted = importer.Convert(param->getType(), false);
          if (!converted.type) {
            function.problem =
                "parameter " + std::to_string(function.signature.params.size() + 1) + " is " + converted.problem;
            break;
          }
          shim = shim || converted.type->kind == CType::kRecord;
          function.signature.names.push_back(param->getName().str());
          function.signature.params.push_back(*converted.type);
        }
        if (function.problem.empty()) {
          const auto converted = importer.Convert(fn->getReturnType(), true);
          if (!converted.type) {
            function.problem = "its result is " + converted.problem;
          } else {
            function.signature.result = *converted.type;
            shim = shim || converted.type->kind == CType::kRecord;
          }
        }
        if (function.problem.empty() && shim && function.signature.variadic)
          function.problem = "it is variadic and passes a struct";
        if (function.problem.empty() && shim && function.signature.HasCallbacks())
          function.problem = "it takes a callback and also a struct or has no symbol of its own";
        if (function.problem.empty() && shim) {
          // void __kura_shim_f(T0* a0, T1* a1, R* r) { *r = f(*a0, *a1); }
          const auto& policy = ctx.getPrintingPolicy();
          const auto symbol = "__kura_shim_" + name;
          std::string text = "void " + symbol + "(";
          std::string call = name + "(";
          size_t idx = 0;
          for (const auto param : fn->parameters()) {
            const auto spelled = param->getType().getUnqualifiedType().getAsString(policy);
            text += spelled + " *a" + std::to_string(idx) + ", ";
            call += (idx ? ", *a" : "*a") + std::to_string(idx);
            idx++;
          }
          call += ")";
          const bool has_result = function.signature.result.kind != CType::kVoid;
          if (has_result)
            text += fn->getReturnType().getUnqualifiedType().getAsString(policy) + " *r, ";
          text.resize(text.size() - (text.back() == ' ' ? 2 : 0));
          if (text.back() == '(')
            text += "void";
          text += ") { " + std::string(has_result ? "*r = " : "") + call + "; }\n";
          function.signature.shim_symbol = symbol;
          function.signature.shim_source = text;
        }
      }
      result->functions_.emplace(name, std::move(function));
    } else if (const auto enumeral = llvm::dyn_cast<clang::EnumDecl>(decl)) {
      for (const auto constant : enumeral->enumerators()) {
        Constant value{};
        value.kind = Constant::kInt;
        value.integer = constant->getInitVal().getSExtValue();
        result->constants_.emplace(constant->getName().str(), value);
      }
      if (!enumeral->getName().empty()) {
        Type type{};
        type.type = CType::Make(CType::kI32);
        result->types_.emplace(enumeral->getName().str(), type);
      }
    } else if (const auto alias = llvm::dyn_cast<clang::TypedefNameDecl>(decl)) {
      const auto underlying = alias->getUnderlyingType();
      Type type{};
      if (underlying.getCanonicalType()->isRecordType()) {
        const auto record = underlying.getCanonicalType()->getAs<clang::RecordType>()->getDecl();
        if (record->getDefinition()) {
          const auto converted = importer.Convert(
              ctx.getTypedefType(clang::ElaboratedTypeKeyword::None, clang::NestedNameSpecifier{}, alias), true);
          if (converted.type)
            type.type = *converted.type;
          else
            type.problem = converted.problem;
        } else {
          type.type = CType::Make(CType::kHandle);
          type.type.handle = alias->getName().str();
        }
      } else {
        const auto converted = importer.Convert(underlying, true);
        if (converted.type) {
          type.type = *converted.type;
          // a name for a pointer is the name of the handle
          if (type.type.kind == CType::kHandle && type.type.handle != "ptr")
            type.type.handle = alias->getName().str();
        } else {
          type.problem = converted.problem;
        }
      }
      result->types_.emplace(alias->getName().str(), std::move(type));
    } else if (const auto record = llvm::dyn_cast<clang::RecordDecl>(decl)) {
      if (record->getName().empty() || result->types_.contains(record->getName().str()))
        continue;
      Type type{};
      if (record->getDefinition()) {
        const auto converted =
            importer.Convert(ctx.getTypeDeclType(clang::ElaboratedTypeKeyword::None, std::nullopt, record), true);
        if (converted.type)
          type.type = *converted.type;
        else
          type.problem = converted.problem;
      } else {
        type.type = CType::Make(CType::kHandle);
        type.type.handle = record->getName().str();
      }
      result->types_.emplace(record->getName().str(), std::move(type));
    }
  }

  // Object-like macros that are a single number or string: `M_PI`, `EOF`.
  auto& pp = ast->getPreprocessor();
  auto& sources = ast->getSourceManager();
  std::vector<std::pair<std::string, std::string>> aliases{};
  for (const auto& it : pp.macros()) {
    const auto name = it.first->getName().str();
    const auto info = pp.getMacroInfo(it.first);
    if (!info || !info->isObjectLike() || info->getNumTokens() == 0)
      continue;
    const auto where = sources.getPresumedLoc(info->getDefinitionLoc());
    if (!where.isValid() || where.getFilename()[0] == '<')
      continue;
    std::vector<clang::Token> tokens(info->tokens_begin(), info->tokens_end());
    // `(-1)` and `-1`
    if (tokens.size() >= 3 && tokens.front().is(clang::tok::l_paren) && tokens.back().is(clang::tok::r_paren)) {
      tokens.erase(tokens.begin());
      tokens.pop_back();
    }
    bool negate = false;
    if (tokens.size() == 2 && tokens.front().is(clang::tok::minus)) {
      negate = true;
      tokens.erase(tokens.begin());
    }
    if (tokens.size() != 1)
      continue;
    Constant value{};
    if (tokens[0].is(clang::tok::numeric_constant)) {
      if (!ParseNumber(Spell(tokens[0], pp), &value))
        continue;
      if (negate) {
        value.integer = -value.integer;
        value.real = -value.real;
      }
      result->constants_.emplace(name, value);
    } else if (tokens[0].is(clang::tok::string_literal) && !negate) {
      const auto text = Unescape(Spell(tokens[0], pp));
      if (!text)
        continue;
      value.kind = Constant::kString;
      value.text = *text;
      result->constants_.emplace(name, value);
    } else if (tokens[0].is(clang::tok::identifier) && !negate) {
      aliases.emplace_back(name, Spell(tokens[0], pp));
    }
  }
  // `#define A B`, where B is another macro or an enumerator
  for (const auto& [name, target] : aliases) {
    if (const auto found = result->constants_.find(target);
        found != result->constants_.end() && !result->constants_.contains(name))
      result->constants_.emplace(name, found->second);
  }
  return result;
}
}  // namespace kura
