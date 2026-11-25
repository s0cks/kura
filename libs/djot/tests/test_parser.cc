#include <gtest/gtest.h>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "djot.h"

namespace {

// Owned representation of a single SAX callback event
struct RecordedEvent {
  enum class Kind { Enter, Exit, Text };

  Kind kind;
  DjotEventKind node_type = kDjotDocument;
  int level = 0;
  std::string arg;
  std::string text;

  static RecordedEvent Enter(DjotEventKind type, std::string_view arg = "", int level = 0) {
    return {Kind::Enter, type, level, std::string(arg), ""};
  }

  static RecordedEvent Exit(DjotEventKind type, std::string_view arg = "", int level = 0) {
    return {Kind::Exit, type, level, std::string(arg), ""};
  }

  static RecordedEvent Text(std::string_view text) {
    return {Kind::Text, kDjotDocument, 0, "", std::string(text)};
  }

  bool operator==(const RecordedEvent& other) const {
    if (kind != other.kind)
      return false;
    if (kind == Kind::Text)
      return text == other.text;
    return node_type == other.node_type && level == other.level && arg == other.arg;
  }
};

const char* node_type_name(DjotEventKind type) {
  switch (type) {
    case kDjotDocument:
      return "DOC";
    case kDjotHeading:
      return "HEADING";
    case kDjotParagraph:
      return "PARAGRAPH";
    case kDjotList:
      return "LIST";
    case kDjotListItem:
      return "LIST_ITEM";
    case kDjotCodeBlock:
      return "CODE_BLOCK";
    case kDjotDiv:
      return "DIV";
    case kDjotSpan:
      return "SPAN";
    case kDjotLink:
      return "LINK";
    case kDjotCodeInline:
      return "CODE_INLINE";
    case kDjotStrong:
      return "STRONG";
    case kDjotEmphasis:
      return "EMPHASIS";
  }
  return "UNKNOWN";
}

// Custom GoogleTest printer for clean assertion diffs
void PrintTo(const RecordedEvent& ev, std::ostream* os) {
  switch (ev.kind) {
    case RecordedEvent::Kind::Enter:
      *os << "Enter(" << node_type_name(ev.node_type);
      if (ev.level != 0)
        *os << ", lvl=" << ev.level;
      if (!ev.arg.empty())
        *os << ", arg=\"" << ev.arg << "\"";
      *os << ")";
      break;
    case RecordedEvent::Kind::Exit:
      *os << "Exit(" << node_type_name(ev.node_type);
      if (ev.level != 0)
        *os << ", lvl=" << ev.level;
      if (!ev.arg.empty())
        *os << ", arg=\"" << ev.arg << "\"";
      *os << ")";
      break;
    case RecordedEvent::Kind::Text:
      *os << "Text(\"" << ev.text << "\")";
      break;
  }
}

class DjotParserTest : public ::testing::Test {
 protected:
  std::vector<RecordedEvent> events_;

  DjotParserConfig make_config() {
    DjotParserConfig cfg{};
    cfg.on_enter = &DjotParserTest::handle_enter;
    cfg.on_exit = &DjotParserTest::handle_exit;
    cfg.on_text = &DjotParserTest::handle_text;
    return cfg;
  }

  const std::vector<RecordedEvent>& parse(std::string_view input) {
    events_.clear();
    DjotParserConfig cfg = make_config();
    DjotParse(input.data(), input.size(), &cfg, this);
    return events_;
  }

 private:
  static inline void handle_enter(DjotEvent* event, void* data) {
    auto* self = static_cast<DjotParserTest*>(data);
    std::string_view arg(event->arg ? event->arg : "", event->arg_len);
    self->events_.push_back(RecordedEvent::Enter(event->kind, arg, event->level));
  }

  static inline void handle_exit(DjotEvent* event, void* data) {
    auto* self = static_cast<DjotParserTest*>(data);
    std::string_view arg(event->arg ? event->arg : "", event->arg_len);
    self->events_.push_back(RecordedEvent::Exit(event->kind, arg, event->level));
  }

