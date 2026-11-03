#include "frontend/syntax/parser.h"

#include <algorithm>
#include <cctype>

#include "frontend/syntax/lexer.h"

namespace kura::syntax {
namespace {
using K = Token;

const auto kExpressionStart = K::Set({
    K::kNumber, K::kString, K::kTrue, K::kFalse, K::kNone, K::kPxLit, K::kPctLit, K::kFrLit, K::kColorLit,
    K::kIdent, K::kUpperIdent, K::kLBrack, K::kLBrace, K::kIf, K::kMatch, K::kFor, K::kHash, K::kDirective,
    K::kLParen, K::kMinus, K::kBang,
});

// Where parsing of a top level item can safely start again.
const auto kItemStart = K::Set({K::kLet, K::kFn, K::kType, K::kUse, K::kExtern, K::kDirective});

inline auto IsCloser(const Token::Kind kind) -> bool {
  return kind == K::kRParen || kind == K::kRBrace || kind == K::kRBrack;
}

inline auto IsOpener(const Token::Kind kind) -> bool {
  return kind == K::kLParen || kind == K::kLBrace || kind == K::kLBrack;
}

auto Unescape(const std::string_view raw) -> std::string {
  std::string out{};
  // raw starts with the opening quote
  for (size_t idx = 1; idx < raw.size(); idx++) {
    const auto c = raw[idx];
    if (c == '"')
      break;
    if (c != '\\' || idx + 1 >= raw.size()) {
      out.push_back(c);
      continue;
    }
    const auto next = raw[++idx];
    switch (next) {
      case 'n':
        out.push_back('\n');
        break;
      case 't':
        out.push_back('\t');
        break;
      default:
        out.push_back(next);  // `\"`, `\\`, and (already diagnosed) unknown escapes
        break;
    }
  }
  return out;
}

auto Capitalize(std::string text) -> std::string {
  if (!text.empty())
    text[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(text[0])));
  return text;
}
}  // namespace

class TreeBuilder {
 private:
  SyntaxTree& tree_;
  const SourceFile& source_;
  const ParseOptions options_;
  std::vector<Token>& tokens_;
  DiagnosticList& diagnostics_;
  size_t index_ = 0;
  uint32_t prev_end_ = 0;
  uint32_t depth_ = 0;
  int64_t last_error_index_ = -1;
  bool depth_reported_ = false;
  Diagnostic scratch_{};  // where a suppressed diagnostic goes to be ignored

  // ---- token access ---------------------------------------------------------

  auto Peek(const size_t ahead = 0) const -> const Token& {
    const auto idx = std::min(index_ + ahead, tokens_.size() - 1);
    return tokens_[idx];
  }

  auto PeekKind(const size_t ahead = 0) const -> Token::Kind {
    return Peek(ahead).kind;
  }

  auto Is(const Token::Kind kind) const -> bool {
    return Peek().kind == kind;
  }

  auto AtEof() const -> bool {
    return Is(K::kEof);
  }

  auto Next() -> Token {
    const auto token = Peek();
    if (token.kind != K::kEof) {
      index_++;
      prev_end_ = token.span.end;
    }
    return token;
  }

  auto Accept(const Token::Kind kind) -> bool {
    if (!Is(kind))
      return false;
    Next();
    return true;
  }

  auto Text(const Span& span) const -> std::string {
    return std::string(source_.GetText(span));
  }

  // The start of the next token to be consumed: where a node begins.
  auto Here() const -> uint32_t {
    return Peek().span.begin;
  }

  // From `begin` to the end of the last consumed token.
  auto SpanFrom(const uint32_t begin) const -> Span {
    return Span(begin, std::max(begin, prev_end_));
  }

  template <typename T>
  auto New(const uint32_t begin) -> T* {
    return tree_.arena_.template New<T>(SpanFrom(begin));
  }

  auto NewError(const uint32_t begin) -> Error* {
    return New<Error>(begin);
  }

  // Stands in for something that should have been here and isn't. It sits
  // right after the last consumed token, which keeps it inside its parent.
  auto NewMissing() -> Error* {
    return tree_.arena_.New<Error>(Span(prev_end_, prev_end_));
  }

  // ---- diagnostics ----------------------------------------------------------

  // How the token under the cursor reads in "found ...".
  auto Found(const Token& token) const -> std::string {
    switch (token.kind) {
      case K::kEof:
        return "the end of the file";
      case K::kInvalid:
        return "an unexpected character";
      default:
        break;
    }
    auto text = Text(token.span);
    if (text.size() > 24)
      text = text.substr(0, 21) + "...";
    if (K::IsKeyword(token.kind))
      return "the keyword `" + text + "`";
    return "`" + text + "`";
  }

  // A diagnostic at the token under the cursor. Reporting twice without
  // consuming anything in between is almost always one mistake seen twice, so
  // the second report is dropped.
  auto ReportHere(const DiagnosticCode code, std::string message, std::string label = {},
                  const bool anchor_to_previous = false) -> Diagnostic& {
    if (last_error_index_ == static_cast<int64_t>(index_))
      return scratch_ = Diagnostic{};
    // The lexer has already said what is wrong with an invalid character, and
    // with a string or backtick name it had to cut short; whatever the parser
    // trips over right there is the same problem seen again.
    if (Peek().kind == K::kInvalid || (index_ > 0 && tokens_[index_ - 1].malformed)) {
      last_error_index_ = static_cast<int64_t>(index_);
      return scratch_ = Diagnostic{};
    }
    last_error_index_ = static_cast<int64_t>(index_);
    auto span = Peek().span;
    if (AtEof() && span.begin > 0) {
      // point at the last character rather than past the end of the file
      span = Span(prev_end_ > 0 ? prev_end_ - 1 : 0, prev_end_);
    } else if (anchor_to_previous && index_ > 0 && AtLineStart() && prev_end_ > 0) {
      // Something is missing, and what comes next is on another line. The
      // mistake is at the end of the line it was missing from, not down there.
      span = Span(prev_end_ - 1, prev_end_);
    }
    return diagnostics_.Error(code, span, std::move(message), std::move(label));
  }

  auto Report(const DiagnosticCode code, const Span& span, std::string message, std::string label = {})
      -> Diagnostic& {
    return diagnostics_.Error(code, span, std::move(message), std::move(label));
  }

  void ExpectedHere(const std::string& what) {
    auto& d = ReportHere(DiagnosticCode::kUnexpectedToken, "expected " + what + ", found " + Found(Peek()),
                         "expected " + what + " here", true);
    // `a--b` and `a-` are not names; point at the likely cause
    const auto& token = Peek();
    if (token.kind == K::kMinus && index_ > 0) {
      const auto& prev = tokens_[index_ - 1];
      if ((prev.kind == K::kIdent || prev.kind == K::kUpperIdent) && prev.span.end == token.span.begin)
        d.WithHelp("a `-` is only part of a name when a letter, digit or `_` follows it; to subtract, put spaces "
                   "around the `-`");
    }
  }

  auto Expect(const Token::Kind kind) -> bool {
    if (Accept(kind))
      return true;
    ExpectedHere(K::Describe(kind));
    return false;
  }

