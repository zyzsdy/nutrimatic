#include "index-builder.h"

#include <algorithm>
#include <stdexcept>

namespace {

bool IsBoundary(Symbol symbol) {
  return symbol == EncodeScalar(U' ') || symbol == kPositionBreak;
}

}  // namespace

std::vector<SymbolString> GenerateIndexChains(
    const SymbolString& normalized_symbols, std::size_t max_chain_symbols) {
  if (max_chain_symbols == 0)
    throw std::invalid_argument("maximum chain length must be nonzero");
  std::vector<std::size_t> starts;
  if (!normalized_symbols.empty() && !IsBoundary(normalized_symbols.front()))
    starts.push_back(0);

  std::size_t run_visible = 0;
  for (std::size_t i = 0; i < normalized_symbols.size(); ++i) {
    const Symbol symbol = normalized_symbols[i];
    if (IsBoundary(symbol)) {
      run_visible = 0;
      if (i + 1 < normalized_symbols.size() &&
          !IsBoundary(normalized_symbols[i + 1])) {
        starts.push_back(i + 1);
      }
      continue;
    }
    if (symbol >= kEnd || symbol == kEpsilon)
      throw std::invalid_argument("normalized stream contains an invalid symbol");
    ++run_visible;
    if (run_visible > max_chain_symbols &&
        (run_visible - 1) % max_chain_symbols == 0) {
      starts.push_back(i);
    }
  }
  std::sort(starts.begin(), starts.end());
  starts.erase(std::unique(starts.begin(), starts.end()), starts.end());

  std::vector<SymbolString> result;
  result.reserve(starts.size());
  for (std::size_t start : starts) {
    SymbolString chain;
    std::size_t visible = 0;
    for (std::size_t i = start; i < normalized_symbols.size(); ++i) {
      const Symbol symbol = normalized_symbols[i];
      if (!IsBoundary(symbol) && visible == max_chain_symbols) break;
      chain.push_back(symbol);
      if (!IsBoundary(symbol)) ++visible;
    }
    while (!chain.empty() && IsBoundary(chain.back())) chain.pop_back();
    if (!chain.empty()) {
      chain.push_back(kEnd);
      result.push_back(std::move(chain));
    }
  }
  return result;
}
