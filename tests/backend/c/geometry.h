#ifndef GEOMETRY_H
#define GEOMETRY_H

#include <stdbool.h>
#include <stdint.h>

#define GEOMETRY_VERSION 3
#define GEOMETRY_SCALE 0.5
#define GEOMETRY_NAME "geometry"
#define GEOMETRY_LIMIT (-4)

typedef struct geometry_canvas geometry_canvas;

typedef enum { SHAPE_CIRCLE = 1, SHAPE_SQUARE = 2, SHAPE_TRIANGLE = 3 } shape_kind;

geometry_canvas* canvas_new(int32_t width, int32_t height);
void canvas_free(geometry_canvas* canvas);
int32_t canvas_area(const geometry_canvas* canvas);
bool canvas_contains(const geometry_canvas* canvas, double x, double y);
void canvas_set_label(geometry_canvas* canvas, const char* label);
const char* canvas_label(const geometry_canvas* canvas);
uint8_t shape_sides(shape_kind kind);
float shape_ratio(float a, float b);
uint64_t counter_next(void);
typedef struct {
  float x;
  float y;
} vec2;

typedef struct {
  vec2 origin;
  vec2 size;
  int32_t id;
  const char* tag;
} rect;

typedef struct {
  double x;
  double y;
  double z;
  uint8_t flag;
} big3;

vec2 vec2_add(vec2 a, vec2 b);
float vec2_length(vec2 v);
rect rect_make(float x, float y, float w, float h, int32_t id);
double rect_area(rect r);
rect rect_grow(rect r, double by);
const char* rect_tag(rect r);
big3 big3_scale(big3 v, double k);
void vec2_print_sum(vec2 a, vec2 b);

static inline int32_t clamp_i(int32_t v, int32_t lo, int32_t hi) {
  return v < lo ? lo : v > hi ? hi : v;
}

int32_t apply_twice(int32_t (*f)(int32_t), int32_t x);
double integrate(double (*f)(double), double a, double b, int32_t steps);
typedef uint8_t (*predicate)(int32_t);
int32_t count_matching(predicate test, int32_t from, int32_t to);

void canvas_fill(geometry_canvas* canvas, uint8_t value, double* unsupported_out);

#endif
