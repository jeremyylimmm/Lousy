#include <stdio.h>

#include "utility.h"
#include "token.h"

int main() {
  Arena* arena = new_arena();

  const char* path = "test/test.c";
  char* source = load_text_file(arena, path);

  Tokens tokens = lex_source(arena, source);

  for (int i = 0; i < tokens.count; ++i) {
    Token tok = tokens.data[i];
    printf("%d (line %d): '%.*s'\n", tok.kind, tok.line, tok.length, tok.start);
  }

  return 0;
}