  // Expect the closing half of a delimiter pair, pointing back at the opener
  // when it is missing.
  auto ExpectClosing(const Token::Kind kind, const Span& opener) -> bool {
    if (Accept(kind))
      return true;
    ExpectedHere(K::Describe(kind));
    if (last_error_index_ == static_cast<int64_t>(index_) && !diagnostics_.empty()) {
      auto& last = const_cast<Diagnostic&>(diagnostics_.at(diagnostics_.size() - 1));
      if (last.code == DiagnosticCode::kUnexpectedToken) {
        last.code = DiagnosticCode::kMissingClosingDelimiter;
        const auto has_label = std::any_of(last.labels.begin(), last.labels.end(),
                                           [&](const Label& label) { return label.span == opener; });
        if (has_label)
          last.labels.back().message = "this `" + Text(opener) + "` is never closed";
        else
          last.WithLabel(opener, "this `" + Text(opener) + "` is never closed");
      }
    }
    return false;
  }

  // ---- recovery -------------------------------------------------------------

  // Skips tokens until one in `stop` is found at the same nesting level as
  // where it started. A closer that has no opener in the skipped range ends the
  // skip too: it belongs to whoever is parsing the enclosing construct.
  void SkipUntil(const Token::KindSet& stop) {
    int depth = 0;
    while (!AtEof()) {
      const auto kind = PeekKind();
      if (depth == 0 && stop.test(kind))
        return;
      if (AtItemKeyword() && (depth == 0 || AtLineStart()))
        return;
      if (IsOpener(kind)) {
        depth++;
      } else if (IsCloser(kind)) {
        if (depth == 0)
          return;
        depth--;
      }
      Next();
    }
  }

  // After a broken top level item: skip to something that can start the next
  // one. A bare `name =` starts a redefinition.
  void RecoverToItem() {
    int depth = 0;
    while (!AtEof()) {
      const auto kind = PeekKind();
      if (depth == 0 && Peek().Test(kItemStart))
        return;
      if (AtItemKeyword() && AtLineStart())
        return;
      if (depth == 0 && kind == K::kIdent && PeekKind(1) == K::kAssign && AtLineStart())
        return;
      if (IsOpener(kind))
        depth++;
      else if (IsCloser(kind) && depth > 0)
        depth--;
      Next();
    }
  }

  // `fn`, `type`, `use` and `extern` only ever start a top level declaration (as
  // a member name after a `.` they are just names), so meeting one while in the
  // middle of something else means the something else was never finished.
  auto AtItemKeyword() const -> bool {
    const auto kind = PeekKind();
    if (kind != K::kFn && kind != K::kType && kind != K::kUse && kind != K::kExtern)
      return false;
    return !(index_ > 0 && tokens_[index_ - 1].kind == K::kDot);
  }

  // True when nothing but whitespace separates the cursor from the previous
  // token's line: the cursor is the first token on its line.
  auto AtLineStart() const -> bool {
    if (index_ == 0)
      return true;
    const auto gap = source_.GetText(Span(tokens_[index_ - 1].span.end, Peek().span.begin));
    return gap.find('\n') != std::string_view::npos;
  }

  auto EnterNesting() -> bool {
    if (++depth_ <= options_.max_depth)
      return true;
    if (!depth_reported_) {
      depth_reported_ = true;
      ReportHere(DiagnosticCode::kTooMuchNesting, "this is nested too deeply to parse",
                 "more than " + std::to_string(options_.max_depth) + " levels deep")
          .WithHelp("split the expression up with `let` bindings or helper functions");
    }
    // give up on the rest of the file: every caller unwinds on the end of it,
    // and everything up to there counts as consumed so spans stay nested
    index_ = tokens_.size() - 1;
    if (tokens_.size() >= 2)
      prev_end_ = tokens_[tokens_.size() - 2].span.end;
    return false;
  }

  void LeaveNesting() {
    depth_--;
  }

  struct NestingGuard {
    TreeBuilder& parser;
    bool ok;
    explicit NestingGuard(TreeBuilder& p) :
      parser(p),
      ok(p.EnterNesting()) {}
    ~NestingGuard() {
      parser.LeaveNesting();
    }
  };

  // ---- names ----------------------------------------------------------------

  static auto MakeName(const Span& span, std::string text) -> Name {
    Name name{};
    name.span = span;
    name.text = std::move(text);
    name.missing = false;
    return name;
  }

  auto MissingName() const -> Name {
    Name name{};
    name.span = Span(prev_end_, prev_end_);
    return name;
  }

  // A lowercase name that is being declared or bound. The things that look
  // like names but are not are diagnosed specifically, and then accepted as
  // the name so one mistake doesn't ripple through the rest of the file.
  auto ParseName(const std::string& what) -> Name {
    const auto token = Peek();
    switch (token.kind) {
      case K::kIdent:
        Next();
        return MakeName(token.span, Text(token.span));
      case K::kUnderscore:
        ReportHere(DiagnosticCode::kWildcardAsName, "`_` can't be used as a name", "not a name")
            .WithHelp("`_` only works as a wildcard in `match` patterns; give this a real name instead");
        Next();
        return MakeName(token.span, "_");
      case K::kUpperIdent:
        ReportHere(DiagnosticCode::kUnexpectedToken, "expected " + what + ", found " + Found(token),
                   "names start with a lowercase letter")
            .WithHelp("capitalized names are for types and variants; try `" + Text(token.span) + "` in lowercase");
        Next();
        return MakeName(token.span, Text(token.span));
      default:
        break;
    }
    if (K::IsKeyword(token.kind) || token.kind == K::kColorLit) {
      const auto text = Text(token.span);
      ReportHere(DiagnosticCode::kKeywordAsName, "`" + text + "` can't be used as a name",
                 token.kind == K::kColorLit ? "this is a color" : "this is a keyword")
          .WithHelp("pick a different name, for example `my-" + text + "`");
      Next();
      return MakeName(token.span, text);
    }
    ExpectedHere(what);
    return MissingName();
  }

  // cName: names that may come from C, so raw names and capitalized names are
  // fine.
  auto ParseCName(const std::string& what) -> Name {
    const auto token = Peek();
    if (token.kind == K::kIdent || token.kind == K::kUpperIdent || token.kind == K::kRawIdent) {
      Next();
      return MakeName(token.span, Text(token.span));
    }
    return ParseName(what);
  }

  // The name after a `.`: keywords and colors are allowed here.
  auto ParseMemberName() -> Name {
    const auto token = Peek();
    if (token.kind == K::kIdent || token.kind == K::kUpperIdent || token.kind == K::kRawIdent ||
        token.kind == K::kColorLit || K::IsKeyword(token.kind)) {
      Next();
      return MakeName(token.span, Text(token.span));
    }
    ExpectedHere("a property name");
    return MissingName();
  }

  // ---- directives -----------------------------------------------------------

