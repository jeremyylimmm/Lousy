#include <ctype.h>
#include <string.h>

#include "front.h"

static Token make_token(int kind, int length, char* start, int line) {
  return (Token) {
    .kind = kind,
    .length = length,
    .start = start,
    .line = line
  };
}

static bool is_ident(int c) {
  return isalnum(c) || c == '_';
}

static int check_keyword(const char* start, const char* cursor, const char* keyword, int kind) {
  size_t len = cursor-start;

  if (len == strlen(keyword) && memcmp(start, keyword, len * sizeof(char)) == 0) {
    return kind;
  }

  return TOKEN_IDENTIFIER;
}

static int check_keywords(const char* start, const char* cursor) {
  switch (start[0]) {
    default:
      return TOKEN_IDENTIFIER;
    case 'i':
      return check_keyword(start, cursor, "if", TOKEN_KEYWORD_IF);
    case 'e':
      return check_keyword(start, cursor, "else", TOKEN_KEYWORD_ELSE);
    case 'w':
      return check_keyword(start, cursor, "while", TOKEN_KEYWORD_WHILE);
    case 'r':
      return check_keyword(start, cursor, "return", TOKEN_KEYWORD_RETURN);
  }
}

Tokens lex_source(Arena* arena, char* source) {
  Vec(Token) vec = NULL;

  int line = 1;
  char* cursor = source;

  while (1) {

    while (1) {
      while (isspace(*cursor)) {
        if (*cursor == '\n') {
          line++;
        }

        *cursor++;
      }

      if (cursor[0] == '/' && cursor[1] == '/') {
        while (*cursor != '\n' && *cursor != '\0') {
          ++cursor;
        }
      }
      else {
        break;
      }
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
        kind = check_keywords(start, cursor);
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

  return (Tokens) {
    .count = vec_len(vec),
    .data = vec_bake(arena, vec)
  };
}