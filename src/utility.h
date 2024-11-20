#pragma once

#include <stdint.h>
#include <assert.h>
#include <memory.h>
#include <stdbool.h>

#define ARRAY_LENGTH(x) (sizeof(x)/sizeof((x)[0]))

#define foreach_list(type, it, head) for (type* it = (head); it; it = it->next)

typedef struct Arena Arena;
typedef struct ScratchImpl ScratchImpl;

typedef struct {
  Arena* arena;
  ScratchImpl* impl;
} Scratch;

Arena* new_arena();
void free_arena(Arena* arena);

void* arena_push(Arena* arena, size_t amount);
void* arena_zeroed(Arena* arena, size_t amount);

void init_scratch_arenas();
void free_scratch_arenas();

Scratch scratch_get(int num_conflicts, Arena** conflicts);
void scratch_release(Scratch* scratch);

#define arena_array(arena, type, count) ((type*)arena_zeroed(arena, (count) * sizeof(type)))
#define arena_type(arena, type) ((type*)arena_zeroed(arena, sizeof(type)))

inline void* ptr_byte_add(void* ptr, int64_t offset) {
  return (uint8_t*)ptr + offset;
}

typedef struct {
  char* s;
  size_t len;
} Str;

char* load_text_file(Arena* arena, const char* path);

#define Vec(T) T*

void* _vec_put(void* vec, size_t stride);
void vec_free(void* vec);

int vec_len(void* vec);
int _vec_pop(void* vec);

void* _vec_bake(Arena* arena, void* vec, size_t stride);

#define vec_put(v, x) ( *(void**)(&(v)) = _vec_put(v, sizeof((v)[0])), (v)[vec_len(v)-1] = (x), (void)0 )
#define vec_pop(v) ((v)[_vec_pop(v)])
#define vec_bake(arena, v) (*(void**)(&(v)) = _vec_bake(arena, v, sizeof((v)[0])), v)
#define vec_back(v) (assert(vec_len(v)), &(v)[vec_len(v)-1] )

static size_t bitset_num_u64(size_t num_bits) {
  return (num_bits+63)/64;
}

static bool bitset_get(uint64_t* set, size_t index) {
  return (set[index/64] >> (index%64)) & 1;
}

static void bitset_set(uint64_t* set, size_t index) {
  set[index/64] |= (uint64_t)1 << (index%64);
}

static void bitset_unset(uint64_t* set, size_t index) {
  set[index/64] &= ~((uint64_t)1 << (index%64));
}

inline void init_thread() {
  init_scratch_arenas();
}

inline void cleanup_thread() {
  free_scratch_arenas();
}