  auto ParseUiArg() -> Node* {
    const auto begin = Here();
    auto* arg = tree_.arena_.New<Arg>(Span(begin, begin));
    if (Is(K::kIdent) && PeekKind(1) == K::kColon) {
      const auto token = Next();
      Next();  // ':'
      arg->form = Arg::kNamed;
      arg->name = MakeName(token.span, Text(token.span));
      arg->value = ParseExpression();
    } else if (Accept(K::kSpread)) {
      arg->form = Arg::kSpread;
      arg->value = ParseExpression();
    } else {
      arg->form = Arg::kPositional;
      arg->value = ParseExpression();
    }
    arg->span = SpanFrom(begin);
    return arg;
  }

  template <typename F>
  void ParseDelimited(const Token::Kind close, const Span& opener, F&& parse_element) {
    while (true) {
      if (Is(close)) {
        Next();
        return;
      }
      if (AtEof() || AtItemKeyword()) {
        ExpectClosing(close, opener);
        return;
      }
      const auto before = index_;
      parse_element();
      if (Accept(K::kComma))
        continue;
      if (Accept(close))
        return;
      ExpectedHere("`,` or " + K::Describe(close));
      if (last_error_index_ == static_cast<int64_t>(index_) && !diagnostics_.empty()) {
        auto& last = const_cast<Diagnostic&>(diagnostics_.at(diagnostics_.size() - 1));
        last.WithLabel(opener, "the `" + Text(opener) + "` is opened here");
      }
      SkipUntil(K::Set({K::kComma, close}));
      if (Accept(K::kComma))
        continue;
      if (Accept(close))
        return;
      if (index_ == before && !AtEof() && !IsCloser(PeekKind()) && !AtItemKeyword())
        Next();  // always make progress
      if (AtEof() || IsCloser(PeekKind()) || AtItemKeyword()) {
        // a closer of another kind, or the next declaration, ends this list unclosed
        if (Is(close))
          continue;
        ExpectClosing(close, opener);
        return;
      }
    }
  }

  auto ParseDirective() -> Node* {
    const auto token = Next();
    auto* directive = tree_.arena_.New<Directive>(token.span);
    directive->name = MakeName(Span(token.span.begin + 1, token.span.end), Text(Span(token.span.begin + 1, token.span.end)));
    if (Is(K::kLParen)) {
      const auto open = Next();
      directive->has_parens = true;
      ParseDelimited(K::kRParen, open.span, [&] { directive->args.push_back(ParseUiArg()); });
    }
    directive->span = SpanFrom(token.span.begin);
    return directive;
  }

  auto ParseDirectives() -> std::vector<Node*> {
    std::vector<Node*> directives{};
    while (Is(K::kDirective))
      directives.push_back(ParseDirective());
    return directives;
  }

  // ---- types ----------------------------------------------------------------

  auto ParseTypeArgs(std::vector<Node*>& args) -> void {
    if (!Is(K::kLt))
      return;
    const auto open = Next();
    while (true) {
      args.push_back(ParseTypeExpr());
      if (Accept(K::kComma))
        continue;
      break;
    }
    ExpectClosing(K::kGt, open.span);
  }

  auto ParseTypeMember() -> Node* {
    NestingGuard guard(*this);
    const auto begin = Here();
    if (!guard.ok)
      return NewError(begin);
    const auto token = Peek();
    switch (token.kind) {
      case K::kUpperIdent: {
        Next();
        const auto name = MakeName(token.span, Text(token.span));
        if (Is(K::kLParen)) {
          const auto open = Next();
          auto* variant = New<VariantType>(begin);
          variant->name = name;
          variant->payload = ParseTypeExpr();
          ExpectClosing(K::kRParen, open.span);
          variant->span = SpanFrom(begin);
          return variant;
        }
        if (Is(K::kDot))
          return ParseQualifiedType(begin, name);
        auto* named = New<NamedType>(begin);
        named->name = name;
        ParseTypeArgs(named->args);
        named->span = SpanFrom(begin);
        return named;
      }
      case K::kIdent: {
        if (PeekKind(1) == K::kDot) {
          Next();
          return ParseQualifiedType(begin, MakeName(token.span, Text(token.span)));
        }
        ReportHere(DiagnosticCode::kUnexpectedToken, "expected a type, found `" + Text(token.span) + "`",
                   "this is not a type")
            .WithHelp("type names are capitalized; did you mean `" + Capitalize(Text(token.span)) + "`?");
        Next();
        return NewError(begin);
      }
      case K::kNone:
        Next();
        return New<NoneType>(begin);
      case K::kLBrace:
        return ParseRecordType();
      case K::kLParen:
        return ParseFunctionType();
      default:
        ExpectedHere("a type");
        return NewMissing();
    }
  }

  auto ParseQualifiedType(const uint32_t begin, const Name& qualifier) -> Node* {
    auto* type = New<QualifiedType>(begin);
    type->qualifier = qualifier;
    Expect(K::kDot);
    type->member = ParseCName("a type name");
    ParseTypeArgs(type->args);
    type->span = SpanFrom(begin);
    return type;
  }

  auto ParseRecordType() -> Node* {
    const auto begin = Here();
    const auto open = Next();
    auto* record = New<RecordType>(begin);
    ParseDelimited(K::kRBrace, open.span, [&] {
      const auto field_begin = Here();
      auto* field = tree_.arena_.New<Field>(Span(field_begin, field_begin));
      field->name = ParseName("a field name");
      field->optional = Accept(K::kQMark);
      Expect(K::kColon);
      field->type = ParseTypeExpr();
      field->span = SpanFrom(field_begin);
      record->fields.push_back(field);
    });
    record->span = SpanFrom(begin);
    return record;
  }

  auto ParseFunctionType() -> Node* {
    const auto begin = Here();
    const auto open = Next();
    auto* fn = New<FunctionType>(begin);
    ParseDelimited(K::kRParen, open.span, [&] { fn->params.push_back(ParseTypeExpr()); });
    Expect(K::kFatArrow);
    fn->result = ParseTypeExpr();
    fn->span = SpanFrom(begin);
    return fn;
  }

  auto ParseTypeExpr() -> Node* {
    const auto begin = Here();
    auto* first = ParseTypeMember();
    if (!Is(K::kBar))
      return first;
    auto* type = New<UnionType>(begin);
    type->members.push_back(first);
    while (Accept(K::kBar))
      type->members.push_back(ParseTypeMember());
    type->span = SpanFrom(begin);
    return type;
  }

  // ---- patterns -------------------------------------------------------------

  auto MakeUnit(const Token& token) -> Node* {
    auto* unit = tree_.arena_.New<UnitLit>(token.span);
    unit->text = Text(token.span);
    switch (token.kind) {
      case K::kPxLit:
        unit->unit = UnitLit::kPx;
        break;
      case K::kPctLit:
        unit->unit = UnitLit::kPct;
        break;
      case K::kFrLit:
        unit->unit = UnitLit::kFr;
        break;
      default:
        unit->unit = UnitLit::kColor;
        break;
    }
    return unit;
  }

