#include <stdlib.h>

#include "utility.h"

#define INITIAL_CAP 8

typedef struct {
  int length;
  int capacity;
} Header;

static int grow_capacity(int c) {
  return c ? c * 2 : 8;
}

static Header* hdr(void* vec) {
  return ptr_byte_add(vec, -(int64_t)sizeof(Header));
}

void* _vec_put(void* vec, size_t stride) {
  Header* h;

  if (vec) {
    h = hdr(vec);
  }
  else {
    h = malloc(sizeof(Header) + INITIAL_CAP * stride);
    h->length = 0;
    h->capacity = INITIAL_CAP;
  }

  if (h->length == h->capacity) {
    h->capacity = grow_capacity(h->capacity);
    h = realloc(h, sizeof(Header) + h->capacity * stride);
  }

  h->length++;

  return ptr_byte_add(h, sizeof(Header));
}

void vec_free(void* vec) {
  if (vec) {
    free(hdr(vec));
  }
}

int vec_len(void* vec) {
  if (vec) {
    return hdr(vec)->length;
  }
  else {
    return 0;
  }
}

int _vec_pop(void* vec) {
  Header* h = hdr(vec);
  assert(h->length);
  return --h->length;
}

void* _vec_bake(Arena* arena, void* vec, size_t stride) {
  size_t sz = vec_len(vec) * stride;
  void* data = arena_push(arena, sz);

  memcpy(data, vec, sz);

  vec_free(vec);

  return data;
}