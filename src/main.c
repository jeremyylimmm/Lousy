#include <stdio.h>

#include "utility.h"
#include "front/front.h"
#include "spindle/spindle.h"

int main() {
  init_thread();

  Arena* arena = new_arena();

  const char* path = "test/test.txt";
  char* source = load_text_file(arena, path);

  if (!source) {
    fprintf(stderr, "Failed to load '%s'\n", path);
    return 1;
  }

  Tokens tokens = lex_source(arena, source);

  ParseTree* tree = parse(arena, tokens, path, source);

  if (!tree) {
    return 1;
  }

  print_parse_tree(stdout, tree);

  SemFunc* func = check_tree(arena, path, source, tree);

  if (!func) {
    return 1;
  }

  if(!sem_analyze_func(path, source, func)) {
    return 1;
  }

  print_sem_func(stdout, func);

  SB_Context* sb= sb_init();
  SB_Func* sb_func = lower_sem_func(sb, func);

  sb_opt(sb, sb_func);
  sb_graphviz_func(stdout, sb_func);

  return 0;
}