  auto ParseLiteralPatternBody() -> Node* {
    const auto token = Next();
    switch (token.kind) {
      case K::kNumber: {
        auto* number = tree_.arena_.New<NumberLit>(token.span);
        number->text = Text(token.span);
        return number;
      }
      case K::kString: {
        auto* string = tree_.arena_.New<StringLit>(token.span);
        string->value = Unescape(source_.GetText(token.span));
        return string;
      }
      case K::kTrue:
      case K::kFalse: {
        auto* boolean = tree_.arena_.New<BoolLit>(token.span);
        boolean->value = token.kind == K::kTrue;
        return boolean;
      }
      default:
        return MakeUnit(token);
    }
  }

  auto ParsePattern() -> Node* {
    NestingGuard guard(*this);
    const auto begin = Here();
    if (!guard.ok)
      return NewError(begin);
    const auto token = Peek();
    switch (token.kind) {
      case K::kUnderscore:
        Next();
        return New<WildcardPattern>(begin);
      case K::kIdent: {
        Next();
        auto* pattern = New<BindingPattern>(begin);
        pattern->name = MakeName(token.span, Text(token.span));
        return pattern;
      }
      case K::kNone:
        Next();
        return New<NonePattern>(begin);
      case K::kUpperIdent: {
        Next();
        auto* pattern = New<VariantPattern>(begin);
        pattern->name = MakeName(token.span, Text(token.span));
        if (Is(K::kLParen)) {
          const auto open = Next();
          pattern->payload = ParsePattern();
          ExpectClosing(K::kRParen, open.span);
        }
        pattern->span = SpanFrom(begin);
        return pattern;
      }
      case K::kMinus: {
        Next();
        auto* pattern = New<LiteralPattern>(begin);
        pattern->negated = true;
        if (Is(K::kNumber) || Is(K::kPxLit) || Is(K::kPctLit) || Is(K::kFrLit) || Is(K::kColorLit)) {
          pattern->literal = ParseLiteralPatternBody();
        } else {
          ExpectedHere("a number after `-`");
          pattern->literal = NewMissing();
        }
        pattern->span = SpanFrom(begin);
        return pattern;
      }
      case K::kNumber:
      case K::kString:
      case K::kTrue:
      case K::kFalse:
      case K::kPxLit:
      case K::kPctLit:
      case K::kFrLit:
      case K::kColorLit: {
        auto* pattern = New<LiteralPattern>(begin);
        pattern->literal = ParseLiteralPatternBody();
        pattern->span = SpanFrom(begin);
        return pattern;
      }
      default:
        ExpectedHere("a pattern");
        return NewMissing();
    }
  }

  // ---- expressions ----------------------------------------------------------

  struct BinaryInfo {
    Binary::Op op;
    int precedence;
  };

  static auto GetBinaryInfo(const Token::Kind kind, BinaryInfo* info) -> bool {
    switch (kind) {
      case K::kOr:
        *info = {Binary::kOr, 1};
        return true;
      case K::kAnd:
        *info = {Binary::kAnd, 2};
        return true;
      case K::kEq:
        *info = {Binary::kEq, 3};
        return true;
      case K::kNeq:
        *info = {Binary::kNeq, 3};
        return true;
      case K::kLt:
        *info = {Binary::kLt, 4};
        return true;
      case K::kLe:
        *info = {Binary::kLe, 4};
        return true;
      case K::kGt:
        *info = {Binary::kGt, 4};
        return true;
      case K::kGe:
        *info = {Binary::kGe, 4};
        return true;
      case K::kPlus:
        *info = {Binary::kAdd, 5};
        return true;
      case K::kBinMinus:
        *info = {Binary::kSub, 5};
        return true;
      case K::kStar:
        *info = {Binary::kMul, 6};
        return true;
      case K::kSlash:
        *info = {Binary::kDiv, 6};
        return true;
      default:
        return false;
    }
  }

  // An unspaced `-` right after an operand, on the same line, in front of
  // something that starts an operand: someone meant to subtract.
  auto LooksLikeBadSubtraction() const -> bool {
    if (!Is(K::kMinus) || index_ == 0)
      return false;
    const auto& prev = tokens_[index_ - 1];
    const auto& after = Peek(1);
    if (!after.Test(kExpressionStart) || after.kind == K::kMinus || after.kind == K::kBang)
      return false;
    const auto gap = source_.GetText(Span(prev.span.end, Peek().span.begin));
    return gap.find('\n') == std::string_view::npos;
  }

  auto ParseOperation(const int min_precedence) -> Node* {
    // no NestingGuard: each recursive call here has a higher precedence, so
    // this can't nest deeper than the number of precedence levels
    const auto begin = Here();
    auto* lhs = ParseUnary();
    while (true) {
      BinaryInfo info{};
      if (!GetBinaryInfo(PeekKind(), &info)) {
        if (!LooksLikeBadSubtraction())
          break;
        ReportHere(DiagnosticCode::kBinaryMinusSpacing, "binary `-` needs whitespace on both sides",
                   "add a space before and after the `-`")
            .WithHelp("`a - b` subtracts, `-b` negates, and `a-b` is a single name");
        info = {Binary::kSub, 5};
      }
      if (info.precedence < min_precedence)
        break;
      const auto op = Next();
      auto* binary = New<Binary>(begin);
      binary->op = info.op;
      binary->op_span = op.span;
      binary->lhs = lhs;
      binary->rhs = ParseOperation(info.precedence + 1);
      binary->span = SpanFrom(begin);
      lhs = binary;
    }
    return lhs;
  }

  auto ParseUnary() -> Node* {
    NestingGuard guard(*this);
    const auto begin = Here();
    if (!guard.ok)
      return NewError(begin);
    if (Is(K::kMinus) || Is(K::kBang)) {
      const auto op = Next();
      auto* unary = New<Unary>(begin);
      unary->op = op.kind == K::kMinus ? Unary::kNeg : Unary::kNot;
      unary->op_span = op.span;
      unary->operand = ParseUnary();
      unary->span = SpanFrom(begin);
      return unary;
    }
    return ParsePostfix();
  }

  void ParseArguments(std::vector<Node*>& args, const Span& opener) {
    ParseDelimited(K::kRParen, opener, [&] { args.push_back(ParseExpression()); });
  }

  auto ParsePostfix() -> Node* {
    const auto begin = Here();
    auto* expr = ParsePrimary();
    while (true) {
      if (Is(K::kDot)) {
        Next();
        auto* member = New<Member>(begin);
        member->object = expr;
        member->member = ParseMemberName();
        member->span = SpanFrom(begin);
        expr = member;
      } else if (Is(K::kLParen)) {
        const auto open = Next();
        auto* call = New<Call>(begin);
        call->callee = expr;
        ParseArguments(call->args, open.span);
        call->span = SpanFrom(begin);
        expr = call;
      } else {
        break;
      }
    }
    return expr;
  }

  auto ParseExpression() -> Node* {
    NestingGuard guard(*this);
    const auto begin = Here();
    if (!guard.ok)
      return NewError(begin);
    auto* lhs = ParseOperation(0);
    while (Is(K::kPipe))
      lhs = ParsePipe(begin, lhs);
    return lhs;
  }

