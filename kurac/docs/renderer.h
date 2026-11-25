#ifndef KURA_DOCS_RENDERER_H
#define KURA_DOCS_RENDERER_H

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
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

  void render(std::string_view djot) {
    rang::setControlMode(opts_.color);

    // Always start with a leading newline
    out_ << '\n';
    at_line_start_ = true;
    col_ = 0;

    DjotParserConfig handler{&TerminalRenderer::on_enter_cb, &TerminalRenderer::on_exit_cb,
                             &TerminalRenderer::on_text_cb};
    DjotParse(djot.data(), djot.size(), &handler, this);
    out_.flush();
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

  std::ostream& out_;
  RenderOptions opts_;

  bool in_div_ = false;
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
      case kDjotDiv:
        in_div_ = true;
        if (ev->arg_len > 0) {
          write_spaces(opts_.base_indent + opts_.tab_width);
          out_ << rang::style::bold << rang::fg::yellow << "│ " << std::string_view(ev->arg, ev->arg_len) << ':'
               << rang::style::reset << rang::fg::reset << '\n';
        }
        at_line_start_ = true;
        break;
      case kDjotList:
        in_list_ = true;
        break;
      case kDjotListItem:
        in_item_ = true;
        write_line_prefix(true);
        break;
      case kDjotCodeBlock:
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
          on_text(" (", 2);
          apply_style(ActiveStyle::LinkUrl);
          on_text(link_url_.data(), link_url_.size());
          reset_style();
          on_text(")", 1);
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
        in_div_ = false;
        out_ << '\n';
        at_line_start_ = true;
        break;
      case kDjotCodeBlock:
        in_code_block_ = false;
        reset_style();
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
        if (at_line_start_) {
          write_line_prefix(false);
          if (active_style_ != ActiveStyle::None) {
            emit_style(out_, active_style_);
          }
        }
        if (text[i] == '\t') {
          write_spaces(opts_.tab_width);
        } else {
          out_ << text[i];
        }
        if (text[i] == '\n') {
          at_line_start_ = true;
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

inline void render_with_pager(std::string_view djot, RenderOptions opts = {}) {
  const bool is_tty = stdout_is_tty();

  if (opts.color == rang::control::Auto)
    opts.color = is_tty ? rang::control::Force : rang::control::Off;

  std::ostringstream buf;
  TerminalRenderer renderer(buf, opts);
  renderer.render(djot);

  const std::string rendered = buf.str();
  const size_t line_count = static_cast<size_t>(std::count(rendered.begin(), rendered.end(), '\n'));
  const size_t term_height = detect_terminal_height();

  if (!is_tty || line_count + 2 < term_height) {
    std::cout << rendered;
    std::cout.flush();
    return;
  }

  const char* pager_env = std::getenv("PAGER");
  if (pager_env && pager_env[0] == '\0') {
    std::cout << rendered;
    std::cout.flush();
    return;
  }

#if defined(_WIN32)
  std::string pager_cmd = pager_env ? pager_env : "more";
#else
  // -F: quit if one screen, -R: pass raw ANSI colors, -X: don't clear screen on exit
  std::string pager_cmd = pager_env ? pager_env : "less -FRX";
  // Ensure `less` receives raw ANSI escapes if user set PAGER=less without -R
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
