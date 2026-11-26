#ifndef KURA_DOCS_RENDERER_H
#define KURA_DOCS_RENDERER_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <jansson.h>
#include <ostream>
#include <rang.hpp>
#include <sstream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#define popen  _popen
#define pclose _pclose
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

#include "djot.h"
#include "docs/index.h"

namespace kura::docs {

inline size_t detect_terminal_width() {
  if (const char* cols = std::getenv("COLUMNS")) {
    int val = std::atoi(cols);
    if (val >= 40)
      return std::min<size_t>(static_cast<size_t>(val), 100);
  }
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    size_t w = static_cast<size_t>(csbi.srWindow.Right - csbi.srWindow.Left + 1);
    if (w >= 40)
      return std::min<size_t>(w, 100);
  }
#else
  struct winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 40) {
    return std::min<size_t>(ws.ws_col, 100);
  }
#endif
  return 80;
}

struct RenderOptions {
  rang::control color = rang::control::Auto;
  size_t max_width = detect_terminal_width();
  size_t tab_width = 2;
  size_t base_indent = 1;
};

class TerminalRenderer {
 public:
  explicit TerminalRenderer(std::ostream& out = std::cout, RenderOptions options = {}) :
    out_(out),
    opts_(options) {}

  void render(std::string_view djot, json_t* saved_diag = nullptr) {
    rang::setControlMode(opts_.color);
    out_ << '\n';
    render_fragment(djot, saved_diag);
    out_.flush();
  }

  void render_fragment(std::string_view djot, json_t* saved_diag = nullptr) {
    rang::setControlMode(opts_.color);
    saved_diag_ = saved_diag;
    rendered_source_ = false;
    at_line_start_ = true;
    col_ = 0;

    DjotParserConfig handler{&TerminalRenderer::on_enter_cb, &TerminalRenderer::on_exit_cb,
                             &TerminalRenderer::on_text_cb};
    DjotParse(djot.data(), djot.size(), &handler, this);

    // Fallback: if this .dj file doesn't have `::: source`, still show the snippet
    if (saved_diag_ && !rendered_source_) {
      render_source_snippet();
    }
    saved_diag_ = nullptr;
  }

  void render_separator() {
    write_spaces(opts_.base_indent);
    const size_t rule_len =
        (opts_.max_width > opts_.base_indent + 2) ? std::min<size_t>(opts_.max_width - opts_.base_indent - 2, 72) : 40;
    out_ << rang::style::dim;
    for (size_t i = 0; i < rule_len; ++i) {
      out_ << "─";
    }
    out_ << rang::style::reset << "\n\n";
    at_line_start_ = true;
    col_ = 0;
  }

  void render_sub_separator() {
    write_spaces(opts_.base_indent);
    const size_t rule_len =
        (opts_.max_width > opts_.base_indent + 2) ? std::min<size_t>(opts_.max_width - opts_.base_indent - 2, 72) : 40;
    out_ << rang::style::dim;
    for (size_t i = 0; i < rule_len; ++i) {
      out_ << "┄";
    }
    out_ << rang::style::reset << "\n\n";
    at_line_start_ = true;
    col_ = 0;
  }

 private:
  enum class ActiveStyle {
    None,
    Heading,
    SpanExpected,
    SpanFound,
    SpanHint,
    SpanDefault,
    Link,
    LinkUrl,
    CodeInline,
    Strong,
    Emphasis,
    CodeBlock,
  };

  json_t* saved_diag_ = nullptr;
  bool in_source_div_ = false;
  bool rendered_source_ = false;
  bool rendered_fix_ = false;
  std::ostream& out_;
  RenderOptions opts_;
  std::string code_lang_{};
  std::string code_line_{};

  bool in_div_ = false;
  bool in_callout_ = false;
  bool in_list_ = false;
  bool in_item_ = false;
  bool in_span_ = false;
  bool in_code_block_ = false;
  bool at_line_start_ = true;
  size_t col_ = 0;

  ActiveStyle active_style_ = ActiveStyle::None;
  std::string link_url_;
  std::ostringstream word_buf_;
  size_t word_visible_len_ = 0;