  auto ParsePipe(const uint32_t begin, Node* lhs) -> Node* {
    const auto pipe = Next();
    auto* node = New<Pipe>(begin);
    node->lhs = lhs;
    const auto callee_begin = Here();
    if (Is(K::kIdent) || Is(K::kUpperIdent)) {
      const auto token = Next();
      Node* callee = nullptr;
      if (token.kind == K::kIdent) {
        auto* ref = tree_.arena_.New<VarRef>(token.span);
        ref->name = MakeName(token.span, Text(token.span));
        callee = ref;
      } else {
        auto* ref = tree_.arena_.New<VariantRef>(token.span);
        ref->name = MakeName(token.span, Text(token.span));
        callee = ref;
      }
      while (Is(K::kDot)) {
        Next();
        auto* member = New<Member>(callee_begin);
        member->object = callee;
        member->member = ParseMemberName();
        member->span = SpanFrom(callee_begin);
        callee = member;
      }
      node->callee = callee;
    } else if (Is(K::kLParen)) {
      const auto open = Next();
      auto* paren = New<Paren>(callee_begin);
      paren->inner = ParseExpression();
      ExpectClosing(K::kRParen, open.span);
      paren->span = SpanFrom(callee_begin);
      node->callee = paren;
    } else {
      ReportHere(DiagnosticCode::kPipeTargetNotCall, "the right side of `|>` must be a call, found " + Found(Peek()),
                 "expected a function name here")
          .WithLabel(pipe.span, "this pipes the value on its left into a call")
          .WithHelp("write it like `value |> name(other, args)`; the value becomes the first argument");
      node->callee = NewMissing();
      node->span = SpanFrom(begin);
      return node;
    }
    if (Is(K::kLParen)) {
      const auto open = Next();
      ParseArguments(node->args, open.span);
    } else {
      ReportHere(DiagnosticCode::kPipeTargetNotCall, "the right side of `|>` must be a call, but `" +
                                                         Text(node->callee->span) + "` has no argument list",
                 "add `()` here")
          .WithLabel(pipe.span, "this pipes the value on its left into a call")
          .WithHelp("write `|> " + Text(node->callee->span) +
                    "()`; the value on the left becomes the first argument");
    }
    node->span = SpanFrom(begin);
    return node;
  }

  // `( ident, ident ) =>`, or any parenthesized group that is followed by `=>`.
  auto LooksLikeLambda() const -> bool {
    int depth = 0;
    for (size_t ahead = 0; ahead < 256; ahead++) {
      const auto kind = PeekKind(ahead);
      if (kind == K::kEof)
        return false;
      if (IsOpener(kind)) {
        depth++;
      } else if (IsCloser(kind)) {
        depth--;
        if (depth == 0)
          return kind == K::kRParen && PeekKind(ahead + 1) == K::kFatArrow;
      }
    }
    return false;
  }

  auto ParseLambda() -> Node* {
    const auto begin = Here();
    const auto open = Next();
    auto* lambda = New<Lambda>(begin);
    ParseDelimited(K::kRParen, open.span, [&] { lambda->params.push_back(ParseName("a parameter name")); });
    Expect(K::kFatArrow);
    lambda->body = ParseExpression();
    lambda->span = SpanFrom(begin);
    return lambda;
  }

  auto ParseSeqLiteral() -> Node* {
    const auto begin = Here();
    const auto open = Next();
    auto* seq = New<SeqLit>(begin);
    ParseDelimited(K::kRBrack, open.span, [&] { seq->elements.push_back(ParseExpression()); });
    seq->span = SpanFrom(begin);
    return seq;
  }

  // `{` starts a record when it is `{}`, `{ ...`, or `{ name: `; otherwise a
  // block.
  auto ParseBraced() -> Node* {
    const auto next = PeekKind(1);
    if (next == K::kRBrace || next == K::kSpread || (next == K::kIdent && PeekKind(2) == K::kColon))
      return ParseRecordLiteral();
    return ParseBlock();
  }

  auto ParseRecordLiteral() -> Node* {
    const auto begin = Here();
    const auto open = Next();
    auto* record = New<RecordLit>(begin);
    ParseDelimited(K::kRBrace, open.span, [&] {
      const auto entry_begin = Here();
      auto* entry = tree_.arena_.New<Arg>(Span(entry_begin, entry_begin));
      if (Is(K::kIdent) && PeekKind(1) == K::kColon) {
        const auto name = Next();
        Next();  // ':'
        entry->form = Arg::kNamed;
        entry->name = MakeName(name.span, Text(name.span));
        entry->value = ParseExpression();
      } else if (Accept(K::kSpread)) {
        entry->form = Arg::kSpread;
        entry->value = ParseExpression();
      } else {
        // `{ a: 1, 2 }`: not an entry. Say what an entry looks like and skip it.
        ReportHere(DiagnosticCode::kBadRecordEntry, "expected `name: value` or `...spread` in this record, found " +
                                                        Found(Peek()),
                   "not a record entry")
            .WithLabel(open.span, "this `{` starts a record")
            .WithHelp("every entry in a record needs a name, like `{ x: 1, y: 2 }`; to copy another record's "
                      "entries use `...other`");
        SkipUntil(K::Set({K::kComma}));
        entry->form = Arg::kPositional;
        entry->value = NewError(entry_begin);
      }
      entry->span = SpanFrom(entry_begin);
      record->entries.push_back(entry);
    });
    record->span = SpanFrom(begin);
    return record;
  }

  // A `x.y = ...` where a statement was expected: records are immutable.
  auto DiagnoseAssignToExpression(Node* target) -> bool {
    if (!Is(K::kAssign))
      return false;
    const auto eq = Next();
    auto& d = Report(DiagnosticCode::kAssignToProperty, Span(target->span.begin, eq.span.end),
                     "`" + Text(target->span) + "` can't be assigned to", "can't assign here");
    if (const auto* member = target->As<Member>(); member != nullptr && member->member.IsRaw() == false) {
      d.WithHelp("records are immutable; build a new one instead: `" + Text(member->object->span) + " = { ..." +
                 Text(member->object->span) + ", " + member->member.text + ": ... }`");
    } else {
      d.WithHelp("only a plain name can be assigned to, like `count = count + 1`");
    }
    ParseExpression();  // parse and drop the right hand side so it isn't reported again
    return true;
  }

  auto ParseBinding(std::vector<Node*> directives) -> Node* {
    const auto begin = directives.empty() ? Here() : directives.front()->span.begin;
    auto* binding = tree_.arena_.New<Binding>(Span(begin, begin));
    binding->directives = std::move(directives);
    binding->is_let = Is(K::kLet);
    if (binding->is_let)
      Next();
    binding->name = ParseName("a name");
    if (binding->is_let && Accept(K::kColon))
      binding->annotation = ParseTypeExpr();
    if (!Expect(K::kAssign)) {
      binding->value = NewMissing();
    } else {
      binding->value = ParseExpression();
    }
    binding->span = SpanFrom(begin);
    return binding;
  }

  auto AtBindingStart() const -> bool {
    return Is(K::kLet) || (Is(K::kIdent) && PeekKind(1) == K::kAssign);
  }

