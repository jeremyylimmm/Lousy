#include <stdio.h>

#include "utility.h"

int main() {
  Arena* arena = new_arena();

  const char* path = "test/test.c";
  char* contents = load_text_file(arena, path);

  if (contents) {
    printf("%s\n", contents);
  }
  else {
    printf("Failed to load '%s'\n", path);
  }

  return 0;
}