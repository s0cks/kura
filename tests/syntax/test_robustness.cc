// The parser sits under the LSP, so it gets fed half typed, broken and hostile
// input all day. These tests throw a lot of that at it and check the things
// that must always hold, however broken the input is:
//
//   - it terminates, without crashing or recursing without bound,
//   - the token stream and every span stays inside the file,
//   - a child node lies inside its parent,
//   - an ErrorNode in the tree always comes with a diagnostic, and a tree with
//     no diagnostics has no ErrorNode (no silent errors).
#include <gtest/gtest.h>

#include <chrono>
#include <dirent.h>
#include <random>

#include "test_util.h"

#ifndef KURA_GRAMMAR_TEST_DIR
#error "KURA_GRAMMAR_TEST_DIR must point at tests/grammar"
#endif

namespace kura::syntax::test {
namespace {
auto ListFiles(const std::string& dir) -> std::vector<std::string> {
  std::vector<std::string> files{};
  if (auto* d = opendir(dir.c_str()); d != nullptr) {
    while (const auto* entry = readdir(d)) {
      const std::string name = entry->d_name;
      if (name.size() > 4 && name.substr(name.size() - 4) == ".kui")
        files.push_back(dir + "/" + name);
    }
    closedir(d);
  }
  std::sort(files.begin(), files.end());
  return files;
}

auto ValidFiles() -> std::vector<std::string> {
  return ListFiles(std::string(KURA_GRAMMAR_TEST_DIR) + "/valid");
}

auto InvalidFiles() -> std::vector<std::string> {
  return ListFiles(std::string(KURA_GRAMMAR_TEST_DIR) + "/invalid");
}

void CheckInvariants(const SyntaxTree& tree, const std::string& text, const std::string& context) {
  const auto size = static_cast<uint32_t>(text.size());
  const auto& tokens = tree.GetTokens();
  ASSERT_FALSE(tokens.empty()) << context;
  ASSERT_EQ(tokens.back().kind, Token::kEof) << context;
  uint32_t last_end = 0;
  for (size_t idx = 0; idx + 1 < tokens.size(); idx++) {
    ASSERT_NE(tokens[idx].kind, Token::kEof) << context;
    ASSERT_GE(tokens[idx].span.begin, last_end) << context;
    ASSERT_GE(tokens[idx].span.end, tokens[idx].span.begin) << context;
    ASSERT_LE(tokens[idx].span.end, size) << context;
    last_end = tokens[idx].span.end;
  }

  ASSERT_NE(tree.GetRoot(), nullptr) << context;
  size_t error_nodes = 0;
  std::vector<const Node*> stack{tree.GetRoot()};
  while (!stack.empty()) {
    const auto* node = stack.back();
    stack.pop_back();
    ASSERT_LE(node->span.begin, node->span.end) << context << " " << node->GetKindName();
    ASSERT_LE(node->span.end, size) << context << " " << node->GetKindName();
    error_nodes += node->Is<Error>();
    node->ForEachChild([&](const Node* child) {
      EXPECT_GE(child->span.begin, node->span.begin) << context << " " << node->GetKindName() << " > " << child->GetKindName();
      EXPECT_LE(child->span.end, node->span.end) << context << " " << node->GetKindName() << " > " << child->GetKindName();
      stack.push_back(child);
    });
  }

  for (const auto& d : tree.GetDiagnostics()) {
    ASSERT_LE(d.span.begin, d.span.end) << context;
    ASSERT_LE(d.span.end, size) << context;
    for (const auto& label : d.labels)
      ASSERT_LE(label.span.end, size) << context;
  }
  // no silent errors, and no errors without a trace in the tree either
  if (error_nodes > 0) {
    ASSERT_TRUE(tree.GetDiagnostics().HasErrors()) << context << ": error node without a diagnostic";
  }
  if (!tree.GetDiagnostics().HasErrors()) {
    ASSERT_EQ(error_nodes, 0U) << context;
  }
  // rendering every diagnostic must not fall over either
  (void) RenderToString(tree.GetSource(), tree.GetDiagnostics());
}

auto ParseChecked(const std::string& text, const std::string& context) -> std::unique_ptr<SyntaxTree> {
  const auto start = std::chrono::steady_clock::now();
  auto tree = Tree(text);
  const auto elapsed = std::chrono::steady_clock::now() - start;
  EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 5000) << context << ": too slow";
  CheckInvariants(*tree, text, context);
  return tree;
}
}  // namespace

