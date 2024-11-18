#include <windows.h>
#include <stdio.h>
#include <threads.h>

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

struct ScratchImpl {
  size_t save;
};

static thread_local Arena* scratch_arenas[2];

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

    VirtualAlloc(arena->next_page, arena->page_size, MEM_COMMIT, PAGE_READWRITE);

    arena->next_page = ptr_byte_add(arena->next_page, arena->page_size);
    arena->capacity += arena->page_size;
  }

  void* ptr = ptr_byte_add(arena->base, offset);
  arena->allocated = offset + amount;

  return ptr;
}

void* arena_zeroed(Arena* arena, size_t amount) {
  void* ptr = arena_push(arena, amount);
  memset(ptr, 0, amount);
  return ptr;
}

void init_scratch_arenas() {
  for (int i = 0; i < ARRAY_LENGTH(scratch_arenas); ++i) {
    scratch_arenas[i] = new_arena();
  }
}

void free_scratch_arenas() {
  for (int i = 0; i < ARRAY_LENGTH(scratch_arenas); ++i) {
    free_arena(scratch_arenas[i]);
  }
}

Scratch scratch_get(int num_conflicts, Arena** conflicts) {
  for (int i = 0; i < ARRAY_LENGTH(scratch_arenas); ++i) {
    Arena* arena = scratch_arenas[i];
    assert(arena && "scratch arenas not initialized");

    bool no_conflicts = true;

    for (int j = 0; j < num_conflicts; ++j) {
      if (arena == conflicts[j]) {
        no_conflicts = false;
        break;
      }
    }

    if (no_conflicts) {
      size_t save = arena->allocated;

      ScratchImpl* impl = arena_type(arena, ScratchImpl);
      impl->save = save;

      return (Scratch) {
        .arena = arena,
        .impl = impl
      };
    }
  }

  assert(false && "unable to find non-conflicting scratch arena");
  return (Scratch) {0};
}

void scratch_release(Scratch* scratch) {
  size_t save = scratch->impl->save;
  Arena* arena = scratch->arena;

  assert(save <= arena->allocated);

#if _DEBUG
  memset(ptr_byte_add(arena->base, save), 0, arena->allocated - save);
  memset(scratch, 0, sizeof(*scratch));
#endif

  arena->allocated = save;
}