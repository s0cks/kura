#ifndef DJOT_H
#define DJOT_H

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

#define FOR_EACH_DJOT_EVENT(V) \
  V(Document)                  \
  V(Heading)                   \
  V(Break)                     \
  V(Paragraph)                 \
  V(List)                      \
  V(ListItem)                  \
  V(CodeBlock)                 \
  V(CodeInline)                \
  V(Div)                       \
  V(SoftBreak)                 \
  V(HardBreak)                 \
  V(Span)                      \
  V(Link)                      \
  V(Strong)                    \
  V(Emphasis)

typedef enum {
#define DEFINE_KIND(Name) kDjot##Name,
  FOR_EACH_DJOT_EVENT(DEFINE_KIND)
#undef DEFINE_KIND
} DjotEventKind;

typedef struct {
  DjotEventKind kind;
  int level;
  const char* arg;
  size_t arg_len;
} DjotEvent;

typedef struct {
  void (*on_enter)(DjotEvent* event, void* data);
  void (*on_exit)(DjotEvent* event, void* data);
  void (*on_text)(const char* text, size_t len, void* data);
} DjotParserConfig;

bool DjotParse(const char* doc, const size_t doc_len, const DjotParserConfig* config, void* data);

#ifdef __cplusplus
};
#endif  // __cplusplus

#endif  // DJOT_H