  auto ParseBlock() -> Node* {
    const auto begin = Here();
    auto* block = New<Block>(begin);
    const auto open = Peek();
    if (!Expect(K::kLBrace)) {
      // the block that should have been here: empty, and right after the last token
      block->span = Span(prev_end_, prev_end_);
      return block;
    }
    while (!Is(K::kRBrace) && !AtEof() && !AtItemKeyword()) {
      const auto before = index_;
      auto directives = ParseDirectives();
      if (AtBindingStart()) {
        block->statements.push_back(ParseBinding(std::move(directives)));
      } else if (block->result == nullptr && (!directives.empty() ? Is(K::kHash) : Peek().Test(kExpressionStart))) {
        auto* expr = directives.empty() ? ParseExpression() : ParseUiElement(std::move(directives));
        if (DiagnoseAssignToExpression(expr))
          expr = NewError(expr->span.begin);
        block->result = expr;
        if (!Is(K::kRBrace) && !AtEof()) {
          if (Is(K::kComma) && block->statements.empty()) {
            // `{ 10, }`, `{ get-data(), }`: a record whose entry has no name
            ReportHere(DiagnosticCode::kBadRecordEntry,
                       "`{ ... }` with `" + Text(expr->span) + "` in it is a block, not a record", "a `,` doesn't belong here")
                .WithLabel(open.span, "this `{` starts a block because its first entry has no name")
                .WithHelp("records need a name for every entry, like `{ value: " + Text(expr->span) +
                          " }`; a block is `{ ... }` holding one expression, with no commas");
          } else {
            ReportHere(DiagnosticCode::kUnexpectedToken,
                       "expected `}` after the last expression of this block, found " + Found(Peek()),
                       "the block should have ended here")
                .WithLabel(open.span, "this `{` starts the block")
                .WithHelp("a block's value is its last expression, so nothing can follow it");
          }
          SkipUntil(K::Set({K::kRBrace}));
        }
      } else {
        ReportHere(DiagnosticCode::kUnexpectedToken, "expected a `let` binding, an expression or `}`, found " +
                                                         Found(Peek()),
                   "not allowed here");
        SkipUntil(K::Set({K::kRBrace, K::kLet}));
        if (index_ == before && !AtEof() && !Is(K::kRBrace) && !AtItemKeyword())
          Next();
      }
    }
    ExpectClosing(K::kRBrace, open.span);
    block->span = SpanFrom(begin);
    return block;
  }

  auto ParseIf() -> Node* {
    NestingGuard guard(*this);
    const auto begin = Here();
    if (!guard.ok)
      return NewError(begin);
    const auto if_token = Next();
    auto* node = New<IfExpr>(begin);
    node->condition = ParseExpression();
    node->then_branch = ParseBlock();
    if (Accept(K::kElse)) {
      node->else_branch = Is(K::kIf) ? ParseIf() : ParseBlock();
    } else {
      ReportHere(DiagnosticCode::kIfWithoutElse, "this `if` has no `else`, found " + Found(Peek()) + " instead",
                 "expected `else` here", true)
          .WithLabel(if_token.span, "this `if` is an expression, so it must produce a value either way")
          .WithHelp("add an `else` branch: `if cond { a } else { b }`");
    }
    node->span = SpanFrom(begin);
    return node;
  }

  auto ParseMatch() -> Node* {
    const auto begin = Here();
    const auto match_token = Next();
    auto* node = New<MatchExpr>(begin);
    node->scrutinee = ParseExpression();
    const auto open = Peek();
    if (!Expect(K::kLBrace)) {
      node->span = SpanFrom(begin);
      return node;
    }
    ParseDelimited(K::kRBrace, open.span, [&] {
      const auto arm_begin = Here();
      auto* arm = tree_.arena_.New<MatchArm>(Span(arm_begin, arm_begin));
      arm->pattern = ParsePattern();
      Expect(K::kFatArrow);
      arm->body = ParseExpression();
      arm->span = SpanFrom(arm_begin);
      node->arms.push_back(arm);
    });
    if (node->arms.empty()) {
      Report(DiagnosticCode::kEmptyMatch, match_token.span, "this `match` has no arms", "nothing to match against")
          .WithHelp("add at least one arm, like `_ => value`");
    }
    node->span = SpanFrom(begin);
    return node;
  }

  auto ParseFor() -> Node* {
    const auto begin = Here();
    const auto for_token = Next();
    auto* node = New<ForExpr>(begin);
    node->binding = ParseName("a loop variable name");
    Expect(K::kIn);
    node->iterable = ParseExpression();
    if (Accept(K::kIf))
      node->guard = ParseExpression();
    if (Accept(K::kFold)) {
      node->has_fold = true;
      node->accumulator = ParseName("an accumulator name");
      Expect(K::kAssign);
      node->initial = ParseExpression();
    }
    if (!Accept(K::kFatArrow)) {
      ReportHere(DiagnosticCode::kForWithoutArrow, "expected `=>` before the body of this loop, found " + Found(Peek()),
                 "expected `=>` here")
          .WithLabel(for_token.span, "this loop")
          .WithHelp("a loop reads `for item in items => body`, or `for item in items fold acc = start => body`");
    }
    node->body = ParseExpression();
    node->span = SpanFrom(begin);
    return node;
  }

  auto ParseUiElement(std::vector<Node*> directives) -> Node* {
    NestingGuard guard(*this);
    if (!guard.ok)
      return NewMissing();
    const auto begin = directives.empty() ? Here() : directives.front()->span.begin;
    auto* element = tree_.arena_.New<UiElement>(Span(begin, begin));
    element->directives = std::move(directives);
    if (!Expect(K::kHash)) {
      element->span = SpanFrom(begin);
      return element;
    }
    if (Is(K::kIdent)) {
      const auto tag = Next();
      element->tag = MakeName(tag.span, Text(tag.span));
    } else {
      ExpectedHere("an element name after `#`");
      element->tag = MissingName();
    }
    if (Is(K::kLParen)) {
      const auto open = Next();
      element->has_props = true;
      ParseDelimited(K::kRParen, open.span, [&] { element->props.push_back(ParseUiArg()); });
    }
    if (Is(K::kLBrace)) {
      const auto open = Next();
      element->has_children = true;
      ParseUiChildren(*element, open.span);
    }
    element->span = SpanFrom(begin);
    return element;
  }

  void ParseUiChildren(UiElement& element, const Span& open) {
    while (!Is(K::kRBrace) && !AtEof() && !AtItemKeyword()) {
      const auto before = index_;
      auto directives = ParseDirectives();
      if (Is(K::kHash)) {
        element.children.push_back(ParseUiElement(std::move(directives)));
      } else if (AtBindingStart()) {
        element.children.push_back(ParseBinding(std::move(directives)));
      } else {
        ReportHere(DiagnosticCode::kUnexpectedToken,
                   "expected a child `#element`, a `let` binding or `}`, found " + Found(Peek()), "not allowed here")
            .WithLabel(open, "inside the children of this element")
            .WithHelp("children are `#elements` and `let` bindings; text goes in an element, like `#text(\"hi\")`");
        SkipUntil(K::Set({K::kRBrace, K::kHash, K::kLet}));
        if (index_ == before && !AtEof() && !Is(K::kRBrace) && !AtItemKeyword())
          Next();
      }
    }
    ExpectClosing(K::kRBrace, open);
  }

