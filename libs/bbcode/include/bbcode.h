#ifndef BBCODE_H
#define BBCODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define FOR_EACH_BBCODE_NODE(V) \
  V(Text)                       \
  V(Span)                       \
  V(Color)                      \
  V(Size)                       \
  V(Hyperlink)                  \
  V(Image)                      \
  V(Email)                      \
  V(Quote)                      \
  V(CodeBlock)                  \
  V(List)                       \
  V(ListItem)                   \
  V(Center)                     \
  V(Right)                      \
  V(Spoiler)

typedef enum {
#define DEFINE_KIND(Name) k##Name##Node,
  FOR_EACH_BBCODE_NODE(DEFINE_KIND)
#undef DEFINE_KIND
} bbcode_node_kind;

typedef struct _bbcode_node bbcode_node;

typedef struct {
  const char* start;
  size_t length;
} bbcode_parser_span;

typedef struct {
  bbcode_node* items;
  size_t count;
} bbcode_node_list;

typedef enum {
  kBoldFormat,
  kItalicFormat,
  kUnderlineFormat,
  kStrikethroughFormat,
  kSuperscriptFormat,
  kSubscriptFormat,
} bbcode_span_format;

typedef struct {
  bbcode_span_format format;
  bbcode_node_list children;
} bbcode_span_node;

typedef struct {
  bbcode_parser_span size;
  bbcode_node_list children;
} bbcode_size_node;

typedef struct {
  bbcode_parser_span color;
  bbcode_node_list children;
} bbcode_color_node;

typedef struct {
  bbcode_parser_span url;
  bbcode_node_list children;
} bbcode_hyperlink_node;

typedef struct {
  bbcode_parser_span span;
} bbcode_image_node;

typedef struct {
  bbcode_node_list children;
} bbcode_container_node;

typedef struct {
  bbcode_parser_span lang;
  bbcode_parser_span code;
} bbcode_code_node;

typedef struct {
  bbcode_parser_span attribute;
  bbcode_node_list children;
} bbcode_quote_node;

typedef struct {
  bool ordered;
  bbcode_node_list values;  // Each item is a container or text node
} bbcode_list_node;

struct _bbcode_node {
  bbcode_node_kind kind;
  bbcode_parser_span span;
  union {
    bbcode_parser_span text;
    bbcode_span_node style;
    bbcode_image_node image;
    bbcode_color_node color;
    bbcode_size_node size;
    bbcode_code_node code;
    bbcode_quote_node quote;
    bbcode_list_node list;
    bbcode_hyperlink_node link;
    bbcode_container_node container;  // Center, Right, Spoiler
  };
};

typedef struct {
  const char* data;
  size_t size;
  size_t pos;

  // Simple bump allocator for AST nodes
  bbcode_node* arena;
  size_t arena_cap;
  size_t arena_used;
} bbcode_parser;

void bbcode_parser_init(bbcode_parser* p, const char* data, size_t size, bbcode_node* arena, size_t arena_cap);
bbcode_node_list bbcode_parse(bbcode_parser* p);

#ifdef __cplusplus
}
#endif

#endif  // BBCODE_H