TEST(CorpusTest, EveryValidFileParsesWithoutDiagnostics) {
  const auto files = ValidFiles();
  ASSERT_FALSE(files.empty());
  for (const auto& file : files) {
    const auto text = ReadFile(file);
    const auto tree = ParseChecked(text, file);
    EXPECT_TRUE(tree->GetDiagnostics().empty()) << file << "\n" << RenderToString(tree->GetSource(), tree->GetDiagnostics());
  }
}

TEST(CorpusTest, EveryInvalidFileIsRejected) {
  const auto files = InvalidFiles();
  ASSERT_FALSE(files.empty());
  for (const auto& file : files) {
    const auto tree = ParseChecked(ReadFile(file), file);
    EXPECT_TRUE(tree->GetDiagnostics().HasErrors()) << file << " was accepted";
  }
}

TEST(CorpusTest, EveryPrefixOfEveryValidFileHoldsTheInvariants) {
  for (const auto& file : ValidFiles()) {
    const auto text = ReadFile(file);
    for (size_t len = 0; len <= text.size(); len++)
      ParseChecked(text.substr(0, len), file + " cut at " + std::to_string(len));
  }
}

TEST(CorpusTest, EveryDeletedByteOfEveryValidFileHoldsTheInvariants) {
  for (const auto& file : ValidFiles()) {
    const auto text = ReadFile(file);
    for (size_t at = 0; at < text.size(); at++) {
      auto mutated = text;
      mutated.erase(at, 1);
      ParseChecked(mutated, file + " without byte " + std::to_string(at));
    }
  }
}

TEST(CorpusTest, EveryDeletedTokenOfEveryValidFileHoldsTheInvariants) {
  for (const auto& file : ValidFiles()) {
    const auto text = ReadFile(file);
    const auto original = Tree(text);
    for (const auto& token : original->GetTokens()) {
      if (token.kind == Token::kEof)
        continue;
      auto mutated = text;
      mutated.erase(token.span.begin, token.span.length());
      ParseChecked(mutated, file + " without token at " + std::to_string(token.span.begin));
    }
  }
}

// A fixed seed, so a failure here is reproducible.
TEST(FuzzTest, RandomMutationsOfValidFiles) {
  std::vector<std::string> corpus{};
  for (const auto& file : ValidFiles())
    corpus.push_back(ReadFile(file));
  for (const auto& file : InvalidFiles())
    corpus.push_back(ReadFile(file));
  ASSERT_FALSE(corpus.empty());

  const std::vector<std::string> pieces = {
      "(", ")", "{", "}", "[", "]", ",", ":", "=>", "=", "|>", "|", "...", ".", "#", "@", "-", " - ", "\"", "`", "<", ">",
      "fn", "let", "type", "if", "else", "match", "for", "in", "fold", "use", "as", "extern", "None", "_", "x", "Foo",
      "10px", "50%", "1.5", "\n", "  ", "//", "$", "\xC3\xA9"};

  std::mt19937 rng(0xC0FFEE);
  const auto pick = [&](const size_t n) { return static_cast<size_t>(rng() % n); };
  for (int iteration = 0; iteration < 4000; iteration++) {
    auto text = corpus[pick(corpus.size())];
    const auto edits = 1 + pick(4);
    for (size_t edit = 0; edit < edits && !text.empty(); edit++) {
      const auto at = pick(text.size());
      switch (pick(5)) {
        case 0:  // delete a range
          text.erase(at, 1 + pick(12));
          break;
        case 1:  // insert a piece of syntax
          text.insert(at, pieces[pick(pieces.size())]);
          break;
        case 2:  // duplicate a range
          text.insert(at, text.substr(at, 1 + pick(40)));
          break;
        case 3:  // replace a byte
          text[at] = static_cast<char>(pick(256));
          break;
        default: {  // swap two lines' worth of text
          const auto other = pick(text.size());
          const auto a = std::min(at, other);
          const auto b = std::max(at, other);
          text = text.substr(0, a) + text.substr(b) + text.substr(a, b - a);
          break;
        }
      }
    }
    ParseChecked(text, "fuzz iteration " + std::to_string(iteration));
    if (testing::Test::HasFatalFailure())
      return;
  }
}

