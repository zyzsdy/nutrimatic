#include "index.h"
#include "search.h"
#include "cli-utf8.h"

#include <cstdio>
#include <cstring>
#include <string>

class PhoneFilter : public SearchFilter {
 public:
  explicit PhoneFilter(const char* digits) : digits_(digits) {}
  bool is_accepting(State state) const override {
    return state == static_cast<State>(digits_.size()) + 1;
  }
  bool has_transition(State from, Symbol symbol, State* to) const override {
    const State length = static_cast<State>(digits_.size());
    if (symbol == kPositionBreak) {
      *to = from;
      return true;
    }
    if (symbol == EncodeScalar(U' ') || symbol == kEnd) {
      if (from != length && from != length + 1) return false;
      *to = length + 1;
      return true;
    }
    if (from < 0 || from >= length || symbol == kEpsilon || symbol >= kEnd)
      return false;
    const char32_t cp = static_cast<char32_t>(symbol - 1);
    char digit = 0;
    if (cp >= U'0' && cp <= U'9') digit = static_cast<char>(cp);
    else if (cp >= U'a' && cp <= U'c') digit = '2';
    else if (cp >= U'd' && cp <= U'f') digit = '3';
    else if (cp >= U'g' && cp <= U'i') digit = '4';
    else if (cp >= U'j' && cp <= U'l') digit = '5';
    else if (cp >= U'm' && cp <= U'o') digit = '6';
    else if (cp >= U'p' && cp <= U's') digit = '7';
    else if (cp >= U't' && cp <= U'v') digit = '8';
    else if (cp >= U'w' && cp <= U'z') digit = '9';
    if (digit == 0 || digits_[from] != digit) return false;
    *to = from + 1;
    return true;
  }

 private:
  std::string digits_;
};

int main(int argc, char** argv) {
  Utf8CommandLine command_line(argc, argv);
  argc = command_line.argc();
  argv = command_line.argv();
  ConfigureBinaryStandardStreams();
  if (argc != 3 || std::strspn(argv[2], "0123456789") != std::strlen(argv[2])) {
    std::fprintf(stderr, "usage: %s input.index digits\n", argv[0]);
    return 2;
  }
  FILE* file = OpenFileUtf8(argv[1], "rb");
  if (file == nullptr) {
    std::fprintf(stderr, "error: can't open \"%s\"\n", argv[1]);
    return 1;
  }
  try {
    IndexReader reader(file);
    PhoneFilter filter(argv[2]);
    SearchDriver driver(&reader, &filter, 0, 1e-6);
    PrintAll(&driver);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
