#include "index.h"
#include "search.h"
#include "unicode.h"
#include "cli-utf8.h"

#include <climits>
#include <cstdio>
#include <map>
#include <stdexcept>

class AnagramFilter : public SearchFilter {
 public:
  explicit AnagramFilter(const SymbolString& letters) {
    std::map<Symbol, int> frequencies;
    for (Symbol symbol : letters) {
      if (symbol == EncodeScalar(U' ') || symbol >= kEnd)
        throw std::runtime_error("anagram letters must not contain boundaries");
      ++frequencies[symbol];
    }
    int product = 1;
    for (const auto& entry : frequencies) {
      const int radix = entry.second + 1;
      if (product > (INT_MAX - 1) / radix)
        throw std::runtime_error("anagram state space is too large");
      values_[entry.first] = {product, product * radix};
      product *= radix;
    }
    complete_ = product - 1;
    accepting_ = product;
  }

  bool is_accepting(State state) const override { return state == accepting_; }

  bool has_transition(State from, Symbol symbol, State* to) const override {
    if (symbol == kPositionBreak) {
      *to = from;
      return true;
    }
    if (symbol == EncodeScalar(U' ') || symbol == kEnd) {
      if (from != complete_ && from != accepting_) return false;
      *to = accepting_;
      return true;
    }
    const auto found = values_.find(symbol);
    if (found == values_.end()) return false;
    const int next = from + found->second.first;
    if (next % found->second.second < found->second.first) return false;
    *to = next;
    return true;
  }

 private:
  struct Value {
    int first;
    int second;
  };
  std::map<Symbol, Value> values_;
  int complete_ = 0;
  int accepting_ = 1;
};

int main(int argc, char** argv) {
  Utf8CommandLine command_line(argc, argv);
  argc = command_line.argc();
  argv = command_line.argv();
  ConfigureBinaryStandardStreams();
  if (argc != 3) {
    std::fprintf(stderr, "usage: %s input.index letters\n", argv[0]);
    return 2;
  }
  FILE* file = OpenFileUtf8(argv[1], "rb");
  if (file == nullptr) {
    std::fprintf(stderr, "error: can't open \"%s\"\n", argv[1]);
    return 1;
  }
  try {
    IndexReader reader(file);
    AnagramFilter filter(NormalizeQueryLiteral(argv[2]));
    SearchDriver driver(&reader, &filter, 0, 1e-6);
    PrintAll(&driver);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
