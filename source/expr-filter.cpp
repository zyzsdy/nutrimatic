#include "expr.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>

#include "fst/vector-fst.h"

ExprFilter::ExprFilter(const fst::StdFst& raw) {
  fst::StdVectorFst optimized;
  OptimizeExpr(raw, &optimized);
  if (std::getenv("DEBUG_FST") != nullptr)
    optimized.Write(std::getenv("DEBUG_FST"));

  if (optimized.NumStates() == 0) {
    accepting_.resize(1, false);
    transitions_.resize(1);
    return;
  }
  accepting_.resize(optimized.NumStates());
  transitions_.resize(optimized.NumStates());
  start_state_ = optimized.Start();
  for (fst::StateIterator<fst::StdFst> states(optimized); !states.Done();
       states.Next()) {
    const State state = states.Value();
    accepting_[state] = optimized.Final(state) != fst::StdArc::Weight::Zero();
    for (fst::ArcIterator<fst::StdFst> arcs(optimized, state); !arcs.Done();
         arcs.Next()) {
      const fst::StdArc& arc = arcs.Value();
      if (arc.ilabel <= 0 || arc.ilabel > kPositionBreak)
        throw std::runtime_error("expression FST contains an invalid symbol");
      transitions_[state].push_back(
          {static_cast<Symbol>(arc.ilabel), arc.nextstate});
    }
    std::sort(transitions_[state].begin(), transitions_[state].end());
  }
}

bool ExprFilter::is_accepting(State state) const {
  return state >= 0 && static_cast<std::size_t>(state) < accepting_.size() &&
         accepting_[state];
}

bool ExprFilter::has_transition(State from, Symbol symbol, State* to) const {
  if (from < 0 || static_cast<std::size_t>(from) >= transitions_.size()) return false;
  const auto& transitions = transitions_[from];
  const auto found = std::lower_bound(
      transitions.begin(), transitions.end(), symbol,
      [](const std::pair<Symbol, State>& transition, Symbol value) {
        return transition.first < value;
      });
  if (found == transitions.end() || found->first != symbol) return false;
  *to = found->second;
  return true;
}
