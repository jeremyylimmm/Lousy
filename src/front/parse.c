#include <stdio.h>

#include "front.h"

typedef enum {
  STATE_PRIMARY,

  STATE_BINARY,
  STATE_BINARY_INFIX,

  STATE_EXPR,

  STATE_COMPLETE
} StateKind;

typedef struct {
  StateKind kind;
  union {
    int binary_prec;
    struct {
      ParseNodeKind kind;
      Token token;
      int num_children;
    } complete;
  } as;
} State;

typedef struct {
  int cur_token;
  Tokens tokens;

  ParseNode* nodes;
  int num_nodes;

  Vec(State) stack;
} Parser;

ParseChildIterator parse_children_begin(ParseNode* node) {
  return (ParseChildIterator) {
    .index = node->num_children-1,
    .child = node-1
  };
}

bool parse_children_continue(ParseChildIterator* it) {
  return it->index >= 0;
}

void parse_children_next(ParseChildIterator* it) {
  it->child -= it->child->subtree_size;
  it->index--;
}

static void make_node(Parser* p, ParseNodeKind kind, Token token, int num_children, void* data) {
  assert(kind);
  assert(p->num_nodes < p->tokens.count && "nodes exhausted - parser bug");

  ParseNode* n = p->nodes + (p->num_nodes++);

  n->kind = kind;
  n->token = token;
  n->num_children = num_children;
  n->subtree_size = 1;
  n->data = data;

  ParseNode* child = n-1;

  for (int i = 0; i < num_children; ++i) {
    n->subtree_size += child->subtree_size;
    child -= child->subtree_size;
  }
}

static void push_state(Parser* p, State state) {
  vec_put(p->stack, state);
}

static Token peek(Parser* p) {
  return p->tokens.data[p->cur_token];
}

static Token lex(Parser* p) {
  Token token = peek(p);

  if (p->cur_token < p->tokens.count - 1) {
    p->cur_token++;
  }

  return token;
}

static State state_primary() {
  return (State) {
    .kind = STATE_PRIMARY
  };
}

static State state_binary(int prec) {
  return (State) {
    .kind = STATE_BINARY,
    .as.binary_prec = prec
  };
}

static State state_binary_infix(int prec) {
  return (State) {
    .kind = STATE_BINARY_INFIX,
    .as.binary_prec = prec
  };
}

static State state_complete(ParseNodeKind kind, Token token, int num_children) {
  return (State) {
    .kind = STATE_COMPLETE,
    .as.complete.kind = kind,
    .as.complete.token = token,
    .as.complete.num_children = num_children
  };
}

static State state_expr() {
  return (State) {
    .kind = STATE_EXPR
  };
}

static uint64_t parse_integer(Token tok) {
  uint64_t value = 0;

  for (int i = 0; i < tok.length; ++i) {
    value *= 10;
    value += tok.start[i] - '0';
  }

  return value;
}

static int binary_prec(Token op) {
  switch (op.kind) {
    case '*':
    case '/':
      return 20;
    case '+':
    case '-':
      return 10;
    default:
      return 0;
  }
}

static ParseNodeKind binary_kind(Token op) {
  switch (op.kind) {
    case '*':
      return PARSE_NODE_MUL;
    case '/':
      return PARSE_NODE_DIV;
    case '+':
      return PARSE_NODE_ADD;
    case '-':
      return PARSE_NODE_SUB;
    default:
      return PARSE_NODE_UNINITIALIZED;
  }
}

static bool handle_state(Parser* p, State state) {
  switch (state.kind) {
    default:
      assert(false);
      return false;
    
    case STATE_PRIMARY: {
      switch (peek(p).kind) {
        default:
          assert(false && "expected an expression");
          return false;
        case TOKEN_INTEGER: {
          Token tok = lex(p);
          uint64_t value = parse_integer(tok);
          make_node(p, PARSE_NODE_INTEGER, tok, 0, (void*)value);
        } break;
      }

      return true;
    }

    case STATE_BINARY: {
      push_state(p, state_binary_infix(state.as.binary_prec));
      push_state(p, state_primary());
      return true;
    }

    case STATE_BINARY_INFIX: {
      if (binary_prec(peek(p)) > state.as.binary_prec) {
        Token op = lex(p);
        push_state(p, state_binary_infix(state.as.binary_prec));
        push_state(p, state_complete(binary_kind(op), op, 2));
        push_state(p, state_binary(binary_prec(op)));
      }
      return true;
    }

    case STATE_COMPLETE: {
      make_node(p, state.as.complete.kind, state.as.complete.token, state.as.complete.num_children, NULL);
      return true;
    }

    case STATE_EXPR: {
      push_state(p, state_binary(0));
      return true;
    }
  }
}

ParseTree* parse(Arena* arena, Tokens tokens) {
  Parser p = {
    .tokens = tokens,
    .nodes = arena_array(arena, ParseNode, tokens.count),
  };

  push_state(&p, state_expr());

  ParseTree* tree = NULL;

  while (vec_len(p.stack)) {
    State state = vec_pop(p.stack);
    if (!handle_state(&p, state)) {
      goto end;
    }
  }

  tree = arena_type(arena, ParseTree);
  tree->nodes = p.nodes;
  tree->num_nodes = p.num_nodes;

  end:
  vec_free(p.stack);
  return tree;
}

typedef struct {
  ParseNode* node;
  int depth;
  uint64_t* first_child;
} PrintItem;

static PrintItem make_print_item(Arena* arena, ParseNode* node, int depth, uint64_t* parent_first_child, bool is_first_child) {
  uint64_t* first_child = arena_array(arena, uint64_t, bitset_num_u64(depth + 1));
  memcpy(first_child, parent_first_child, bitset_num_u64(depth)*sizeof(uint64_t));

  if (is_first_child) {
    bitset_set(first_child, depth);
  }

  return (PrintItem) {
    .node = node,
    .depth = depth,
    .first_child = first_child
  };
}

void print_parse_tree(ParseTree* tree) {
  Scratch scratch = scratch_get(0, NULL);

  Vec(PrintItem) stack = NULL;

  assert(tree->num_nodes);
  ParseNode* root = &tree->nodes[tree->num_nodes-1];
  vec_put(stack, make_print_item(scratch.arena, root, 0, NULL, true));

  PrintItem* items = arena_array(scratch.arena, PrintItem, tree->num_nodes);

  while (vec_len(stack)) {
    PrintItem item = vec_pop(stack);

    items[item.node-tree->nodes] = item;

    parse_node_for_each_child(item.node, it) {
      vec_put(stack, make_print_item(
        scratch.arena,
        it.child,
        item.depth + 1,
        item.first_child,
        it.index == 0
      ));
    }
  }

  for (int i = 0; i < tree->num_nodes; ++i) {
    PrintItem item = items[i];

    for (int d = 1; d <= item.depth; ++d) {
      bool first_child = bitset_get(item.first_child, d);

      if (d == item.depth) {
        printf("%c", first_child ? 218 : 195);
        printf("%c", 196);
      }
      else {
        printf("%c", first_child ? ' ' :  179);
        printf(" ");
      }
    }

    printf("%s: '%.*s'\n", parse_node_label[item.node->kind], item.node->token.length, item.node->token.start);
  }

  vec_free(stack);
  scratch_release(&scratch);
}