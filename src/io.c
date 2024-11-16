#include <stdio.h>

#include "utility.h"

char* load_text_file(Arena* arena, const char* path) {
  FILE* file = fopen(path, "r");

  if (!file) {
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  size_t file_len = ftell(file);
  rewind(file);

  char* buffer = arena_push(arena, file_len + 1);
  size_t end = fread(buffer, 1, file_len, file);
  buffer[end] = '\0';

  fclose(file);

  return buffer;
}