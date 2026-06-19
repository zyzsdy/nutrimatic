#ifndef NUTRIMATIC_SEARCH_H_
#define NUTRIMATIC_SEARCH_H_

#include "index.h"

#include <cstddef>
#include <deque>
#include <limits>
#include <queue>
#include <set>
#include <string>
#include <vector>

struct SearchFilter {
  using State = int;
  virtual bool is_accepting(State state) const = 0;
  virtual bool has_transition(State from, Symbol symbol, State* to) const = 0;
  virtual ~SearchFilter() = default;
};

class SearchDriver {
 public:
  const char* text = nullptr;
  const SymbolString* symbols = nullptr;
  double score = 0;

  SearchDriver(const IndexReader*, const SearchFilter*, SearchFilter::State start,
               double restart);
  bool step();
  void next() {
    while (!step()) {
    }
  }

 private:
  struct NextChoice {
    int crumb = -1;
    double scale = 1;
    IndexReader::Choice choice;
    SearchFilter::State state = 0;
    bool operator<(const NextChoice& other) const {
      return choice.count * scale < other.choice.count * other.scale;
    }
  };
  struct Crumb {
    int parent = -1;
    Symbol symbol = kEpsilon;
  };

  std::priority_queue<NextChoice> nexts_;
  std::deque<Crumb> crumbs_;
  std::vector<IndexReader::Choice> choices_;
  std::set<SymbolString> seen_;
  const IndexReader* reader_;
  const SearchFilter* filter_;
  double restart_;
  std::string utf8_result_;
};

void PrintAll(SearchDriver*,
              std::size_t max_steps = std::numeric_limits<std::size_t>::max());

#endif  // NUTRIMATIC_SEARCH_H_
