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
    struct {
      SemBlock* entry_head;
      SemBlock* entry_tail;
      SemBlock* body_head;
    } while_loop;
    struct {
      SemBlock* entry_tail;
      SemBlock* then_head;
      SemBlock* then_tail;
      SemBlock* else_head;
    } if_stmt;
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

static SemBlock* new_block(Checker* c, SemBlock** out_cur) {
  if (out_cur) {
    *out_cur = c->current_block;
  }

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
  }

  make_inst_base(c, c->current_block, write, op, num_reads, data);

  push_place(c, write);
}

static void get_children(ParseNode* node, ParseNode** children, int capacity) {
  (void)capacity;
  assert(node->num_children <= capacity);

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
    get_children(x.node, children, ARRAY_LENGTH(children));

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

static void make_goto(Checker* c, SemBlock* from, SemBlock* to) {
  make_inst_base(c, from, SEM_NULL_PLACE, SEM_OP_GOTO, 0, to);
}

static void make_branch(Checker* c, SemBlock* from, SemBlock* then_loc, SemBlock* else_loc) {
  SemBlock** locs = arena_array(c->arena, SemBlock*, 2);
  locs[0] = then_loc;
  locs[1] = else_loc;

  make_inst_base(c, from, SEM_NULL_PLACE, SEM_OP_BRANCH, 1, locs);
}

static bool check_IF(Checker* c, CheckItem x) {
  ParseNode* children[3];
  get_children(x.node, children, ARRAY_LENGTH(children));

  switch (x.stage) {
    default:
      assert(false);
      return true;

    case 0: {
      next_stage(c, x);
      push(c, item(children[0])); // push cond
      return true;
    }

    case 1: {
      x.as.if_stmt.then_head = new_block(c, &x.as.if_stmt.entry_tail);
      next_stage(c, x);
      push(c, item(children[1])); // push then
      return true;
    }

    case 2: {
      SemBlock* else_head = new_block(c, &x.as.if_stmt.then_tail);

      make_branch(c, x.as.if_stmt.entry_tail, x.as.if_stmt.then_head, else_head);

      if (x.node->num_children == 2) {
        // No else
        make_goto(c, x.as.if_stmt.then_tail, else_head);
      }
      else {
        // Has else
        next_stage(c, x);
        push(c, item(children[2]));
      }

      return true;
    }

    case 3: {
      SemBlock* else_tail;
      SemBlock* end = new_block(c, &else_tail);

      make_goto(c, x.as.if_stmt.then_tail, end);
      make_goto(c, else_tail, end);

      return true;
    }
  }  
}

static bool check_WHILE(Checker* c, CheckItem x) {
  ParseNode* children[2];
  get_children(x.node, children, ARRAY_LENGTH(children));

  switch (x.stage) {
    default:
      assert(false);
      return false;

    case 0: {
      SemBlock* before;
      x.as.while_loop.entry_head = new_block(c, &before);

      make_goto(c, before, x.as.while_loop.entry_head);

      next_stage(c, x);
      push(c, item(children[0])); // push cond

      return true;
    }

    case 1: {
      x.as.while_loop.body_head = new_block(c, &x.as.while_loop.entry_tail);

      next_stage(c, x);
      push(c, item(children[1])); // push body

      return true;
    }

    case 2: {
      SemBlock* body_tail;
      SemBlock* end_head = new_block(c, &body_tail);

      make_branch(c, x.as.while_loop.entry_tail, x.as.while_loop.body_head, end_head);
      make_goto(c, body_tail, x.as.while_loop.entry_head);

      return true;
    }
  }
}

static bool check_RETURN(Checker* c, CheckItem x) {
  switch (x.stage) {
    default:
      assert(false);
      return false;
    
    case 0:
      if (x.node->num_children == 0) {
        make_inst(c, false, SEM_OP_RETURN, 0, NULL);
        new_block(c, NULL);
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
      new_block(c, NULL);
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

  SemBlock* root = new_block(&c, NULL);

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