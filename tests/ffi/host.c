#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exports.h"

#define CHECK(cond)                                      \
  do {                                                   \
    if (!(cond)) {                                       \
      printf("FAIL line %d: %s\n", __LINE__, #cond);     \
      return 1;                                          \
    }                                                    \
  } while (0)

int main(int argc, char** argv) {
  kura_module_init();
  CHECK(add(2, 3) == 5);
  CHECK(add(-7, 3) == -4);
  CHECK(fabs(scale(2.5, 2.0f) - 5.0) < 1e-9);
  CHECK(is_even(10) && !is_even(7));
  CHECK(strcmp(greet("tazz"), "hello, tazz") == 0);
  CHECK(maybe_name(NULL) == NULL);
  CHECK(strcmp(maybe_name("x"), "x") == 0);
  CHECK(nothing() == NULL);
  CHECK(big() == 5000000000ULL);
  CHECK(damage_for(4) == 40);
  noop();
  if (argc > 1 && strcmp(argv[1], "overflow") == 0) {
    too_big();
    printf("FAIL: too_big returned\n");
    return 1;
  }
  if (argc > 1 && strcmp(argv[1], "byte") == 0) {
    byte_of(300);
    printf("FAIL: byte_of returned\n");
    return 1;
  }
  printf("ok\n");
  return 0;
}
