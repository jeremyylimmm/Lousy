#include <stdio.h>

#include "utility.h"
#include "front/front.h"

int main() {
  init_thread();

  Arena* arena = new_arena();

  const char* path = "test/test.txt";
  char* source = load_text_file(arena, path);

  Tokens tokens = lex_source(arena, source);

  ParseTree* tree = parse(arena, tokens, path, source);

  if (!tree) {
    return 1;
  }

  print_parse_tree(tree);

  SemFunc* func = check_tree(arena, path, source, tree);

  if (!func) {
    return 1;
  }

  if(!sem_analyze_func(path, source, func)) {
    return 1;
  }

  print_sem_func(func);

  return 0;
}