#include "geometry.h"

#include <stdlib.h>

struct geometry_canvas {
  int32_t width;
  int32_t height;
  const char* label;
};

geometry_canvas* canvas_new(int32_t width, int32_t height) {
  if (width <= 0 || height <= 0)
    return NULL;
  geometry_canvas* canvas = malloc(sizeof(*canvas));
  canvas->width = width;
  canvas->height = height;
  canvas->label = NULL;
  return canvas;
}

void canvas_free(geometry_canvas* canvas) {
  free(canvas);
}

int32_t canvas_area(const geometry_canvas* canvas) {
  return canvas->width * canvas->height;
}

bool canvas_contains(const geometry_canvas* canvas, double x, double y) {
  return x >= 0 && y >= 0 && x < canvas->width && y < canvas->height;
}

void canvas_set_label(geometry_canvas* canvas, const char* label) {
  canvas->label = label;
}

const char* canvas_label(const geometry_canvas* canvas) {
  return canvas->label;
}

uint8_t shape_sides(shape_kind kind) {
  switch (kind) {
    case SHAPE_CIRCLE:
      return 0;
    case SHAPE_SQUARE:
      return 4;
    case SHAPE_TRIANGLE:
      return 3;
  }
  return 255;
}

float shape_ratio(float a, float b) {
  return a / b;
}

uint64_t counter_next(void) {
  static uint64_t counter = 0;
  return ++counter;
}

void canvas_fill(geometry_canvas* canvas, uint8_t value, double* unsupported_out) {
  (void)canvas;
  (void)value;
  (void)unsupported_out;
}

vec2 vec2_add(vec2 a, vec2 b) {
  vec2 r = {a.x + b.x, a.y + b.y};
  return r;
}

float vec2_length(vec2 v) {
  return __builtin_sqrtf(v.x * v.x + v.y * v.y);
}

rect rect_make(float x, float y, float w, float h, int32_t id) {
  rect r = {{x, y}, {w, h}, id, NULL};
  return r;
}

double rect_area(rect r) {
  return (double)r.size.x * (double)r.size.y;
}

rect rect_grow(rect r, double by) {
  r.size.x += (float)by;
  r.size.y += (float)by;
  return r;
}

const char* rect_tag(rect r) {
  return r.tag;
}

big3 big3_scale(big3 v, double k) {
  big3 r = {v.x * k, v.y * k, v.z * k, v.flag};
  return r;
}

void vec2_print_sum(vec2 a, vec2 b) {
  (void)a;
  (void)b;
}

int32_t apply_twice(int32_t (*f)(int32_t), int32_t x) {
  return f(f(x));
}

double integrate(double (*f)(double), double a, double b, int32_t steps) {
  double total = 0;
  double width = (b - a) / steps;
  for (int32_t i = 0; i < steps; i++)
    total += f(a + (i + 0.5) * width) * width;
  return total;
}

int32_t count_matching(predicate test, int32_t from, int32_t to) {
  int32_t count = 0;
  for (int32_t i = from; i < to; i++)
    count += test(i) ? 1 : 0;
  return count;
}
