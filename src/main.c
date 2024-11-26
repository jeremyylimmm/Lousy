#include <stdio.h>

#include "utility.h"
#include "front/front.h"
#include "spindle/spindle.h"

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

  SB_Context* sb_context = sb_init();
  SB_Func* sb_func = sb_begin_func(sb_context);

  SB_Node* start = sb_node_start(sb_func);
  SB_Node* start_ctrl = sb_node_start_ctrl(sb_func, start);
  SB_Node* start_mem = sb_node_start_mem(sb_func, start);

  SB_Node* constant = sb_node_constant(sb_func, 69);

  sb_node_end(sb_func, start_ctrl, start_mem, constant);

  sb_finish_func(sb_func);

  sb_graphviz_func(stdout, sb_func);

  return 0;
}