  auto ParsePrimary() -> Node* {
    const auto begin = Here();
    const auto token = Peek();
    switch (token.kind) {
      case K::kNumber: {
        Next();
        auto* number = New<NumberLit>(begin);
        number->text = Text(token.span);
        return number;
      }
      case K::kString: {
        Next();
        auto* string = New<StringLit>(begin);
        string->value = Unescape(source_.GetText(token.span));
        return string;
      }
      case K::kTrue:
      case K::kFalse: {
        Next();
        auto* boolean = New<BoolLit>(begin);
        boolean->value = token.kind == K::kTrue;
        return boolean;
      }
      case K::kNone:
        Next();
        return New<NoneLit>(begin);
      case K::kPxLit:
      case K::kPctLit:
      case K::kFrLit:
      case K::kColorLit:
        Next();
        return MakeUnit(token);
      case K::kIdent: {
        Next();
        auto* ref = New<VarRef>(begin);
        ref->name = MakeName(token.span, Text(token.span));
        return ref;
      }
      case K::kUpperIdent: {
        Next();
        auto* ref = New<VariantRef>(begin);
        ref->name = MakeName(token.span, Text(token.span));
        return ref;
      }
      case K::kLBrack:
        return ParseSeqLiteral();
      case K::kLBrace:
        return ParseBraced();
      case K::kIf:
        return ParseIf();
      case K::kMatch:
        return ParseMatch();
      case K::kFor:
        return ParseFor();
      case K::kDirective: {
        auto directives = ParseDirectives();
        if (!Is(K::kHash)) {
          ReportHere(DiagnosticCode::kUnexpectedToken, "expected a `#element` after this directive, found " +
                                                           Found(Peek()),
                     "expected `#` here")
              .WithLabel(directives.back()->span, "directives in an expression apply to the element after them");
          return NewError(begin);
        }
        return ParseUiElement(std::move(directives));
      }
      case K::kHash:
        return ParseUiElement({});
      case K::kLParen: {
        if (LooksLikeLambda())
          return ParseLambda();
        const auto open = Next();
        auto* paren = New<Paren>(begin);
        paren->inner = ParseExpression();
        ExpectClosing(K::kRParen, open.span);
        paren->span = SpanFrom(begin);
        return paren;
      }
      case K::kElse:
        ReportHere(DiagnosticCode::kDuplicateElse, "this `else` has no `if` to belong to", "no matching `if`")
            .WithHelp("an `else` must directly follow the closing `}` of an `if` block");
        return NewMissing();
      case K::kBinMinus:
        ReportHere(DiagnosticCode::kUnexpectedToken, "expected an expression, found `-`", "expected an expression here")
            .WithHelp("a negative number is written without a space, like `-1`");
        return NewMissing();
      default:
        ExpectedHere("an expression");
        return NewMissing();
    }
  }

  // ---- declarations ---------------------------------------------------------

  auto ParseTypeDecl(std::vector<Node*> directives) -> Node* {
    const auto begin = directives.empty() ? Here() : directives.front()->span.begin;
    auto* decl = tree_.arena_.New<TypeDecl>(Span(begin, begin));
    decl->directives = std::move(directives);
    Next();  // `type`
    if (Is(K::kUpperIdent)) {
      const auto name = Next();
      decl->name = MakeName(name.span, Text(name.span));
    } else {
      ExpectedHere("a capitalized type name");
      if (Is(K::kIdent)) {
        const auto name = Next();
        decl->name = MakeName(name.span, Text(name.span));
      } else {
        decl->name = MissingName();
      }
    }
    if (Is(K::kLt)) {
      const auto open = Next();
      while (true) {
        if (Is(K::kUpperIdent)) {
          const auto param = Next();
          decl->type_params.push_back(MakeName(param.span, Text(param.span)));
        } else {
          ExpectedHere("a capitalized type parameter name");
          break;
        }
        if (!Accept(K::kComma))
          break;
      }
      ExpectClosing(K::kGt, open.span);
    }
    if (!Expect(K::kAssign)) {
      decl->type = NewMissing();
    } else {
      decl->type = ParseTypeExpr();
    }
    decl->span = SpanFrom(begin);
    return decl;
  }

  // Parameters up to and including the `)`. `variadic` is set for a trailing
  // `...` when the caller allows one.
  void ParseParams(std::vector<Node*>& params, const Span& open, bool* variadic) {
    while (true) {
      if (Accept(K::kRParen))
        return;
      if (AtEof() || AtItemKeyword()) {
        ExpectClosing(K::kRParen, open);
        return;
      }
      const auto before = index_;
      if (Is(K::kSpread) && variadic != nullptr) {
        Next();
        *variadic = true;
        Accept(K::kComma);
        ExpectClosing(K::kRParen, open);
        return;
      }
      const auto begin = Here();
      auto* param = tree_.arena_.New<Param>(Span(begin, begin));
      param->name = ParseName("a parameter name");
      if (Expect(K::kColon))
        param->type = ParseTypeExpr();
      else
        param->type = NewMissing();
      param->span = SpanFrom(begin);
      params.push_back(param);
      if (Accept(K::kComma))
        continue;
      if (Accept(K::kRParen))
        return;
      ExpectedHere("`,` or `)`");
      SkipUntil(K::Set({K::kComma, K::kRParen}));
      if (Accept(K::kComma))
        continue;
      if (Accept(K::kRParen))
        return;
      if (index_ == before && !AtEof() && !IsCloser(PeekKind()) && !AtItemKeyword())
        Next();
      if (AtEof() || IsCloser(PeekKind()) || AtItemKeyword()) {
        ExpectClosing(K::kRParen, open);
        return;
      }
    }
  }

  auto ParseFnDecl(std::vector<Node*> directives) -> Node* {
    const auto begin = directives.empty() ? Here() : directives.front()->span.begin;
    auto* decl = tree_.arena_.New<FnDecl>(Span(begin, begin));
    decl->directives = std::move(directives);
    const auto fn_token = Next();
    decl->name = ParseName("a function name");
    if (Is(K::kLParen)) {
      const auto open = Next();
      ParseParams(decl->params, open.span, nullptr);
    } else {
      ReportHere(DiagnosticCode::kFnWithoutParens, "a function needs a parameter list, found " + Found(Peek()),
                 "expected `(` here")
          .WithLabel(fn_token.span, "in this function")
          .WithHelp("functions always have parentheses, even with no parameters: `fn " +
                    (decl->name.missing ? std::string("name") : decl->name.text) + "() => ...`");
    }
    if (Accept(K::kColon))
      decl->result = ParseTypeExpr();
    if (!Expect(K::kFatArrow))
      decl->body = NewMissing();
    else
      decl->body = ParseExpression();
    decl->span = SpanFrom(begin);
    return decl;
  }

