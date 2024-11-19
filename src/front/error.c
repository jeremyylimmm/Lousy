#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>

#include "front.h"

void error_token(const char* path, const char* source, Token token, const char* fmt, ...) {
  const char* line_start = token.start;

  while (line_start != source && *line_start != '\n') {
    line_start--;
  }

  while (isspace(*line_start)) {
    line_start++;
  }

  int line_length = 0;
  while (line_start[line_length] != '\n' && line_start[line_length] != '\0') {
    line_length++;
  }

  int offset = fprintf(stderr, "%s(%d): error: ", path, token.line);
  fprintf(stderr, "%.*s\n", line_length, line_start);

  offset += (int)(token.start-line_start);

  fprintf(stderr, "%*s^ ", offset, "");

  va_list ap;
  va_start(ap, fmt);

  vfprintf(stderr, fmt, ap);

  va_end(ap);

  fprintf(stderr, "\n");
}