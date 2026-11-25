#include <stddef.h>
#include <string.h>

#include "djot.h"

static inline void djot_emit_text(const DjotParserConfig* h, const char* s, size_t len, void* data) {
  if (len > 0 && h->on_text) {
    h->on_text(s, len, data);
  }
}

static inline void djot_parse_inline(const DjotParserConfig* h, const char* s, size_t len, void* data) {
  size_t i = 0;
  size_t text_start = 0;

  while (i < len) {
    // 1. Backslash escape
    if (s[i] == '\\' && i + 1 < len) {
      djot_emit_text(h, s + text_start, i - text_start, data);
      djot_emit_text(h, s + i + 1, 1, data);
      i += 2;
      text_start = i;
      continue;
    }

    // 2. Bracketed constructs: [content]{.class} OR [label](url)
    if (s[i] == '[') {
      const char* close_b = (const char*)memchr(s + i + 1, ']', len - (i + 1));
      if (close_b && (size_t)(close_b - s + 1) < len) {
        char next_ch = close_b[1];  // <-- [1] index after ']'

        // 2a. Semantic Span: [content]{.class}
        if (next_ch == '{') {
          const char* attr = close_b + 2;
          const char* close_brace = (const char*)memchr(attr, '}', len - (size_t)(attr - s));
          if (close_brace) {
            djot_emit_text(h, s + text_start, i - text_start, data);
            DjotEvent ev = {.kind = kDjotSpan, .level = 0, .arg = attr, .arg_len = (size_t)(close_brace - attr)};
            if (h->on_enter)
              h->on_enter(&ev, data);
            djot_parse_inline(h, s + i + 1, (size_t)(close_b - (s + i + 1)), data);
            if (h->on_exit)
              h->on_exit(&ev, data);

            i = (size_t)(close_brace - s) + 1;
            text_start = i;
            continue;
          }
        }

        // 2b. Link: [label](url)
        if (next_ch == '(') {
          const char* url = close_b + 2;
          const char* close_paren = (const char*)memchr(url, ')', len - (size_t)(url - s));
          if (close_paren) {
            djot_emit_text(h, s + text_start, i - text_start, data);
            DjotEvent ev = {.kind = kDjotLink, .level = 0, .arg = url, .arg_len = (size_t)(close_paren - url)};
            if (h->on_enter)
              h->on_enter(&ev, data);
            djot_parse_inline(h, s + i + 1, (size_t)(close_b - (s + i + 1)), data);
            if (h->on_exit)
              h->on_exit(&ev, data);

            i = (size_t)(close_paren - s) + 1;
            text_start = i;
            continue;
          }
        }
      }
    }

    // 3. Inline Verbatim Code: `code`
    if (s[i] == '`') {
      const char* end = (const char*)memchr(s + i + 1, '`', len - (i + 1));
      if (end) {
        djot_emit_text(h, s + text_start, i - text_start, data);
        DjotEvent ev = {.kind = kDjotCodeInline};
        if (h->on_enter)
          h->on_enter(&ev, data);
        djot_emit_text(h, s + i + 1, (size_t)(end - (s + i + 1)), data);
        if (h->on_exit)
          h->on_exit(&ev, data);

        i = (size_t)(end - s) + 1;
        text_start = i;
        continue;
      }
    }

    // 4. Strong (*text*) & Emphasis (_text_)
    if ((s[i] == '*' || s[i] == '_') && i + 1 < len && s[i + 1] != ' ') {
      char delim = s[i];
      const char* end = (const char*)memchr(s + i + 1, delim, len - (i + 1));
      if (end && end > s + i + 1 && end[-1] != ' ') {
        djot_emit_text(h, s + text_start, i - text_start, data);
        DjotEvent ev = {.kind = (delim == '*') ? kDjotStrong : kDjotEmphasis};
        if (h->on_enter)
          h->on_enter(&ev, data);
        djot_parse_inline(h, s + i + 1, (size_t)(end - (s + i + 1)), data);
        if (h->on_exit)
          h->on_exit(&ev, data);

        i = (size_t)(end - s) + 1;
        text_start = i;
        continue;
      }
    }

    i++;
  }

  djot_emit_text(h, s + text_start, len - text_start, data);
}

static inline void djot_close_inline_blocks(const DjotParserConfig* h, bool* in_para, bool* in_item, bool* in_list,
                                            void* data) {
  DjotEvent para_ev = {.kind = kDjotParagraph};
  DjotEvent item_ev = {.kind = kDjotListItem};
  DjotEvent list_ev = {.kind = kDjotList};

  if (*in_para) {
    if (h->on_exit)
      h->on_exit(&para_ev, data);

    *in_para = false;
  }
  if (*in_item) {
    if (h->on_exit)
      h->on_exit(&item_ev, data);

    *in_item = false;
  }
  if (*in_list) {
    if (h->on_exit)
      h->on_exit(&list_ev, data);

    *in_list = false;
  }
}