  static ActiveStyle map_span_style(std::string_view cls) {
    if (cls == ".expected" || cls == ".ok")
      return ActiveStyle::SpanExpected;
    if (cls == ".found" || cls == ".err")
      return ActiveStyle::SpanFound;
    if (cls == ".hint" || cls == ".note")
      return ActiveStyle::SpanHint;
    return ActiveStyle::SpanDefault;
  }

  static bool is_kura_keyword(std::string_view word) {
    static constexpr std::string_view kKeywords[] = {
        "fn",   "let",   "type", "use", "extern", "set", "test", "assert", "static-assert", "if",  "else",
        "when", "match", "for",  "in",  "fold",   "is",  "as",   "true",   "false",         "none"};
    for (auto kw : kKeywords) {
      if (word == kw)
        return true;
    }
    return false;
  }

  static bool is_ident_char(char c) {
    unsigned char uc = static_cast<unsigned char>(c);
    return std::isalnum(uc) || c == '_' || c == '-';
  }

  void write_highlighted_kura_line(std::string_view line) {
    size_t i = 0;
    const size_t n = line.size();

    while (i < n) {
      // 1. Line comment: // ...
      if (line[i] == '/' && i + 1 < n && line[i + 1] == '/') {
        out_ << rang::style::dim << rang::fg::gray << line.substr(i) << rang::style::reset << rang::fg::reset;
        break;
      }

      // 2. String literal: "..."
      if (line[i] == '"') {
        size_t start = i++;
        while (i < n) {
          if (line[i] == '\\' && i + 1 < n) {
            i += 2;
          } else if (line[i] == '"') {
            i++;
            break;
          } else {
            i++;
          }
        }
        out_ << rang::fg::green << line.substr(start, i - start) << rang::fg::reset;
        continue;
      }

      // 3. UI Element tag: #button, #text
      if (line[i] == '#' && i + 1 < n && std::isalpha(static_cast<unsigned char>(line[i + 1]))) {
        size_t start = i++;
        while (i < n && is_ident_char(line[i]))
          i++;
        out_ << rang::style::bold << rang::fg::blue << line.substr(start, i - start) << rang::style::reset
             << rang::fg::reset;
        continue;
      }

      // 4. UI Element ID: :my-button (not preceded by an identifier char like `fill:`)
      if (line[i] == ':' && (i == 0 || !is_ident_char(line[i - 1])) && i + 1 < n &&
          std::isalpha(static_cast<unsigned char>(line[i + 1]))) {
        size_t start = i++;
        while (i < n && is_ident_char(line[i]))
          i++;
        out_ << rang::fg::yellow << line.substr(start, i - start) << rang::fg::reset;
        continue;
      }

      // 5. Numbers & unit literals: 0, 10px, 50%, 1fr
      if (std::isdigit(static_cast<unsigned char>(line[i]))) {
        size_t start = i++;
        while (i < n && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '.' || line[i] == '%')) {
          i++;
        }
        out_ << rang::fg::yellow << line.substr(start, i - start) << rang::fg::reset;
        continue;
      }

      // 6. Identifiers, Keywords, Property keys (`name:`), and PascalCase Types
      if (std::isalpha(static_cast<unsigned char>(line[i])) || line[i] == '_') {
        size_t start = i++;
        while (i < n && is_ident_char(line[i]))
          i++;
        std::string_view word = line.substr(start, i - start);

        if (i < n && line[i] == ':') {
          // Property key (e.g. `on-click:`)
          out_ << rang::fgB::gray << word << rang::fg::reset;
        } else if (is_kura_keyword(word)) {
          out_ << rang::fg::magenta << word << rang::fg::reset;
        } else if (std::isupper(static_cast<unsigned char>(word[i]))) {
          // PascalCase type or variant (e.g. `MyMessage`, `String`)
          out_ << rang::fg::cyan << word << rang::fg::reset;
        } else {
          out_ << word;
        }
        continue;
      }

      out_ << line[i++];
    }
  }

  static void emit_style(std::ostream& os, ActiveStyle style) {
    switch (style) {
      case ActiveStyle::None:
        break;
      case ActiveStyle::Heading:
        os << rang::style::bold << rang::fgB::gray;
        break;
      case ActiveStyle::SpanExpected:
        os << rang::style::bold << rang::fg::green;
        break;
      case ActiveStyle::SpanFound:
        os << rang::style::bold << rang::fg::red;
        break;
      case ActiveStyle::SpanHint:
        os << rang::style::bold << rang::fg::yellow;
        break;
      case ActiveStyle::SpanDefault:
        os << rang::fg::cyan;
        break;
      case ActiveStyle::Link:
        os << rang::style::underline << rang::fg::cyan;
        break;
      case ActiveStyle::LinkUrl:
        os << rang::style::dim;
        break;
      case ActiveStyle::CodeInline:
        os << rang::fg::cyan;
        break;
      case ActiveStyle::Strong:
        os << rang::style::bold;
        break;
      case ActiveStyle::Emphasis:
        os << rang::style::italic;
        break;
      case ActiveStyle::CodeBlock:
        os << rang::fg::gray;
        break;
    }
  }

  void paint_severity(std::string_view severity, std::string_view text) {
    if (severity == "WARNING") {
      out_ << rang::style::bold << rang::fg::yellow << text << rang::style::reset << rang::fg::reset;
    } else if (severity == "NOTE" || severity == "HELP") {
      out_ << rang::style::bold << rang::fg::cyan << text << rang::style::reset << rang::fg::reset;
    } else {
      out_ << rang::style::bold << rang::fg::red << text << rang::style::reset << rang::fg::reset;
    }
  }

  void write_callout_rail() {
    write_spaces(opts_.base_indent);
    out_ << rang::style::dim << "│ " << rang::style::reset;
  }

  void render_callout_top(std::string_view title = "Your Code") {
    write_spaces(opts_.base_indent);
    const size_t total_w =
        (opts_.max_width > opts_.base_indent + 2) ? std::min<size_t>(opts_.max_width - opts_.base_indent - 2, 72) : 40;
    // "╭─ " (3) + title + " " (1)
    const size_t used = 4 + title.size();
    const size_t fill = (total_w > used) ? (total_w - used) : 4;

    out_ << rang::style::dim << "╭─ " << rang::style::reset << rang::style::bold << title << rang::style::reset
         << rang::style::dim << ' ';
    for (size_t i = 0; i < fill; ++i)
      out_ << "─";
    out_ << rang::style::reset << '\n';
  }

  void render_callout_bottom() {
    write_spaces(opts_.base_indent);
    const size_t total_w =
        (opts_.max_width > opts_.base_indent + 2) ? std::min<size_t>(opts_.max_width - opts_.base_indent - 2, 72) : 40;

    out_ << rang::style::dim << "╰";
    for (size_t i = 1; i < total_w; ++i)
      out_ << "─";
    out_ << rang::style::reset << "\n\n";
    at_line_start_ = true;
    col_ = 0;
  }

  void write_snippet_indent() {
    if (in_callout_) {
      write_callout_rail();
    } else {
      write_spaces(opts_.base_indent);
    }
  }

  void render_fix_snippet() {
    rendered_fix_ = true;
    if (!saved_diag_)
      return;

    json_t* j_fix_lines = json_object_get(saved_diag_, "fix_lines");
    if (!j_fix_lines || !json_is_array(j_fix_lines) || json_array_size(j_fix_lines) == 0)
      return;

    const size_t gutter_width = static_cast<size_t>(json_integer_value(json_object_get(saved_diag_, "gutter_width")));
    const std::string gutter(gutter_width, ' ');

    write_snippet_indent();
    out_ << rang::style::bold << rang::fg::green << "Fix:" << rang::style::reset << rang::fg::reset << '\n';
    write_snippet_indent();
    out_ << '\n';

    size_t idx = 0;
    json_t* j_fline = nullptr;
    json_array_foreach(j_fix_lines, idx, j_fline) {
      const std::string number = json_string_value(json_object_get(j_fline, "number"));
      const char* text = json_string_value(json_object_get(j_fline, "text"));

      write_snippet_indent();
      write_spaces(gutter.size() - number.size());
      out_ << rang::fg::green << number << " +" << rang::fg::reset << ' ';
      if (text) {
        write_highlighted_kura_line(text);
      }
      out_ << '\n';
    }

    at_line_start_ = true;
    col_ = 0;
  }

  void render_source_snippet() {
    rendered_source_ = true;
    if (!saved_diag_)
      return;

    const char* sev_c = json_string_value(json_object_get(saved_diag_, "severity"));
    const char* code = json_string_value(json_object_get(saved_diag_, "code"));
    const char* file = json_string_value(json_object_get(saved_diag_, "filename"));
    const char* message = json_string_value(json_object_get(saved_diag_, "message"));
    const char* help = json_string_value(json_object_get(saved_diag_, "help"));
    const auto header_line = json_integer_value(json_object_get(saved_diag_, "line"));
    const auto header_col = json_integer_value(json_object_get(saved_diag_, "character"));
    const size_t gutter_width = static_cast<size_t>(json_integer_value(json_object_get(saved_diag_, "gutter_width")));

    const std::string_view severity = sev_c ? sev_c : "ERROR";
    const std::string gutter(gutter_width, ' ');

    write_snippet_indent();
    paint_severity(severity, std::string(severity) + "[" + (code ? code : "") + "]");
    out_ << ' ' << rang::style::bold << (message ? message : "") << rang::style::reset << '\n';

    // Location: --> examples/counter.kui:89:9
    write_snippet_indent();
    if (gutter_width >= 2)
      write_spaces(gutter_width - 2);
    out_ << rang::fg::magenta << "--> " << rang::fg::reset << (file ? file : "<unknown>") << ':' << header_line << ':'
         << header_col << '\n';

    write_snippet_indent();
    out_ << gutter << rang::fg::magenta << " |" << rang::fg::reset << '\n';

    // Pre-sorted marks & carets
    size_t idx = 0;
    json_t* j_mark = nullptr;
    json_array_foreach(json_object_get(saved_diag_, "marks"), idx, j_mark) {
      if (json_is_true(json_object_get(j_mark, "has_gap"))) {
        write_snippet_indent();
        out_ << gutter << rang::fg::magenta << " |" << rang::fg::reset
             << (json_is_true(json_object_get(j_mark, "skipped_lines")) ? " ..." : "") << '\n';
      }

      if (!json_is_true(json_object_get(j_mark, "same_line"))) {
        const std::string number = json_string_value(json_object_get(j_mark, "number"));
        const char* line_text = json_string_value(json_object_get(j_mark, "line_text"));
        write_snippet_indent();
        write_spaces(gutter.size() - number.size());
        out_ << rang::fg::magenta << number << " |" << rang::fg::reset << ' ';
        if (line_text) {
          write_highlighted_kura_line(line_text);
        }
        out_ << '\n';
      }

      const size_t from = static_cast<size_t>(json_integer_value(json_object_get(j_mark, "from")));
      const char* marker = json_string_value(json_object_get(j_mark, "marker"));
      const char* text = json_string_value(json_object_get(j_mark, "text"));
      const bool primary = json_is_true(json_object_get(j_mark, "primary"));

      write_snippet_indent();
      out_ << gutter << rang::fg::magenta << " |" << rang::fg::reset << ' ' << std::string(from, ' ');

      if (primary) {
        paint_severity(severity, marker ? marker : "^");
      } else {
        out_ << rang::fg::magenta << (marker ? marker : "-") << rang::fg::reset;
      }

      if (text && text[0] != '\0') {
        out_ << ' ';
        if (primary) {
          paint_severity(severity, text);
        } else {
          out_ << rang::fg::magenta << text << rang::fg::reset;
        }
      }
      out_ << '\n';
    }

    if (help && help[0] != '\0') {
      write_snippet_indent();
      out_ << gutter << rang::fg::magenta << " = " << rang::fg::reset << rang::style::bold << "help"
           << rang::style::reset << ": " << help << '\n';
    }

    write_snippet_indent();
    out_ << '\n';
    at_line_start_ = true;
    col_ = 0;
  }

  static void emit_reset(std::ostream& os) {
    os << rang::style::reset << rang::fg::reset;
  }

  void apply_style(ActiveStyle style) {
    active_style_ = style;
    if (in_code_block_) {
      emit_style(out_, style);
    } else {
      emit_style(word_buf_, style);
    }
  }

  void reset_style() {
    active_style_ = ActiveStyle::None;
    if (in_code_block_) {
      emit_reset(out_);
    } else {
      emit_reset(word_buf_);
    }
  }

  void write_spaces(size_t count) {
    for (size_t i = 0; i < count; ++i) {
      out_ << ' ';
    }
  }

  size_t current_indent_cols() const {
    size_t cols = opts_.base_indent;
    if (in_div_) {
      // "│ " + 1 tab_width
      cols += opts_.tab_width + 2;
    }
    if (in_code_block_) {
      cols += opts_.tab_width * 2;
    } else if (in_item_) {
      cols += opts_.tab_width * 2;
    }
    return cols;
  }

  void write_line_prefix(bool is_first_item_line = false) {
    write_spaces(opts_.base_indent);
    size_t prefix_cols = opts_.base_indent;

    if (in_div_) {
      write_spaces(opts_.tab_width);
      out_ << rang::style::bold << rang::fg::yellow << "│" << rang::style::reset << rang::fg::reset << ' ';
      prefix_cols += opts_.tab_width + 2;
    }

    if (in_code_block_) {
      write_spaces(opts_.tab_width * 2);
      prefix_cols += opts_.tab_width * 2;
    } else if (in_item_) {
      if (is_first_item_line) {
        write_spaces(opts_.tab_width);
        out_ << rang::fg::cyan << "•" << rang::fg::reset << ' ';
      } else {
        write_spaces(opts_.tab_width + 2);
      }
      prefix_cols += opts_.tab_width + 2;
    }

    col_ = prefix_cols;
    at_line_start_ = false;
  }

  void flush_word(bool trailing_space) {
    const std::string word_str = word_buf_.str();
    if (word_str.empty() && word_visible_len_ == 0 && !trailing_space)
      return;

    if (at_line_start_) {
      write_line_prefix(false);
    }

    const size_t min_wrap_col = current_indent_cols() + 1;
    if (col_ > min_wrap_col && (col_ + word_visible_len_ > opts_.max_width)) {
      out_ << '\n';
      write_line_prefix(false);
      if (active_style_ != ActiveStyle::None) {
        emit_style(out_, active_style_);
      }
    }

    if (!word_str.empty()) {
      out_ << word_str;
      col_ += word_visible_len_;
      word_buf_.str("");
      word_buf_.clear();
      word_visible_len_ = 0;
    }

    if (trailing_space && !at_line_start_) {
      if (col_ + 1 <= opts_.max_width) {
        out_ << ' ';
        col_ += 1;
      } else {
        out_ << '\n';
        at_line_start_ = true;
        col_ = 0;
      }
    }
  }

  void on_enter(DjotEvent* ev) {
    switch (ev->kind) {
      case kDjotHeading:
        apply_style(ActiveStyle::Heading);
        break;
      case kDjotBreak:
        render_sub_separator();
        break;
      case kDjotSpan:
        in_span_ = true;
        apply_style(map_span_style({ev->arg, ev->arg_len}));
        break;
      case kDjotLink:
        link_url_.assign(ev->arg, ev->arg_len);
        apply_style(ActiveStyle::Link);
        break;
      case kDjotCodeInline:
        if (!in_span_)
          apply_style(ActiveStyle::CodeInline);
        break;
      case kDjotStrong:
        apply_style(ActiveStyle::Strong);
        break;
      case kDjotEmphasis:
        apply_style(ActiveStyle::Emphasis);
        break;
      case kDjotDiv: {
        std::string_view div_cls(ev->arg, ev->arg_len);
        if (div_cls == "callout") {
          in_callout_ = true;
          if (saved_diag_) {
            render_callout_top();
          }
          break;
        }
        if (div_cls == "source") {
          in_source_div_ = true;
          render_source_snippet();
          break;
        }
        if (div_cls == "fix") {
          in_source_div_ = true;
          render_fix_snippet();
          break;
        }

        in_div_ = true;
        if (ev->arg_len > 0) {
          write_spaces(opts_.base_indent + opts_.tab_width);
          out_ << rang::style::bold << rang::fg::yellow << "│ " << div_cls << ':' << rang::style::reset
               << rang::fg::reset << '\n';
        }
        at_line_start_ = true;
        break;
      }
      case kDjotList:
        in_list_ = true;
        break;
      case kDjotListItem:
        in_item_ = true;
        write_line_prefix(true);
        break;
      case kDjotCodeBlock:
        code_lang_.assign(ev->arg, ev->arg_len);
        code_line_.clear();
        in_code_block_ = true;
        apply_style(ActiveStyle::CodeBlock);
        at_line_start_ = true;
        break;
      default:
        break;
    }
  }

  void on_exit(DjotEvent* ev) {
    switch (ev->kind) {
      case kDjotHeading:
      case kDjotParagraph:
        flush_word(false);
        reset_style();
        out_ << "\n\n";
        at_line_start_ = true;
        col_ = 0;
        break;
      case kDjotListItem:
        flush_word(false);
        reset_style();
        out_ << '\n';
        in_item_ = false;
        at_line_start_ = true;
        col_ = 0;
        break;
      case kDjotList:
        in_list_ = false;
        out_ << '\n';
        break;
      case kDjotSpan:
        in_span_ = false;
        reset_style();
        break;
      case kDjotLink:
        reset_style();
        if (!link_url_.empty()) {
          on_text(" ( ", 3);
          apply_style(ActiveStyle::LinkUrl);
          on_text(link_url_.data(), link_url_.size());
          reset_style();
          on_text(" )", 2);
          link_url_.clear();
        }
        break;
      case kDjotCodeInline:
        if (!in_span_)
          reset_style();
        break;
      case kDjotStrong:
      case kDjotEmphasis:
        reset_style();
        break;
      case kDjotDiv:
        if (in_source_div_) {
          in_source_div_ = false;
          break;
        }
        if (in_callout_) {
          in_callout_ = false;
          if (saved_diag_) {
            render_callout_bottom();
          }
          break;
        }

        in_div_ = false;
        out_ << '\n';
        at_line_start_ = true;
        break;
      case kDjotCodeBlock:
        in_code_block_ = false;
        reset_style();
        code_lang_.clear();
        out_ << '\n';
        at_line_start_ = true;
        col_ = 0;
        break;
      default:
        break;
    }
  }

  void on_text(const char* text, size_t len) {
    if (in_code_block_) {
      for (size_t i = 0; i < len; ++i) {
        if (text[i] == '\t') {
          code_line_.append(opts_.tab_width, ' ');
        } else if (text[i] == '\n') {
          write_line_prefix(false);
          if (code_lang_ == "kura") {
            write_highlighted_kura_line(code_line_);
          } else {
            if (active_style_ != ActiveStyle::None) {
              emit_style(out_, active_style_);
            }
            out_ << code_line_;
            emit_reset(out_);
          }
          out_ << '\n';
          code_line_.clear();
          at_line_start_ = true;
        } else {
          code_line_ += text[i];
        }
      }
      return;
    }

    for (size_t i = 0; i < len; ++i) {
      char ch = text[i];
      if (ch == ' ' || ch == '\t') {
        flush_word(true);
      } else {
        word_buf_ << ch;
        if ((static_cast<uint8_t>(ch) & 0xC0) != 0x80) {
          word_visible_len_++;
        }
      }
    }
  }

  static inline void on_enter_cb(DjotEvent* ev, void* ud) {
    static_cast<TerminalRenderer*>(ud)->on_enter(ev);
  }

  static inline void on_exit_cb(DjotEvent* ev, void* ud) {
    static_cast<TerminalRenderer*>(ud)->on_exit(ev);
  }

  static inline void on_text_cb(const char* t, size_t l, void* ud) {
    static_cast<TerminalRenderer*>(ud)->on_text(t, l);
  }
};

