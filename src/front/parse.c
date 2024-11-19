#include <stdio.h>

#include "front.h"

typedef enum {
  STATE_PRIMARY,

  STATE_BINARY,
  STATE_BINARY_INFIX,

  STATE_SEMICOLON,

  STATE_EXPR,

  STATE_BLOCK,
  STATE_BLOCK_STMT,

  STATE_LOCAL,

  STATE_IF,
  STATE_ELSE,

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
    struct {
      int count;
      Token lbrace;
    } block_stmt;
    Token else_if_tok;
  } as;
} State;

typedef struct {
  const char* path;
  const char* source;

  int cur_token;
  Tokens tokens;

  Vec(ParseNode) nodes;

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

static void make_node(Parser* p, ParseNodeKind kind, Token token, int num_children) {
  assert(kind);

  ParseNode n = {
    .kind = kind,
    .token = token,
    .num_children = num_children,
    .subtree_size = 1,
  };

  int child = vec_len(p->nodes)-1;

  for (int i = 0; i < num_children; ++i) {
    int sts = p->nodes[child].subtree_size;
    n.subtree_size += sts;
    child -= sts;
  }

  vec_put(p->nodes, n);
}

static void push_state(Parser* p, State state) {
  vec_put(p->stack, state);
}

static Token peek(Parser* p) {
  return p->tokens.data[p->cur_token];
}

static Token peekn(Parser* p, int offset) {
  int index = p->cur_token + offset;

  if (index >= p->tokens.count) {
    index = p->tokens.count - 1;
  }

  return p->tokens.data[index];
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

static State state_block() {
  return (State) {
    .kind = STATE_BLOCK
  };
}

static State state_block_stmt(Token lbrace, int num_stmts) {
  return (State) {
    .kind = STATE_BLOCK_STMT,
    .as.block_stmt.lbrace = lbrace,
    .as.block_stmt.count = num_stmts,
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

static State state_semicolon() {
  return (State) {
    .kind = STATE_SEMICOLON
  };
}

static State state_local() {
  return (State) {
    .kind = STATE_LOCAL
  };
}

static State state_if() {
  return (State) {
    .kind = STATE_IF
  };
}

static State state_else(Token if_tok) {
  return (State) {
    .kind = STATE_ELSE,
    .as.else_if_tok = if_tok
  };
}

static int binary_prec(Token op, bool calling) {
  int i = calling ? 1 : 0;

  switch (op.kind) {
    case '*':
    case '/':
      return 20;
    case '+':
    case '-':
      return 10;
    case '=':
      return 5 - i;
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
    case '=':
      return PARSE_NODE_ASSIGN;
    default:
      return PARSE_NODE_UNINITIALIZED;
  }
}

static bool match(Parser* p, int kind, const char* message) {
  Token token = peek(p);

  if (token.kind == kind) {
    lex(p);
    return true;
  }

  error_token(p->path, p->source, token, message);
  return false;
}

#define REQUIRE(p, kind, msg) \
  do { \
    if (!match(p, kind, msg)) {\
      return false; \
    } \
  } while (false)

static bool handle_state(Parser* p, State state) {
  switch (state.kind) {
    default:
      assert(false);
      return false;
    
    case STATE_PRIMARY: {
      switch (peek(p).kind) {
        default:
          error_token(p->path, p->source, peek(p), "expected an expression");
          return false;

        case TOKEN_IDENTIFIER: {
          Token tok = lex(p);
          make_node(p, PARSE_NODE_SYMBOL, tok, 0);
          return true;
        }

        case TOKEN_INTEGER: {
          Token tok = lex(p);
          make_node(p, PARSE_NODE_INTEGER, tok, 0);
          return true;
        }
      }
    }

    case STATE_BINARY: {
      push_state(p, state_binary_infix(state.as.binary_prec));
      push_state(p, state_primary());
      return true;
    }

    case STATE_BINARY_INFIX: {
      if (binary_prec(peek(p), false) > state.as.binary_prec) {
        Token op = lex(p);
        push_state(p, state_binary_infix(state.as.binary_prec));
        push_state(p, state_complete(binary_kind(op), op, 2));
        push_state(p, state_binary(binary_prec(op, true)));
      }
      return true;
    }

    case STATE_COMPLETE: {
      make_node(p, state.as.complete.kind, state.as.complete.token, state.as.complete.num_children);
      return true;
    }

    case STATE_SEMICOLON: {
      REQUIRE(p, ';', "ill-formed expression, consider adding a ';' here"); 
      return true;
    }

    case STATE_EXPR: {
      push_state(p, state_binary(0));
      return true;
    }

    case STATE_BLOCK: {
      Token lbrace = peek(p);
      REQUIRE(p, '{', "expected a block '{'");
      push_state(p, state_block_stmt(lbrace, 0));
      return true;
    }

    case STATE_BLOCK_STMT: {
      if (peek(p).kind == '}') {
        lex(p);
        make_node(p, PARSE_NODE_BLOCK, state.as.block_stmt.lbrace, state.as.block_stmt.count);
        return true;
      }

      if (peek(p).kind == TOKEN_EOF) {
        error_token(p->path, p->source, state.as.block_stmt.lbrace, "no matching '}' to close this block");
        return false;
      }

      push_state(p, state_block_stmt(state.as.block_stmt.lbrace, state.as.block_stmt.count+1));

      switch (peek(p).kind) {
        default:
          push_state(p, state_semicolon());
          push_state(p, state_expr());
          break;

        case '{':
          push_state(p, state_block());
          break;

        case TOKEN_IDENTIFIER:
          push_state(p, state_semicolon());

          if (peekn(p, 1).kind == ':') {
            push_state(p, state_local());
          }
          else {
            push_state(p, state_expr());
          }
          break;

        case TOKEN_KEYWORD_IF:
          push_state(p, state_if());
          break;
      }

      return true;
    }

    case STATE_LOCAL: {
      Token name = peek(p);
      REQUIRE(p, TOKEN_IDENTIFIER, "expected a local declaration");

      Token colon = peek(p);
      REQUIRE(p, ':', "expected local declaration, consider adding a ':' here");

      Token type = peek(p);
      REQUIRE(p, TOKEN_IDENTIFIER, "expected a typename");

      make_node(p, PARSE_NODE_IDENTIFIER, name, 0);
      make_node(p, PARSE_NODE_TYPENAME, type, 0);

      if (peek(p).kind == '=') {
        lex(p);
        push_state(p, state_complete(PARSE_NODE_LOCAL, colon, 3));
        push_state(p, state_expr());
      }
      else {
        make_node(p, PARSE_NODE_LOCAL, colon, 2);
      }

      return true;
    }

    case STATE_IF: {
      Token if_tok = peek(p);
      REQUIRE(p, TOKEN_KEYWORD_IF, "expected an if statement");

      push_state(p, state_else(if_tok));
      push_state(p, state_block());
      push_state(p, state_expr());

      return true;
    }

    case STATE_ELSE: {
      if (peek(p).kind == TOKEN_KEYWORD_ELSE) {
        lex(p);

        push_state(p, state_complete(PARSE_NODE_IF, state.as.else_if_tok, 3));

        switch (peek(p).kind) {
          default:
            error_token(p->path, p->source, peek(p), "an else clause must be followed by an if statement or a block");
            return false;
          case TOKEN_KEYWORD_IF:
            push_state(p, state_if());
            break;
          case '{':
            push_state(p, state_block());
            break;
        }
      }
      else {
        make_node(p, PARSE_NODE_IF, state.as.else_if_tok, 2);
      }

      return true;
    } 
  }
}

ParseTree* parse(Arena* arena, Tokens tokens, const char* path, const char* source) {
  Parser p = {
    .path = path,
    .source = source,
    .tokens = tokens,
  };

  push_state(&p, state_block());

  ParseTree* tree = NULL;

  while (vec_len(p.stack)) {
    State state = vec_pop(p.stack);
    if (!handle_state(&p, state)) {
      goto end;
    }
  }

  tree = arena_type(arena, ParseTree);
  tree->num_nodes = vec_len(p.nodes);
  tree->nodes = vec_bake(arena, p.nodes);

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