TEST(FuzzTest, RandomBytes) {
  std::mt19937 rng(42);
  for (int iteration = 0; iteration < 500; iteration++) {
    std::string text{};
    const auto len = rng() % 300;
    for (size_t idx = 0; idx < len; idx++)
      text.push_back(static_cast<char>(rng() % 256));
    ParseChecked(text, "random bytes " + std::to_string(iteration));
    if (testing::Test::HasFatalFailure())
      return;
  }
}

// ---- hostile input ----------------------------------------------------------

TEST(HostileInputTest, DeepNestingIsAnErrorNotACrash) {
  const std::string deep = "let x = ";
  for (const auto* open : {"(", "[", "{", "-", "!", "{ a: ", "f(", "if a { ", "#a { ", "x |> f("}) {
    std::string text = deep;
    for (int idx = 0; idx < 100000; idx++)
      text += open;
    const auto tree = ParseChecked(text, std::string("nested ") + open);
    bool reported = false;
    for (const auto& d : tree->GetDiagnostics())
      reported |= d.code == DiagnosticCode::kTooMuchNesting;
    EXPECT_TRUE(reported) << open;
  }
}

TEST(HostileInputTest, DeepElseIfChainAndTypesAndPatterns) {
  std::string chain = "let x = if a { 1 }";
  for (int idx = 0; idx < 50000; idx++)
    chain += " else if a { 1 }";
  ParseChecked(chain + " else { 2 }", "else if chain");

  std::string type = "type T = ";
  for (int idx = 0; idx < 50000; idx++)
    type += "Seq<";
  ParseChecked(type, "nested type arguments");

  std::string pattern = "let x = match y { ";
  for (int idx = 0; idx < 50000; idx++)
    pattern += "A(";
  ParseChecked(pattern, "nested patterns");
}

TEST(HostileInputTest, ReasonableNestingStillParses) {
  std::string text = "let x = ";
  for (int idx = 0; idx < 60; idx++)
    text += "(";
  text += "1";
  for (int idx = 0; idx < 60; idx++)
    text += ")";
  const auto tree = ParseChecked(text, "60 parentheses");
  EXPECT_TRUE(tree->IsValid()) << RenderToString(tree->GetSource(), tree->GetDiagnostics());
}

TEST(HostileInputTest, LargeFlatInputsStayFast) {
  std::string seq = "let x = [";
  for (int idx = 0; idx < 200000; idx++)
    seq += "1, ";
  seq += "]";
  EXPECT_TRUE(ParseChecked(seq, "200k element seq")->IsValid());

  std::string items{};
  for (int idx = 0; idx < 50000; idx++)
    items += "fn f" + std::to_string(idx) + "(a: Int): Int => a + 1\n";
  EXPECT_TRUE(ParseChecked(items, "50k functions")->IsValid());

  std::string chain = "let x = a";
  for (int idx = 0; idx < 100000; idx++)
    chain += " + a.b(c)";
  EXPECT_TRUE(ParseChecked(chain, "100k long binary chain")->IsValid());

  // many parentheses that are not lambdas: the lookahead must stay bounded
  std::string parens = "let x = ";
  for (int idx = 0; idx < 5000; idx++)
    parens += "(a) + ";
  parens += "1";
  ParseChecked(parens, "5k parenthesized operands");
}

TEST(HostileInputTest, ManyErrorsStayFast) {
  std::string text{};
  for (int idx = 0; idx < 20000; idx++)
    text += "let a = )\nfn f( => ]\n";
  const auto tree = ParseChecked(text, "20k broken lines");
  EXPECT_GT(tree->GetDiagnostics().size(), 10000U);
}
}  // namespace kura::syntax::test
