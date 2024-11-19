#include "front.h"

typedef struct {
  int stage;
  SemBlock* block;
  ParseNode* node;
} CheckItem;

typedef struct {
  const char* path;
  const char* source;
  Vec(CheckItem) tree_stack;
  Vec(SemVal) val_stack;
} Checker;

static CheckItem make_check_item(SemBlock* block, ParseNode* node) {
  return (CheckItem) {
    .block = block,
    .node = node
  };
}

static SemBlock* new_block(Arena* arena) {
  SemBlock* block = arena_type(arena, SemBlock);
  return block;
}

static void push(Checker* c, CheckItem x) {
  vec_put(c->tree_stack, x);
}

static bool unhandled_check(Checker* c, CheckItem x) {
  error_token(c->path, c->source, x.node->token, "compiler bug: checker hit unexpected node '%s': '%.*s'", parse_node_label[x.node->kind], x.node->token.length, x.node->token.start);
  return false;
}

#define UNHANDLED() return unhandled_check(c, x)

static bool check_INTEGER(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_ADD(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_SUB(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_MUL(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_DIV(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_ASSIGN(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_BLOCK(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_IDENTIFIER(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_TYPENAME(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_LOCAL(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_SYMBOL(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_IF(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_WHILE(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_RETURN(Checker* c, CheckItem x) {
  UNHANDLED();
}

SemFunc* check_tree(Arena* arena, const char* path, const char* source, ParseTree* tree) {
  SemBlock* root = new_block(arena);

  SemFunc* func = NULL;

  Checker c = {
    .path = path,
    .source = source
  };

  push(&c, make_check_item(root, &tree->nodes[tree->num_nodes-1]));

  while (vec_len(c.tree_stack)) {
    CheckItem item = vec_pop(c.tree_stack);

    bool result = true;

    #define X(name, ...) case PARSE_NODE_##name: result = check_##name(&c, item); break;
    switch (item.node->kind) {
      default:
        assert(false);
        break;
      #include "parse_node.def"
    }
    #undef X

    if (!result) {
      goto end;
    }
  }

  end:
  vec_free(c.tree_stack);
  vec_free(c.val_stack);
  return func;
}

void free_sem_func_storage(SemFunc* func) {
  foreach_list(SemBlock, b, func->cfg) {
    vec_free(b->code);
  }
}