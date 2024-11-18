#include <stdio.h>

#include "utility.h"
#include "front/front.h"

int main() {
  init_thread();

  Arena* arena = new_arena();

  const char* path = "test/test.txt";
  char* source = load_text_file(arena, path);

  Tokens tokens = lex_source(arena, source);

  ParseTree* tree = parse(arena, tokens);

  if (!tree) {
    return 1;
  }

  print_parse_tree(tree);

  return 0;
}