bool DjotParse(const char* doc, const size_t doc_len, const DjotParserConfig* h, void* data) {
  DjotEvent doc_ev = {.kind = kDjotDocument};
  DjotEvent code_ev = {.kind = kDjotCodeBlock};
  DjotEvent div_ev = {.kind = kDjotDiv};
  DjotEvent para_ev = {.kind = kDjotParagraph};
  DjotEvent list_ev = {.kind = kDjotList};
  DjotEvent item_ev = {.kind = kDjotListItem};

  if (h->on_enter)
    h->on_enter(&doc_ev, data);

  bool in_code = false;
  bool in_div = false;
  bool in_para = false;
  bool in_list = false;
  bool in_item = false;

  size_t pos = 0;
  while (pos < doc_len) {
    const char* line = doc + pos;
    const char* nl = (const char*)memchr(line, '\n', doc_len - pos);
    size_t len = nl ? (size_t)(nl - line) : (doc_len - pos);
    pos += nl ? (len + 1) : len;

    if (len > 0 && line[len - 1] == '\r')
      len--;

    // 1. Fenced Code Block (```lang)
    if (len >= 3 && strncmp(line, "```", 3) == 0) {
      djot_close_inline_blocks(h, &in_para, &in_item, &in_list, data);
      if (!in_code) {
        in_code = true;
        const char* lang = line + 3;
        size_t lang_len = len - 3;
        while (lang_len > 0 && *lang == ' ') {
          lang++;
          lang_len--;
        }

        code_ev.arg = lang;
        code_ev.arg_len = lang_len;
        if (h->on_enter)
          h->on_enter(&code_ev, data);
      } else {
        in_code = false;
        if (h->on_exit)
          h->on_exit(&code_ev, data);
      }

      continue;
    }

    if (in_code) {
      djot_emit_text(h, line, len, data);
      djot_emit_text(h, "\n", 1, data);
      continue;
    }

    // 2. Div Block (::: note)
    if (len >= 3 && strncmp(line, ":::", 3) == 0) {
      djot_close_inline_blocks(h, &in_para, &in_item, &in_list, data);
      if (!in_div) {
        in_div = true;
        const char* cls = line + 3;
        size_t cls_len = len - 3;
        while (cls_len > 0 && *cls == ' ') {
          cls++;
          cls_len--;
        }

        div_ev.arg = cls;
        div_ev.arg_len = cls_len;
        if (h->on_enter)
          h->on_enter(&div_ev, data);
      } else {
        in_div = false;
        if (h->on_exit)
          h->on_exit(&div_ev, data);
      }

      continue;
    }

    // 3. Blank line closes active paragraph / list
    if (len == 0) {
      djot_close_inline_blocks(h, &in_para, &in_item, &in_list, data);
      continue;
    }

    // 4. Heading (# Title)
    if (line[0] == '#') {
      size_t lvl = 0;
      while (lvl < len && line[lvl] == '#')
        lvl++;
      if (lvl <= 6 && lvl < len && line[lvl] == ' ') {
        djot_close_inline_blocks(h, &in_para, &in_item, &in_list, data);
        DjotEvent head_ev = {.kind = kDjotHeading, .level = (int)lvl};
        if (h->on_enter)
          h->on_enter(&head_ev, data);
        djot_parse_inline(h, line + lvl + 1, len - lvl - 1, data);
        if (h->on_exit)
          h->on_exit(&head_ev, data);
        continue;
      }
    }

    // 5. Bullet List Item (- item or * item)
    if (len >= 2 && (line[0] == '-' || line[0] == '*') && line[1] == ' ') {
      if (in_para) {
        if (h->on_exit)
          h->on_exit(&para_ev, data);
        in_para = false;
      }
      if (in_item) {
        if (h->on_exit)
          h->on_exit(&item_ev, data);
        in_item = false;
      }
      if (!in_list) {
        in_list = true;
        if (h->on_enter)
          h->on_enter(&list_ev, data);
      }
      in_item = true;
      if (h->on_enter)
        h->on_enter(&item_ev, data);
      djot_parse_inline(h, line + 2, len - 2, data);
      continue;
    }

    // 6. List item continuation line (indented by 2+ spaces while in_item)
    if (in_item && len >= 2 && line[0] == ' ' && line[1] == ' ') {
      const char* trimmed = line;
      size_t trimmed_len = len;
      while (trimmed_len > 0 && *trimmed == ' ') {
        trimmed++;
        trimmed_len--;
      }
      djot_emit_text(h, " ", 1, data);
      djot_parse_inline(h, trimmed, trimmed_len, data);
      continue;
    }

    // 7. Regular Paragraph Line
    if (in_item || in_list)
      djot_close_inline_blocks(h, &in_para, &in_item, &in_list, data);

    if (!in_para) {
      in_para = true;
      if (h->on_enter)
        h->on_enter(&para_ev, data);
    } else {
      djot_emit_text(h, " ", 1, data);
    }

    djot_parse_inline(h, line, len, data);
  }

  djot_close_inline_blocks(h, &in_para, &in_item, &in_list, data);
  if (in_code && h->on_exit)
    h->on_exit(&code_ev, data);

  if (in_div && h->on_exit)
    h->on_exit(&div_ev, data);

  if (h->on_exit)
    h->on_exit(&doc_ev, data);

  return true;
}
