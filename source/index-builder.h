#ifndef NUTRIMATIC_INDEX_BUILDER_H_
#define NUTRIMATIC_INDEX_BUILDER_H_

#include "unicode.h"

#include <cstddef>
#include <vector>

std::vector<SymbolString> GenerateIndexChains(
    const SymbolString& normalized_symbols,
    std::size_t max_chain_symbols = 40);

#endif  // NUTRIMATIC_INDEX_BUILDER_H_
