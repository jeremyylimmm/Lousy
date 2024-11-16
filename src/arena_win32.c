#include <windows.h>
#include <stdio.h>

#include "utility.h"

#define ARENA_CAPACITY ((size_t)8 * 1024 * 1024 * 1024)

struct Arena {
  void* base;
  
  size_t allocated;
  size_t capacity;

  void* next_page;
  size_t page_size;

  void* page_end;
};

Arena* new_arena() {
  Arena* arena = LocalAlloc(LMEM_ZEROINIT, sizeof(Arena));

  SYSTEM_INFO sys;
  GetSystemInfo(&sys);

  arena->page_size = sys.dwPageSize;

  size_t page_count = (ARENA_CAPACITY + arena->page_size - 1) / arena->page_size;
  size_t reserve_size = page_count * arena->page_size;

  arena->base = VirtualAlloc(NULL, reserve_size, MEM_RESERVE, PAGE_NOACCESS);

  if (arena->base == NULL) {
    fprintf(stderr, "Failed to reserve virtual address space for arena.\n");
    exit(1);
  }

  arena->next_page = arena->base;
  arena->page_end = ptr_byte_add(arena->base, reserve_size);

  return arena;
}

void free_arena(Arena* arena) {
  VirtualFree(arena->base, 0, MEM_RELEASE);
  LocalFree(arena);
}

void* arena_push(Arena* arena, size_t amount) {
  size_t offset = (arena->allocated + 7) & (~7);

  while (arena->capacity - offset < amount) {
    if (arena->next_page == arena->page_end) {
      fprintf(stderr, "Arena address space exhausted.\n");
      exit(1);
    }

    printf("Committing a page.\n");

    VirtualAlloc(arena->next_page, arena->page_size, MEM_COMMIT, PAGE_READWRITE);

    arena->next_page = ptr_byte_add(arena->next_page, arena->page_size);
    arena->capacity += arena->page_size;
  }

  void* ptr = ptr_byte_add(arena->base, offset);
  arena->allocated = offset + amount;

  return ptr;
}