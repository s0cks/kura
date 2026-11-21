#include "bbcode.h"

#include <ctype.h>
#include <string.h>

static bool span_eq_ci(bbcode_parser_span s, const char* lit) {
  size_t n = strlen(lit);
  if (s.length != n)
    return false;
  for (size_t i = 0; i < n; i++) {
    if (tolower((unsigned char)s.start[i]) != tolower((unsigned char)lit[i])) {
      return false;
    }
  }
  return true;
}

static bbcode_node* alloc_node(bbcode_parser* p, bbcode_node_kind kind) {
  if (p->arena_used >= p->arena_cap)
    return NULL;
  bbcode_node* n = &p->arena[p->arena_used++];
  memset(n, 0, sizeof(*n));
  n->kind = kind;
  return n;
}

typedef struct {
  bbcode_parser_span name;
  bbcode_parser_span attr;
  bool is_closing;
  size_t full_start;
  size_t full_end;
} parsed_tag;

static bool try_parse_tag(const bbcode_parser* p, size_t pos, parsed_tag* out) {
  if (pos >= p->size || p->data[pos] != '[')
    return false;
  size_t i = pos + 1;
  out->full_start = pos;
  out->is_closing = false;

  if (i < p->size && p->data[i] == '/') {
    out->is_closing = true;
    i++;
  }

  size_t name_start = i;
  while (i < p->size && (isalnum((unsigned char)p->data[i]) || p->data[i] == '*')) {
    i++;
  }
  if (i == name_start)
    return false;
  out->name = (bbcode_parser_span){p->data + name_start, i - name_start};

  out->attr = (bbcode_parser_span){NULL, 0};
  if (!out->is_closing && i < p->size && p->data[i] == '=') {
    i++;
    if (i < p->size && p->data[i] == '"') {
      i++;
      size_t attr_start = i;
      while (i < p->size && p->data[i] != '"' && p->data[i] != ']')
        i++;
      out->attr = (bbcode_parser_span){p->data + attr_start, i - attr_start};
      if (i < p->size && p->data[i] == '"')
        i++;
    } else {
      size_t attr_start = i;
      while (i < p->size && p->data[i] != ']')
        i++;
      out->attr = (bbcode_parser_span){p->data + attr_start, i - attr_start};
    }
  }

  if (i >= p->size || p->data[i] != ']')
    return false;
  out->full_end = i + 1;
  return true;
}

static bbcode_node_list parse_until(bbcode_parser* p, const char* stop_tag, bool stop_on_list_item);

static bbcode_node* parse_list(bbcode_parser* p, const parsed_tag* open) {
  bbcode_node* node = alloc_node(p, kListNode);
  if (!node)
    return NULL;

  node->list.ordered = (open->attr.length > 0);

  bbcode_node temp_items[128];
  size_t item_count = 0;

  while (p->pos < p->size) {
    parsed_tag tag;
    if (try_parse_tag(p, p->pos, &tag)) {
      if (tag.is_closing && span_eq_ci(tag.name, "list")) {
        p->pos = tag.full_end;
        break;
      }

      if (!tag.is_closing && span_eq_ci(tag.name, "*")) {
        size_t item_start = tag.full_start;
        p->pos = tag.full_end;

        bbcode_node item = {0};
        item.kind = kListItemNode;
        item.container.children = parse_until(p, NULL, true);
        item.span = (bbcode_parser_span){p->data + item_start, p->pos - item_start};

        if (item_count < 128) {
          temp_items[item_count++] = item;
        }
        continue;
      }
    }

    p->pos++;
  }

  if (item_count > 0 && p->arena_used + item_count <= p->arena_cap) {
    node->list.values.items = &p->arena[p->arena_used];
    node->list.values.count = item_count;
    memcpy(node->list.values.items, temp_items, item_count * sizeof(bbcode_node));
    p->arena_used += item_count;
  }

  node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
  return node;
}