  auto ParseExternDecl(std::vector<Node*> directives) -> Node* {
    const auto begin = directives.empty() ? Here() : directives.front()->span.begin;
    auto* decl = tree_.arena_.New<ExternDecl>(Span(begin, begin));
    decl->directives = std::move(directives);
    const auto extern_token = Next();
    if (Is(K::kString)) {
      const auto symbol = Next();
      decl->has_symbol = true;
      decl->symbol_span = symbol.span;
      decl->symbol = Unescape(source_.GetText(symbol.span));
    }
    Expect(K::kFn);
    decl->c_name = ParseCName("a function name");
    if (Is(K::kLParen)) {
      const auto open = Next();
      ParseParams(decl->params, open.span, &decl->variadic);
    } else {
      ExpectedHere("`(`");
    }
    if (Accept(K::kColon)) {
      decl->result = ParseTypeExpr();
    } else {
      ReportHere(DiagnosticCode::kExternWithoutReturnType, "this extern function has no return type, found " +
                                                               Found(Peek()),
                 "expected `:` and a return type here")
          .WithLabel(extern_token.span, "in this declaration")
          .WithHelp("say what the C function returns, like `extern fn sqrt(x: Float): Float`");
      decl->result = NewMissing();
    }
    if (Accept(K::kFatArrow))
      decl->body = ParseExpression();
    decl->span = SpanFrom(begin);
    return decl;
  }

  auto ParseUseDecl(std::vector<Node*> directives) -> Node* {
    const auto begin = directives.empty() ? Here() : directives.front()->span.begin;
    auto* decl = tree_.arena_.New<UseDecl>(Span(begin, begin));
    decl->directives = std::move(directives);
    const auto use_token = Next();
    if (Is(K::kString)) {
      const auto header = Next();
      decl->has_header = true;
      decl->header_span = header.span;
      decl->header = Unescape(source_.GetText(header.span));
    } else {
      ReportHere(DiagnosticCode::kUseWithoutHeader, "`use` needs a header in quotes, found " + Found(Peek()),
                 "expected a string like \"math.h\" here")
          .WithLabel(use_token.span, "in this `use`");
    }
    if (Accept(K::kAs)) {
      decl->has_alias = true;
      if (Is(K::kIdent) || Is(K::kUpperIdent)) {
        const auto alias = Next();
        decl->alias = MakeName(alias.span, Text(alias.span));
      } else {
        ExpectedHere("a name for the alias");
        decl->alias = MissingName();
      }
    }
    decl->span = SpanFrom(begin);
    return decl;
  }

  auto ParseItem() -> Node* {
    const auto begin = Here();
    auto directives = ParseDirectives();
    switch (PeekKind()) {
      case K::kType:
        return ParseTypeDecl(std::move(directives));
      case K::kFn:
        return ParseFnDecl(std::move(directives));
      case K::kExtern:
        return ParseExternDecl(std::move(directives));
      case K::kUse:
        return ParseUseDecl(std::move(directives));
      case K::kLet:
        return ParseBinding(std::move(directives));
      default:
        break;
    }
    if (Is(K::kIdent) && PeekKind(1) == K::kAssign)
      return ParseBinding(std::move(directives));
    if (directives.empty() && Peek().Test(kExpressionStart) && !Is(K::kDirective)) {
      // An expression where a declaration belongs. Parse it anyway so the
      // message can quote it, and so a later error inside it is still found.
      auto* expr = ParseExpression();
      if (!DiagnoseAssignToExpression(expr)) {
        Report(DiagnosticCode::kTopLevelExpression, expr->span, "an expression can't stand on its own at the top level",
               "not a declaration")
            .WithHelp("give it a name with `let name = ...`, or put it in a function body");
      }
      return NewError(begin);
    }
    ReportHere(DiagnosticCode::kExpectedItem, "expected a declaration, found " + Found(Peek()),
               "expected `fn`, `type`, `let`, `use` or `extern`");
    return NewError(begin);
  }

 public:
  TreeBuilder(SyntaxTree& tree, const ParseOptions& options) :
    tree_(tree),
    source_(tree.GetSource()),
    options_(options),
    tokens_(tree.tokens_),
    diagnostics_(tree.diagnostics_) {}

  void Run() {
    auto* module = tree_.arena_.New<Module>(Span(0, source_.GetSize()));
    while (!AtEof()) {
      const auto before = index_;
      if (IsCloser(PeekKind())) {
        ReportHere(DiagnosticCode::kUnexpectedToken, "unmatched " + Found(Peek()), "nothing to close")
            .WithHelp("remove it, or add the opening delimiter it belongs to");
        Next();
        continue;
      }
      const auto reported = diagnostics_.size();
      auto* item = ParseItem();
      module->items.push_back(item);
      // Whatever follows a broken item is likely a leftover of it. Skip to the
      // next thing that looks like a declaration rather than report the
      // leftovers as errors of their own.
      if (item->Is<Error>() || diagnostics_.size() != reported)
        RecoverToItem();
      if (index_ == before && !AtEof())
        Next();  // always make progress
    }
    tree_.root_ = module;
  }

  // Widens every node to cover its children. Spans are set where each node is
  // built, which is exact for a well formed file; around an ErrorNode (an empty
  // placeholder sitting between two tokens) a parent can end up a hair short of
  // it. Tools rely on "a node contains its children", so that is made true here,
  // in one place, after the fact. Iterative: a chain like `a + a + a + ...` is
  // as deep as it is long.
  static void CoverChildren(Node* root) {
    struct Frame {
      Node* node;
      bool expanded;
    };
    std::vector<Frame> stack{{root, false}};
    while (!stack.empty()) {
      auto frame = stack.back();
      stack.pop_back();
      if (!frame.expanded) {
        stack.push_back({frame.node, true});
        frame.node->ForEachChild([&](const Node* child) { stack.push_back({const_cast<Node*>(child), false}); });
        continue;
      }
      frame.node->ForEachChild([&](const Node* child) { frame.node->span = frame.node->span.Join(child->span); });
    }
  }

  static auto Build(std::shared_ptr<const SourceFile> source, const ParseOptions& options)
      -> std::unique_ptr<SyntaxTree> {
    auto tree = std::make_unique<SyntaxTree>(std::move(source));
    auto lexed = Lex(tree->GetSource(), tree->diagnostics_);
    tree->tokens_ = std::move(lexed.tokens);
    tree->comments_ = std::move(lexed.comments);
    TreeBuilder(*tree, options).Run();
    CoverChildren(const_cast<Module*>(tree->root_));
    tree->diagnostics_.SortBySpan();
    return tree;
  }
};

auto Parse(std::shared_ptr<const SourceFile> source, const ParseOptions& options) -> std::unique_ptr<SyntaxTree> {
  return TreeBuilder::Build(std::move(source), options);
}

auto ParseText(std::string name, std::string text, const ParseOptions& options) -> std::unique_ptr<SyntaxTree> {
  return Parse(std::make_shared<const SourceFile>(std::move(name), std::move(text)), options);
}
}  // namespace kura::syntax
