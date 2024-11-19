#pragma once

#include "utility.h"

enum {
  TOKEN_EOF = 0,

  TOKEN_INTEGER = 256,
  TOKEN_IDENTIFIER
};

typedef struct {
  int kind;
  int length;
  char* start;
  int line;
} Token;

typedef struct {
  Token* data;
  int count;
} Tokens;

#define X(name, ...) PARSE_NODE_##name,
typedef enum {
  PARSE_NODE_UNINITIALIZED,
  #include "parse_node.def"
  NUM_PARSE_NODE_KINDS
} ParseNodeKind;
#undef X

#define X(name, label, ...) label,
static const char* parse_node_label[NUM_PARSE_NODE_KINDS] = {
  "!uninitialized!",
  #include "parse_node.def"
};
#undef X

typedef struct {
  ParseNodeKind kind;
  Token token;
  int num_children;
  int subtree_size;
  void* data;
} ParseNode;

typedef struct {
  ParseNode* nodes;
  int num_nodes;
} ParseTree;

typedef struct {
  int index;
  ParseNode* child;
} ParseChildIterator;

Tokens lex_source(Arena* arena, char* source);

ParseTree* parse(Arena* arena, Tokens tokens, const char* path, const char* source);

void print_parse_tree(ParseTree* tree);

ParseChildIterator parse_children_begin(ParseNode* node);
bool parse_children_continue(ParseChildIterator* it);
void parse_children_next(ParseChildIterator* it);

#define parse_node_for_each_child(node, it) for (ParseChildIterator it = parse_children_begin(node); parse_children_continue(&it); parse_children_next(&it))

void error_token(const char* path, const char* source, Token token, const char* fmt, ...);