  static inline void handle_text(const char* text, size_t len, void* data) {
    auto* self = static_cast<DjotParserTest*>(data);
    self->events_.push_back(RecordedEvent::Text({text, len}));
  }
};

// --- Test Cases ---

TEST_F(DjotParserTest, EmptyDocument) {
  EXPECT_EQ(parse(""), (std::vector<RecordedEvent>{
                           RecordedEvent::Enter(kDjotDocument),
                           RecordedEvent::Exit(kDjotDocument),
                       }));
}

TEST_F(DjotParserTest, HeadingAndSemanticSpans) {
  const std::string_view doc =
      "# E0308: Mismatched types\n"
      "\n"
      "Expected [`i32`]{.expected}, found [`&str`]{.found}.\n";

  EXPECT_EQ(parse(doc), (std::vector<RecordedEvent>{
                            RecordedEvent::Enter(kDjotDocument),

                            RecordedEvent::Enter(kDjotHeading, "", 1),
                            RecordedEvent::Text("E0308: Mismatched types"),
                            RecordedEvent::Exit(kDjotHeading, "", 1),

                            RecordedEvent::Enter(kDjotParagraph),
                            RecordedEvent::Text("Expected "),
                            RecordedEvent::Enter(kDjotSpan, ".expected"),
                            RecordedEvent::Enter(kDjotCodeInline),
                            RecordedEvent::Text("i32"),
                            RecordedEvent::Exit(kDjotCodeInline),
                            RecordedEvent::Exit(kDjotSpan, ".expected"),
                            RecordedEvent::Text(", found "),
                            RecordedEvent::Enter(kDjotSpan, ".found"),
                            RecordedEvent::Enter(kDjotCodeInline),
                            RecordedEvent::Text("&str"),
                            RecordedEvent::Exit(kDjotCodeInline),
                            RecordedEvent::Exit(kDjotSpan, ".found"),
                            RecordedEvent::Text("."),
                            RecordedEvent::Exit(kDjotParagraph),

                            RecordedEvent::Exit(kDjotDocument),
                        }));
}

TEST_F(DjotParserTest, DivAdmonitionAndCodeBlock) {
  const std::string_view doc =
      "::: note\n"
      "Use `.parse()`.\n"
      ":::\n"
      "\n"
      "```cpp\n"
      "int x = 1;\n"
      "```\n";

  EXPECT_EQ(parse(doc), (std::vector<RecordedEvent>{
                            RecordedEvent::Enter(kDjotDocument),

                            RecordedEvent::Enter(kDjotDiv, "note"),
                            RecordedEvent::Enter(kDjotParagraph),
                            RecordedEvent::Text("Use "),
                            RecordedEvent::Enter(kDjotCodeInline),
                            RecordedEvent::Text(".parse()"),
                            RecordedEvent::Exit(kDjotCodeInline),
                            RecordedEvent::Text("."),
                            RecordedEvent::Exit(kDjotParagraph),
                            RecordedEvent::Exit(kDjotDiv, "note"),

                            RecordedEvent::Enter(kDjotCodeBlock, "cpp"),
                            RecordedEvent::Text("int x = 1;"),
                            RecordedEvent::Text("\n"),
                            RecordedEvent::Exit(kDjotCodeBlock, "cpp"),

                            RecordedEvent::Exit(kDjotDocument),
                        }));
}

TEST_F(DjotParserTest, BulletListWithContinuationsAndLinks) {
  const std::string_view doc =
      "- First item\n"
      "  continued here\n"
      "- See [Docs](https://example.org)\n";

  EXPECT_EQ(parse(doc), (std::vector<RecordedEvent>{
                            RecordedEvent::Enter(kDjotDocument),

                            RecordedEvent::Enter(kDjotList),
                            RecordedEvent::Enter(kDjotListItem),
                            RecordedEvent::Text("First item"),
                            RecordedEvent::Text(" "),
                            RecordedEvent::Text("continued here"),
                            RecordedEvent::Exit(kDjotListItem),

                            RecordedEvent::Enter(kDjotListItem),
                            RecordedEvent::Text("See "),
                            RecordedEvent::Enter(kDjotLink, "https://example.org"),
                            RecordedEvent::Text("Docs"),
                            RecordedEvent::Exit(kDjotLink, "https://example.org"),
                            RecordedEvent::Exit(kDjotListItem),
                            RecordedEvent::Exit(kDjotList),

                            RecordedEvent::Exit(kDjotDocument),
                        }));
}

TEST_F(DjotParserTest, EscapedDelimitersStayLiteral) {
  const std::string_view doc = R"(\*not bold\* and \[not span\]{.ok})";

  EXPECT_EQ(parse(doc), (std::vector<RecordedEvent>{
                            RecordedEvent::Enter(kDjotDocument),
                            RecordedEvent::Enter(kDjotParagraph),
                            RecordedEvent::Text("*"),
                            RecordedEvent::Text("not bold"),
                            RecordedEvent::Text("*"),
                            RecordedEvent::Text(" and "),
                            RecordedEvent::Text("["),
                            RecordedEvent::Text("not span"),
                            RecordedEvent::Text("]"),
                            RecordedEvent::Text("{.ok}"),
                            RecordedEvent::Exit(kDjotParagraph),
                            RecordedEvent::Exit(kDjotDocument),
                        }));
}

TEST_F(DjotParserTest, NullCallbacksDoNotCrash) {
  DjotParserConfig cfg{};  // All function pointers nullptr
  const std::string_view doc = "# Title\n\nBody *text*";
  EXPECT_NO_FATAL_FAILURE(DjotParse(doc.data(), doc.size(), &cfg, nullptr));
}

}  // namespace
