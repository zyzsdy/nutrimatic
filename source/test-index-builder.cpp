#include "index-builder.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

Symbol S(char32_t cp) { return EncodeScalar(cp); }

void TestMixedChains() {
  const SymbolString normalized =
      NormalizeCorpusText("ab \xE4\xB8\xAD\xE5\x9B\xBD").symbols;
  const std::vector<SymbolString> chains = GenerateIndexChains(normalized, 40);
  Expect(chains.size() == 3, "line, space and CJK position starts");
  Expect(chains[0] == SymbolString({S(U'a'), S(U'b'), S(U' '), S(U'\u4E2D'),
                                    kPositionBreak, S(U'\u56FD'), kEnd}),
         "full mixed chain");
  Expect(chains[1] == SymbolString({S(U'\u4E2D'), kPositionBreak,
                                    S(U'\u56FD'), kEnd}),
         "start after visible space");
  Expect(chains[2] == SymbolString({S(U'\u56FD'), kEnd}),
         "start after position break");
}

void TestWindowCountsVisibleSymbols() {
  SymbolString normalized;
  for (int i = 0; i < 41; ++i) normalized.push_back(S(U'a'));
  const std::vector<SymbolString> chains = GenerateIndexChains(normalized, 40);
  Expect(chains.size() == 2, "hard split long unbroken run");
  Expect(chains[0].size() == 41 && chains[0].back() == kEnd,
         "first hard window contains 40 visible symbols");
  Expect(chains[1] == SymbolString({S(U'a'), kEnd}),
         "hard split continues at symbol 41");
}

}  // namespace

int main() {
  TestMixedChains();
  TestWindowCountsVisibleSymbols();
  return 0;
}
