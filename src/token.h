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

Tokens lex_source(Arena* arena, char* source);