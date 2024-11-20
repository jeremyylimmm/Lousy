#include <stdio.h>

#include "front.h"

typedef struct SymbolTableEntry SymbolTableEntry;

struct SymbolTableEntry {
  SymbolTableEntry* next;
  Str name;
  SemPlace place;
};

typedef struct {
  SymbolTableEntry* head;
} SymbolTable; // TODO: convert to a hash-map

typedef struct Scope Scope;
struct Scope {
  Scope* parent;
  SymbolTable locals;
};

typedef struct {
  int stage;
  ParseNode* node;

  union {
    struct {
      int initial_stack_state;
    } block;
  } as;
} CheckItem;

typedef struct {
  Scratch scratch;
  Arena* arena;

  const char* path;
  const char* source;

  Vec(CheckItem) tree_stack;
  Vec(SemPlace) place_stack;
  Vec(SemPlaceData) place_data;

  Scope* current_scope;
  SemBlock* current_block;
} Checker;

static SemPlace find_symbol(Scope* scope, Token identifier) {
  foreach_list(SymbolTableEntry, e, scope->locals.head) {
    if (e->name.len == identifier.length && memcmp(e->name.s, identifier.start, e->name.len) == 0) {
      return e->place;
    }
  }

  if (scope->parent) {
    return find_symbol(scope->parent, identifier);
  }

  return SEM_NULL_PLACE;
}

static void add_local(Checker* c, Token identifier, SemPlace place) {
  SymbolTableEntry* e = arena_type(c->scratch.arena, SymbolTableEntry);

  e->name = (Str) {
    .len = identifier.length,
    .s = identifier.start
  };

  e->place = place;

  e->next = c->current_scope->locals.head;
  c->current_scope->locals.head = e;
}

static CheckItem item(ParseNode* node) {
  return (CheckItem) {
    .stage = 0,
    .node = node
  };
}

static SemBlock* new_block(Checker* c) {
  SemBlock* block = arena_type(c->arena, SemBlock);

  if (c->current_block) {
    c->current_block->next = block;
  }

  c->current_block = block;

  return block;
}

static void push(Checker* c, CheckItem x) {
  vec_put(c->tree_stack, x);
}

static void next_stage(Checker* c, CheckItem x) {
  x.stage++;
  push(c, x);
}

static bool unhandled_check(Checker* c, CheckItem x) {
  error_token(c->path, c->source, x.node->token, "compiler bug: checker hit unexpected node '%s': '%.*s'", parse_node_label[x.node->kind], x.node->token.length, x.node->token.start);
  return false;
}

static SemPlace pop_place(Checker* c) {
  return vec_pop(c->place_stack);
}

static void push_place(Checker* c, SemPlace place) {
  vec_put(c->place_stack, place);
}

static SemPlace new_place(Checker* c) {
  SemPlace place = vec_len(c->place_data);
  vec_put(c->place_data, (SemPlaceData){0});
  return place;
}

static void make_inst_base(Checker* c, SemBlock* block, SemPlace write, SemOp op, int num_reads, void* data) {
  assert(op);
  
  SemInst inst = {
    .op = op,
    .num_reads = num_reads,
    .write = write,
    .data = data
  };

  for (int i = num_reads-1; i >= 0; --i) {
    inst.reads[i] = pop_place(c);
  }

  vec_put(block->code, inst);
}

static void make_inst(Checker* c, bool writes, SemOp op, int num_reads, void* data) {
  SemPlace write = SEM_NULL_PLACE;

  if (writes) {
    write = new_place(c);
    push_place(c, write);
  }

  make_inst_base(c, c->current_block, write, op, num_reads, data);
}

static void get_children(ParseNode* node, ParseNode** children) {
  parse_node_for_each_child(node, it) {
    children[it.index] = it.child;
  }
}

#define UNHANDLED() return unhandled_check(c, x)

static bool check_INTEGER(Checker* c, CheckItem x) {
  uint64_t value = 0;

  for (int i = 0; i < x.node->token.length; ++i) {
    value *= 10;
    value += x.node->token.start[i] - '0';
  }

  make_inst(c, true, SEM_OP_INTEGER_CONST, 0, (void*)value);

  return true;
}

static bool check_binary(Checker* c, CheckItem x, SemOp op) {
  switch (x.stage) {
    default:
      assert(false);
      return false;

    case 0:
      assert(x.node->num_children == 2);
      next_stage(c, x);
      parse_node_for_each_child(x.node, it) {
        push(c, item(it.child));
      }
      return true;

    case 1:
      make_inst(c, true, op, 2, NULL);
      return true;
  }
}

static bool check_ADD(Checker* c, CheckItem x) {
  return check_binary(c, x, SEM_OP_ADD);
}

static bool check_SUB(Checker* c, CheckItem x) {
  return check_binary(c, x, SEM_OP_SUB);
}

static bool check_MUL(Checker* c, CheckItem x) {
  return check_binary(c, x, SEM_OP_MUL);
}

static bool check_DIV(Checker* c, CheckItem x) {
  return check_binary(c, x, SEM_OP_DIV);
}

