#include "search.h"

#include <algorithm>

SearchDriver::SearchDriver(const IndexReader* reader, const SearchFilter* filter,
                           SearchFilter::State start, double restart)
    : reader_(reader), filter_(filter), restart_(restart) {
  NextChoice seed;
  seed.choice.symbol = kEpsilon;
  seed.choice.next = reader_->root();
  seed.choice.count = reader_->count();
  seed.state = start;
  nexts_.push(seed);
}

bool SearchDriver::step() {
  if (nexts_.empty()) {
    text = nullptr;
    symbols = nullptr;
    score = 0;
    return true;
  }

  const NextChoice next = nexts_.top();
  nexts_.pop();
  NextChoice candidate;
  candidate.crumb = static_cast<int>(crumbs_.size());
  candidate.scale = next.scale;

  choices_.clear();
  if (next.choice.next != 0) {
    reader_->Children(next.choice.next, kEpsilon, kPositionBreak, &choices_);
  }
  for (const IndexReader::Choice& choice : choices_) {
    if (filter_->has_transition(next.state, choice.symbol, &candidate.state)) {
      if (static_cast<int>(crumbs_.size()) == candidate.crumb)
        crumbs_.push_back({next.crumb, next.choice.symbol});
      candidate.choice = choice;
      nexts_.push(candidate);
    }
  }

  if (filter_->is_accepting(next.state) && next.crumb != -1) {
    SymbolString result;
    for (int index = next.crumb; index >= 0; index = crumbs_[index].parent)
      result.push_back(crumbs_[index].symbol);
    std::reverse(result.begin(), result.end());
    result.push_back(next.choice.symbol);
    result.erase(std::remove_if(result.begin(), result.end(), [](Symbol symbol) {
                   return symbol == kEpsilon || symbol == kPositionBreak ||
                          symbol == kEnd;
                 }),
                 result.end());
    const auto inserted = seen_.insert(std::move(result));
    if (inserted.second) {
      symbols = &*inserted.first;
      utf8_result_ = EncodeVisibleUtf8(*symbols);
      text = utf8_result_.c_str();
      score = next.scale * next.choice.count;
      return true;
    }
  }

  if (restart_ > 0 && next.choice.symbol == EncodeScalar(U' ') &&
      next.choice.next != reader_->root()) {
    candidate.crumb = next.crumb;
    candidate.scale = next.scale * next.choice.count / reader_->count() * restart_;
    candidate.choice.symbol = next.choice.symbol;
    candidate.choice.count = reader_->count();
    candidate.choice.next = reader_->root();
    candidate.state = next.state;
    nexts_.push(candidate);
  }
  return false;
}
