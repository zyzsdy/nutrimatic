#include "index.h"
#include "search.h"
#include "expr.h"
#include "cli-utf8.h"

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
  Utf8CommandLine command_line(argc, argv);
  argc = command_line.argc();
  argv = command_line.argv();
  ConfigureBinaryStandardStreams();
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

  FILE *fp = OpenFileUtf8(index_path, "rb");
  if (fp == NULL) {
    fprintf(stderr, "error: can't open \"%s\"\n", index_path);
    return 1;
  }

  try {
    IndexReader reader(fp);
    StdVectorFst parsed;
    ExprError error;
    const ExprCompileContext context{reader.alphabet(), {}};
    if (!CompileExpr(expression, context, &parsed, &error)) {
      fprintf(stderr, "error: expression byte %zu: %s\n", error.byte_offset,
              error.message.c_str());
      return 2;
    }

    StdVectorFst boundary;
    const auto start = boundary.AddState();
    const auto final = boundary.AddState();
    boundary.SetStart(start);
    boundary.SetFinal(final, StdArc::Weight::One());
    for (Symbol symbol : {EncodeScalar(U' '), kPositionBreak, kEnd})
      boundary.AddArc(start, StdArc(symbol, symbol, StdArc::Weight::One(), final));
    Concat(&parsed, boundary);

    ExprFilter filter(parsed);
    SearchDriver driver(&reader, &filter, filter.start(), 1e-6);
    PrintAll(&driver, max_steps);
    return 0;
  } catch (const std::exception& error) {
    fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