static bool can_take_address(ParseNodeKind kind) {
  switch (kind) {
    default:
      return false;
    case PARSE_NODE_SYMBOL:
      return true;
  }
}

static bool check_ASSIGN(Checker* c, CheckItem x) {
  switch (x.stage) {
    default:
      assert(false);
      return false;

    case 0:
      assert(x.node->num_children == 2);

      next_stage(c, x);

      parse_node_for_each_child(x.node, it) {
        if (it.index == 0) {
          if (!can_take_address(it.child->kind)) {
            error_token(c->path, c->source, it.child->token, "cannot assign this value");
            return false;
          }
        }

        push(c, item(it.child));
      }

      return true;

    case 1:
      SemPlace value = pop_place(c);
      SemPlace dest = pop_place(c);
      push_place(c, value);
      make_inst_base(c, c->current_block, dest, SEM_OP_COPY, 1, NULL);
      push_place(c, value);
      return true;
  }
}

static bool check_BLOCK(Checker* c, CheckItem x) {
  switch (x.stage) {
    default:
      assert(false);
      return false;

    case 0: {
      Scope* scope = arena_type(c->scratch.arena, Scope);
      scope->parent = c->current_scope;
      c->current_scope = scope;

      x.as.block.initial_stack_state = vec_len(c->place_stack);
      next_stage(c, x);

      parse_node_for_each_child(x.node, it) {
        push(c, item(it.child));
      }

      return true;
    }

    case 1: {
      while (vec_len(c->place_stack) > x.as.block.initial_stack_state) {
        vec_pop(c->place_stack);
      }

      c->current_scope = c->current_scope->parent;

      return true;
    }
  }
}

static bool check_IDENTIFIER(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_TYPENAME(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_LOCAL(Checker* c, CheckItem x) {
  if (x.node->num_children == 2) {
    ParseNode* children[2];
    get_children(x.node, children);

    Token name = children[0]->token;

    if (find_symbol(c->current_scope, name) != SEM_NULL_PLACE) {
      error_token(c->path, c->source, name, "this name clashes with an existing symbol");
      return false;
    }

    add_local(c, name, new_place(c));

    return true;
  }
  else {
    error_token(c->path, c->source, x.node->token, "initializers not implemented yet");
    return false;
  }
}

static bool check_SYMBOL(Checker* c, CheckItem x) {
  SemPlace place = find_symbol(c->current_scope, x.node->token);

  if (place == SEM_NULL_PLACE) {
    error_token(c->path, c->source, x.node->token, "symbol does not exist in this scope");
    return false;
  }

  push_place(c, place);

  return true;
}

static bool check_IF(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_WHILE(Checker* c, CheckItem x) {
  UNHANDLED();
}

static bool check_RETURN(Checker* c, CheckItem x) {
  switch (x.stage) {
    default:
      assert(false);
      return false;
    
    case 0:
      if (x.node->num_children == 0) {
        make_inst(c, false, SEM_OP_RETURN, 0, NULL);
        new_block(c);
      }
      else {
        assert(x.node->num_children == 1);
        next_stage(c, x);

        parse_node_for_each_child(x.node, it) {
          push(c, item(it.child));
        }
      }
      return true;

    case 1:
      make_inst(c, false, SEM_OP_RETURN, 1, NULL);
      new_block(c);
      return true;
  }
}

SemFunc* check_tree(Arena* arena, const char* path, const char* source, ParseTree* tree) {
  SemFunc* func = NULL;

  Checker c = {
    .scratch = scratch_get(1, &arena),
    .arena = arena,
    .path = path,
    .source = source,
  };

  SemBlock* root = new_block(&c);

  push(&c, item(&tree->nodes[tree->num_nodes-1]));

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

  func = arena_type(arena, SemFunc);
  func->cfg = root;
  func->place_data = c.place_data;

  end:
  scratch_release(&c.scratch);
  vec_free(c.tree_stack);
  vec_free(c.place_stack);
  return func;
}

void free_sem_func_storage(SemFunc* func) {
  foreach_list(SemBlock, b, func->cfg) {
    vec_free(b->code);
  }

  vec_free(func->place_data);
}

void print_sem_func(SemFunc* func) {
  int next_block_id = 0;

  foreach_list(SemBlock, b, func->cfg) {
    b->_id = next_block_id++;
  }

  foreach_list(SemBlock, b, func->cfg) {
    printf("bb_%d:\n", b->_id);

    for(int inst_idx = 0; inst_idx < vec_len(b->code); ++inst_idx) {
      printf("  ");

      SemInst* inst = &b->code[inst_idx];

      if (inst->write != SEM_NULL_PLACE) {
        printf("_%-3d = ", inst->write);
      }
      else {
        printf("%7s", "");
      }

      printf("%s ", sem_op_label[inst->op]);

      for (int i = 0; i < inst->num_reads; ++i) {
        if (i > 0) {
          printf(", ");
        }

        printf("_%d", inst->reads[i]);
      }

      switch (inst->op) {
        case SEM_OP_INTEGER_CONST:
          printf("%lld", (uint64_t)inst->data);
          break;
      }

      printf("\n");
    }
  }
}