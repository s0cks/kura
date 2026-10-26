#include "inlined.h"

int64_t triple(int64_t value) {
  return value * 3;
}

pair pair_swap(pair p) {
  pair r = {p.b, p.a};
  return r;
}

int32_t pair_sum(pair p) {
  return p.a + p.b;
}

const char* greeting(void) {
  return "from C";
}