inline size_t detect_terminal_height() {
  if (const char* lines = std::getenv("LINES")) {
    int val = std::atoi(lines);
    if (val >= 10)
      return static_cast<size_t>(val);
  }
#if defined(_WIN32)
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    size_t h = static_cast<size_t>(csbi.srWindow.Bottom - csbi.srWindow.Top + 1);
    if (h >= 10)
      return h;
  }
#else
  struct winsize ws{};
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_row >= 10) {
    return ws.ws_row;
  }
#endif
  return 24;
}

inline bool stdout_is_tty() {
  return isatty(fileno(stdout)) != 0;
}

inline void render_with_pager(std::string_view rendered, RenderOptions opts = {}) {
  const bool is_tty = stdout_is_tty();
  const size_t line_count = static_cast<size_t>(std::count(rendered.begin(), rendered.end(), '\n'));
  const size_t term_height = detect_terminal_height();

  if (!is_tty || line_count + 2 < term_height) {
    std::fwrite(rendered.data(), 1, rendered.size(), stdout);
    std::fflush(stdout);
    return;
  }

  const char* pager_env = std::getenv("PAGER");
  if (pager_env && pager_env[0] == '\0') {
    std::fwrite(rendered.data(), 1, rendered.size(), stdout);
    std::fflush(stdout);
    return;
  }

#if defined(_WIN32)
  std::string pager_cmd = pager_env ? pager_env : "more";
#else
  std::string pager_cmd = pager_env ? pager_env : "less -FRX";
  if (pager_env && std::string_view(pager_env) == "less" && !std::getenv("LESS")) {
    pager_cmd = "less -FRX";
  }
#endif

  FILE* pipe = popen(pager_cmd.c_str(), "w");
  if (!pipe) {
    std::fwrite(rendered.data(), 1, rendered.size(), stdout);
    std::fflush(stdout);
    return;
  }

  std::fwrite(rendered.data(), 1, rendered.size(), pipe);
  pclose(pipe);
}

