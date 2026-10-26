#ifndef INLINED_H
#define INLINED_H

#include <stdint.h>

typedef struct {
  int32_t a;
  int32_t b;
} pair;

int64_t triple(int64_t value);
pair pair_swap(pair p);
int32_t pair_sum(pair p);
const char* greeting(void);

#endif
