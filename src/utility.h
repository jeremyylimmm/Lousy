#pragma once

#include <stdint.h>
#include <assert.h>
#include <memory.h>
#include <stdbool.h>

typedef struct Arena Arena;

Arena* new_arena();
void free_arena(Arena* arena);

void* arena_push(Arena* arena, size_t amount);
void* arena_zeroed(Arena* arena, size_t amount);

inline void* ptr_byte_add(void* ptr, int64_t offset) {
  return (uint8_t*)ptr + offset;
}

char* load_text_file(Arena* arena, const char* path);