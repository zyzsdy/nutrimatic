#ifndef NUTRIMATIC_EXPR_H_
#define NUTRIMATIC_EXPR_H_

#include "search.h"

#include "fst/mutable-fst.h"
#include "fst/vector-fst.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

struct ExprLimits {
  std::uint64_t max_states = 1000000;
  std::uint64_t max_arcs = 20000000;
};

struct ExprCompileContext {
  const std::vector<Symbol>& alphabet;
  ExprLimits limits;
};

struct ExprError {
  std::size_t byte_offset = 0;
  std::string message;
};

bool CompileExpr(std::string_view expression, const ExprCompileContext& context,
                 fst::StdMutableFst* output, ExprError* error,
                 bool quoted = false);

const char* ParseExpr(const char*, fst::StdMutableFst* output, bool quoted);

void OptimizeExpr(const fst::StdFst& input, fst::StdMutableFst* output);
void IntersectExprs(const std::vector<fst::StdVectorFst>& input,
                    fst::StdMutableFst* output);

class ExprFilter : public SearchFilter {
 public:
  explicit ExprFilter(const fst::StdFst& parsed_expr);
  State start() const { return start_state_; }
  bool is_accepting(State state) const override;
  bool has_transition(State from, Symbol symbol, State* to) const override;

 private:
  State start_state_ = 0;
  std::vector<bool> accepting_;
  std::vector<std::vector<std::pair<Symbol, State>>> transitions_;
};

#endif  // NUTRIMATIC_EXPR_H_
