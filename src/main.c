#include <stdio.h>

#include "utility.h"

int main() {
  Arena* arena = new_arena();

  int n = 1000;
  int** arr = arena_push(arena, n * sizeof(int*));

  for (int i = 0; i < n; ++i) {
    arr[i] = arena_push(arena, sizeof(int));
    *arr[i] = i;
  }

  for (int i = 0; i < n; ++i) {
    printf("%d\n", *arr[i]);
  }

  return 0;
}