static bbcode_node* parse_tag_element(bbcode_parser* p, const parsed_tag* open) {
  p->pos = open->full_end;

  if (span_eq_ci(open->name, "code")) {
    bbcode_node* node = alloc_node(p, kCodeBlockNode);
    if (!node)
      return NULL;
    node->code.lang = open->attr;
    size_t content_start = p->pos;

    while (p->pos < p->size) {
      parsed_tag close;
      if (try_parse_tag(p, p->pos, &close) && close.is_closing && span_eq_ci(close.name, "code")) {
        node->code.code = (bbcode_parser_span){p->data + content_start, p->pos - content_start};
        p->pos = close.full_end;
        node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
        return node;
      }
      p->pos++;
    }
    node->code.code = (bbcode_parser_span){p->data + content_start, p->pos - content_start};
    node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
    return node;
  }

  // [img]url[/img]
  if (span_eq_ci(open->name, "img")) {
    bbcode_node* node = alloc_node(p, kImageNode);
    if (!node)
      return NULL;
    size_t content_start = p->pos;
    while (p->pos < p->size) {
      parsed_tag close;
      if (try_parse_tag(p, p->pos, &close) && close.is_closing && span_eq_ci(close.name, "img")) {
        node->image.span = (bbcode_parser_span){p->data + content_start, p->pos - content_start};
        p->pos = close.full_end;
        node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
        return node;
      }
      p->pos++;
    }
    return node;
  }

  // Inline styles: b, i, u, s, sup, sub
  bbcode_span_format fmt;
  bool is_span = true;
  if (span_eq_ci(open->name, "b"))
    fmt = kBoldFormat;
  else if (span_eq_ci(open->name, "i"))
    fmt = kItalicFormat;
  else if (span_eq_ci(open->name, "u"))
    fmt = kUnderlineFormat;
  else if (span_eq_ci(open->name, "s"))
    fmt = kStrikethroughFormat;
  else if (span_eq_ci(open->name, "sup"))
    fmt = kSuperscriptFormat;
  else if (span_eq_ci(open->name, "sub"))
    fmt = kSubscriptFormat;
  else
    is_span = false;

  char tag_buf[32] = {0};
  size_t copy_len = open->name.length < 31 ? open->name.length : 31;
  memcpy(tag_buf, open->name.start, copy_len);

  if (is_span) {
    bbcode_node* node = alloc_node(p, kSpanNode);
    if (!node)
      return NULL;
    node->style.format = fmt;
    node->style.children = parse_until(p, tag_buf, false);
    node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
    return node;
  }

  if (span_eq_ci(open->name, "color")) {
    bbcode_node* node = alloc_node(p, kColorNode);
    if (!node)
      return NULL;
    node->color.color = open->attr;
    node->color.children = parse_until(p, "color", false);
    node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
    return node;
  }

  if (span_eq_ci(open->name, "size")) {
    bbcode_node* node = alloc_node(p, kSizeNode);
    if (!node)
      return NULL;
    node->size.size = open->attr;
    node->size.children = parse_until(p, "size", false);
    node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
    return node;
  }

  if (span_eq_ci(open->name, "url") || span_eq_ci(open->name, "email")) {
    bbcode_node* node = alloc_node(p, span_eq_ci(open->name, "url") ? kHyperlinkNode : kEmailNode);
    if (!node)
      return NULL;
    node->link.url = open->attr;
    node->link.children = parse_until(p, tag_buf, false);
    if (node->link.url.length == 0 && node->link.children.count == 1 &&
        node->link.children.items[0].kind == kTextNode) {
      node->link.url = node->link.children.items[0].text;
    }
    node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
    return node;
  }

  if (span_eq_ci(open->name, "quote")) {
    bbcode_node* node = alloc_node(p, kQuoteNode);
    if (!node)
      return NULL;
    node->quote.attribute = open->attr;
    node->quote.children = parse_until(p, "quote", false);
    node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
    return node;
  }

  if (span_eq_ci(open->name, "center") || span_eq_ci(open->name, "right") || span_eq_ci(open->name, "spoiler")) {
    bbcode_node_kind kind = kCenterNode;
    if (span_eq_ci(open->name, "right"))
      kind = kRightNode;
    else if (span_eq_ci(open->name, "spoiler"))
      kind = kSpoilerNode;

    bbcode_node* node = alloc_node(p, kind);
    if (!node)
      return NULL;
    node->container.children = parse_until(p, tag_buf, false);
    node->span = (bbcode_parser_span){p->data + open->full_start, p->pos - open->full_start};
    return node;
  }

  // Unrecognized tag: treat as plain text
  bbcode_node* txt = alloc_node(p, kTextNode);
  if (txt) {
    txt->text = (bbcode_parser_span){p->data + open->full_start, open->full_end - open->full_start};
    txt->span = txt->text;
  }
  return txt;
}

// Updated signature to stop when a sibling [*] or [/list] is encountered
static bbcode_node_list parse_until(bbcode_parser* p, const char* stop_tag, bool stop_on_list_item) {
  bbcode_node temp_siblings[256];
  size_t count = 0;

  while (p->pos < p->size) {
    parsed_tag tag;
    if (try_parse_tag(p, p->pos, &tag)) {
      // Stop for explicit closing tag (e.g., [/b] or [/list])
      if (tag.is_closing && stop_tag && span_eq_ci(tag.name, stop_tag)) {
        p->pos = tag.full_end;
        break;
      }

      // Stop without consuming if we're inside a [*] and hit another [*], [/*], or [/list]
      if (stop_on_list_item) {
        if (span_eq_ci(tag.name, "*") || (tag.is_closing && span_eq_ci(tag.name, "list"))) {
          if (tag.is_closing && span_eq_ci(tag.name, "*")) {
            p->pos = tag.full_end;  // Consume optional [/*]
          }
          break;
        }
      }

      if (!tag.is_closing) {
        bbcode_node* elem = parse_tag_element(p, &tag);
        if (elem && count < 256) {
          temp_siblings[count++] = *elem;
        }
        continue;
      }
    }

    // Consume plain text until next '['
    size_t text_start = p->pos;
    p->pos++;
    while (p->pos < p->size && p->data[p->pos] != '[') {
      p->pos++;
    }

    if (count < 256) {
      bbcode_node txt = {0};
      txt.kind = kTextNode;
      txt.text = (bbcode_parser_span){p->data + text_start, p->pos - text_start};
      txt.span = txt.text;
      temp_siblings[count++] = txt;
    }
  }

  bbcode_node_list result = {NULL, 0};
  if (count > 0 && p->arena_used + count <= p->arena_cap) {
    result.items = &p->arena[p->arena_used];
    result.count = count;
    memcpy(result.items, temp_siblings, count * sizeof(bbcode_node));
    p->arena_used += count;
  }
  return result;
}

void bbcode_parser_init(bbcode_parser* p, const char* data, size_t size, bbcode_node* arena, size_t arena_cap) {
  p->data = data;
  p->size = size;
  p->pos = 0;
  p->arena = arena;
  p->arena_cap = arena_cap;
  p->arena_used = 0;
}

bbcode_node_list bbcode_parse(bbcode_parser* p) {
  return parse_until(p, NULL, false);
}