static inline void list_error_codes(RenderOptions opts = {},
                                    std::span<const DocIndexEntry> index = detail::doc_index()) {
  const bool is_tty = stdout_is_tty();
  if (opts.color == rang::control::Auto)
    opts.color = is_tty ? rang::control::Force : rang::control::Off;
  rang::setControlMode(opts.color);

  if (index.empty()) {
    std::cout << "\n No error codes available.\n\n";
    return;
  }

  size_t max_code_len = 0;
  for (const auto& entry : index) {
    max_code_len = std::max(max_code_len, entry.code.size());
  }

  std::ostringstream buf{};
  buf << '\n';

  for (const auto& entry : index) {
    for (size_t i = 0; i < opts.base_indent; ++i) {
      buf << ' ';
    }

    buf << rang::style::bold << rang::fg::cyan << entry.code << rang::style::reset << rang::fg::reset;

    // Pad column to align titles
    const size_t pad = (max_code_len - entry.code.size()) + opts.tab_width;
    for (size_t i = 0; i < pad; ++i) {
      buf << ' ';
    }

    buf << entry.title << '\n';
  }

  buf << '\n';

  const std::string rendered = buf.str();
  const size_t line_count = static_cast<size_t>(std::count(rendered.begin(), rendered.end(), '\n'));
  const size_t term_height = detect_terminal_height();

  if (!is_tty || line_count + 2 < term_height) {
    std::cout << rendered;
    std::cout.flush();
    return;
  }

  const char* pager_env = std::getenv("PAGER");
  if (pager_env && (*pager_env) == '\0') {
    std::cout << rendered;
    std::cout.flush();
    return;
  }

#if defined(_WIN32)
  std::string pager_cmd = pager_env ? pager_env : "more";
#else
  std::string pager_cmd = pager_env ? pager_env : "less -FRX";
  if (pager_env && std::string_view(pager_env) == "less" && !std::getenv("LESS")) {
    pager_cmd = "less -FRX";
  }
#endif

  FILE* pipe = popen(pager_cmd.c_str(), "w");
  if (!pipe) {
    std::cout << rendered;
    std::cout.flush();
    return;
  }

  std::fwrite(rendered.data(), 1, rendered.size(), pipe);
  pclose(pipe);
}

}  // namespace kura::docs

#endif  // KURA_DOCS_RENDERER_H
