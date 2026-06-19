#include "index.h"
#include "index-builder.h"

#include <stdio.h>
#include <stdlib.h>

#include <algorithm>
#include <fstream>
#include <string>
#include <vector>

static void Fail(const char* message) {
  fprintf(stderr, "FAIL: %s\n", message);
  exit(1);
}

static std::string ReadFile(const char* path) {
  std::ifstream input(path, std::ios::binary);
  return std::string((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
}

static void ExpectInvalidMaxSteps(const char* value) {
  std::string command = "build\\find-expr.exe --max-steps";
  if (value != NULL) {
    command += " ";
    command += value;
  }
  command += " test-find-expr.index a > test-find-expr.out "
             "2> test-find-expr.err";

  if (system(command.c_str()) == 0) Fail("invalid --max-steps was accepted");
  if (ReadFile("test-find-expr.err").find("max-steps") == std::string::npos) {
    Fail("invalid --max-steps diagnostic is missing");
  }
}

int main() {
  FILE* fp = fopen("test-find-expr.index", "wb");
  if (fp == NULL) Fail("can't create test index");

  IndexMetadata metadata;
  metadata.unicode_version = UnicodeVersionArray();
  IndexWriter writer(fp, metadata);
  std::vector<SymbolString> chains =
      GenerateIndexChains(NormalizeCorpusText("a ").symbols);
  std::sort(chains.begin(), chains.end());
  SymbolString previous;
  for (const SymbolString& chain : chains) {
    std::size_t same = 0;
    while (same < previous.size() && same < chain.size() &&
           previous[same] == chain[same]) {
      ++same;
    }
    writer.Next(&chain, same, 1);
    previous = chain;
  }
  writer.Finish();
  fclose(fp);

  int status = system(
      "build\\find-expr.exe --max-steps 1 test-find-expr.index a "
      "> test-find-expr.out 2> test-find-expr.err");
  if (status != 0) Fail("--max-steps should be accepted");

  std::string contents = ReadFile("test-find-expr.out");
  if (!contents.empty()) Fail("one search step should produce no results");

  ExpectInvalidMaxSteps(NULL);
  ExpectInvalidMaxSteps("-1");
  ExpectInvalidMaxSteps("0");
  ExpectInvalidMaxSteps("abc");
  ExpectInvalidMaxSteps("184467440737095516160");

  status = system(
      "build\\find-expr.exe test-find-expr.index a "
      "> test-find-expr.out 2> test-find-expr.err");
  if (status != 0) Fail("default max steps should be accepted");
  if (ReadFile("test-find-expr.out").empty()) {
    Fail("default max steps should allow the result");
  }

  remove("test-find-expr.index");
  remove("test-find-expr.out");
  remove("test-find-expr.err");
  return 0;
}
