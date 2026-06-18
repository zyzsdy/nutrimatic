#include "index.h"

#include <stdio.h>
#include <stdlib.h>

#include <fstream>
#include <string>

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

  IndexWriter writer(fp);
  writer.next("a ", 0, 1);
  writer.next(NULL, 0, 0);
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
