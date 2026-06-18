#include "index.h"
#include "search.h"
#include "expr.h"

#include "fst/concat.h"

#include <cerrno>
#include <limits>

#include <stdio.h>
#include <stdlib.h>

using namespace fst;

constexpr size_t kDefaultMaxSearchSteps = 1000000;

static bool ParsePositiveSize(const char* text, size_t* value) {
  if (text == NULL || *text == '\0' || *text == '-') return false;

  errno = 0;
  char* end = NULL;
  unsigned long long parsed = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    return false;
  }

  *value = static_cast<size_t>(parsed);
  return true;
}

int main(int argc, char *argv[]) {
  size_t max_steps = kDefaultMaxSearchSteps;
  int next_arg = 1;
  if (next_arg < argc && strcmp(argv[next_arg], "--max-steps") == 0) {
    if (++next_arg >= argc ||
        !ParsePositiveSize(argv[next_arg], &max_steps)) {
      fprintf(stderr, "error: --max-steps must be a positive integer\n");
      return 2;
    }
    ++next_arg;
  }

  if (argc - next_arg != 2 || strlen(argv[next_arg + 1]) == 0) {
    fprintf(stderr, "usage: %s [--max-steps N] input.index expression\n",
            argv[0]);
    return 2;
  }

  const char* index_path = argv[next_arg];
  const char* expression = argv[next_arg + 1];

  SymbolTable *chars = new SymbolTable("chars");
  chars->AddSymbol("epsilon", 0);
  chars->AddSymbol("space", ' ');
  for (int i = 33; i <= 127; ++i)
    chars->AddSymbol(std::string(1, i), i);

  StdVectorFst parsed;
  parsed.SetInputSymbols(chars);
  parsed.SetOutputSymbols(chars);

  const char *p = ParseExpr(expression, &parsed, false);
  if (p == NULL || *p != '\0') {
    fprintf(stderr, "error: can't parse \"%s\"\n", p ? p : expression);
    return 2;
  }

  // Require a space at the end, so the matches must be complete words.
  StdVectorFst space;
  ParseExpr(" ", &space, true);
  Concat(&parsed, space);

  FILE *fp = fopen(index_path, "rb");
  if (fp == NULL) {
    fprintf(stderr, "error: can't open \"%s\"\n", index_path);
    return 1;
  }

  ExprFilter filter(parsed);
  IndexReader reader(fp);
  SearchDriver driver(&reader, &filter, filter.start(), 1e-6);
  PrintAll(&driver, max_steps);
  return 0;
}
