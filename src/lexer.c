#include <stdlib.h>
#include <ctype.h>

#include "token.h"

typedef struct {
  int capacity;
  int length;
  Token* data;
} TokenVec;

static void push_token(TokenVec* vec, Token tok) {
  if (vec->length == vec->length) {
    vec->capacity = vec->capacity ? vec->capacity * 2 : 8;
    vec->data = realloc(vec->data, vec->capacity * sizeof(vec->data[0]));
  }

  vec->data[vec->length++] = tok;
}

static Tokens bake_tokens(Arena* arena, TokenVec* vec) {
  size_t sz = vec->length * sizeof(Token);
  Token* data = arena_push(arena, sz);

  memcpy(data, vec->data, sz);
  free(vec->data);

  return (Tokens) {
    .count = vec->length,
    .data = data
  };
}

static bool is_ident(int c) {
  return isalnum(c) || c == '_';
}

Tokens lex_source(Arena* arena, char* source) {
  TokenVec vec = {0};

  int line = 1;
  char* cursor = source;

  while (1) {
    while (isspace(*cursor)) {
      if (*cursor == '\n') {
        line++;
      }

      *cursor++;
    }

    if (*cursor == '\0') {
      break;
    }

    char* start = cursor++;
    int start_line = line;
    int kind = *start;

    switch (*start) {
      case '0':
      case '1':
      case '2':
      case '3':
      case '4':
      case '5':
      case '6':
      case '7':
      case '8':
      case '9':
        while (isdigit(*cursor)) {
          ++cursor;
        }
        kind = TOKEN_INTEGER;
        break;
      case '_':
      case 'a':
      case 'b':
      case 'c':
      case 'd':
      case 'e':
      case 'f':
      case 'g':
      case 'h':
      case 'i':
      case 'j':
      case 'k':
      case 'l':
      case 'm':
      case 'n':
      case 'o':
      case 'p':
      case 'q':
      case 'r':
      case 's':
      case 't':
      case 'u':
      case 'v':
      case 'w':
      case 'x':
      case 'y':
      case 'z':
        while(is_ident(*cursor)) {
          cursor++;
        }
        kind = TOKEN_IDENTIFIER;
        break;
    }

    push_token(&vec, (Token) {
      .kind = kind,
      .length = (int)(cursor - start),
      .start = start,
      .line = start_line
    });
  }

  push_token(&vec, (Token) {
    .kind = TOKEN_EOF,
    .length = 0,
    .start = cursor,
    .line = line
  });

  return bake_tokens(arena, &vec);
}