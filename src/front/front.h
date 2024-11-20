#pragma once

#include "utility.h"

enum {
  TOKEN_EOF = 0,

  TOKEN_INTEGER = 256,
  TOKEN_IDENTIFIER,

  TOKEN_KEYWORD_IF,
  TOKEN_KEYWORD_ELSE,
  TOKEN_KEYWORD_WHILE,
  TOKEN_KEYWORD_RETURN,
};

typedef struct {
  int kind;
  int length;
  char* start;
  int line;
} Token;

typedef struct {
  int count;
  Token* data;
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
  "!!uninitialized!!",
  #include "parse_node.def"
};
#undef X

typedef struct {
  ParseNodeKind kind;
  Token token;
  int num_children;
  int subtree_size;
} ParseNode;

typedef struct {
  ParseNode* nodes;
  int num_nodes;
} ParseTree;

typedef struct {
  int index;
  ParseNode* child;
} ParseChildIterator;

#define X(name, ...) SEM_OP_##name,
typedef enum {
  SEM_OP_UNINITIALIZED,
  #include "sem_op.def"
  NUM_SEM_OPS
} SemOp;
#undef X

#define X(name, label, ...) label,
static const char* sem_op_label[] = {
  "!!uninitialized!!",
  #include "sem_op.def"
};
#undef X

typedef uint32_t SemPlace;
#define SEM_NULL_PLACE 0xffffffff

typedef struct {
  int dummy;
} SemPlaceData;

typedef struct {
  SemOp op;

  Token token;

  SemPlace reads[4];
  int num_reads;

  SemPlace write;

  void* data;
} SemInst;

typedef struct SemBlock SemBlock;
struct SemBlock {
  bool contains_usercode;
  SemBlock* next;
  Vec(SemInst) code;
  int _id;
};

typedef struct {
  SemBlock* cfg;
  Vec(SemPlaceData) place_data;
} SemFunc;

Tokens lex_source(Arena* arena, char* source);

ParseTree* parse(Arena* arena, Tokens tokens, const char* path, const char* source);

void print_parse_tree(ParseTree* tree);

ParseChildIterator parse_children_begin(ParseNode* node);
bool parse_children_continue(ParseChildIterator* it);
void parse_children_next(ParseChildIterator* it);

#define parse_node_for_each_child(node, it) for (ParseChildIterator it = parse_children_begin(node); parse_children_continue(&it); parse_children_next(&it))

void error_token(const char* path, const char* source, Token token, const char* fmt, ...);

SemFunc* check_tree(Arena* arena, const char* path, const char* source, ParseTree* tree);
void free_sem_func_storage(SemFunc* func);

void print_sem_func(SemFunc* func);

bool sem_analyze_func(const char* path, const char* source, SemFunc* func);