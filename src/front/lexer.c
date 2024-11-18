#include <ctype.h>

#include "front.h"

static Token make_token(int kind, int length, char* start, int line) {
  return (Token) {
    .kind = kind,
    .length = length,
    .start = start,
    .line = line
  };
}

static Tokens bake_tokens(Arena* arena, Vec(Token)* vec) {
  int count = vec_len(*vec);
  size_t sz = count * sizeof(Token);
  
  Token* data = arena_push(arena, sz);

  memcpy(data, *vec, sz);
  vec_free(*vec);

  return (Tokens) {
    .count = count,
    .data = data
  };
}

static bool is_ident(int c) {
  return isalnum(c) || c == '_';
}

Tokens lex_source(Arena* arena, char* source) {
  Vec(Token) vec = NULL;

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

    vec_put(vec, make_token(
      kind,
      (int)(cursor - start),
      start,
      start_line
    ));
  }

  vec_put(vec, make_token(
    TOKEN_EOF,
    0,
    cursor,
    line
  ));

  return bake_tokens(arena, &vec);
}