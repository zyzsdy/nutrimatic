#include "expr.h"

#include "unicode.h"

#include "fst/closure.h"
#include "fst/concat.h"
#include "fst/equivalent.h"
#include "fst/union.h"

#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {

using fst::StdArc;
using fst::StdMutableFst;
using fst::StdVectorFst;

struct ParseFailure : std::runtime_error {
  ParseFailure(std::size_t offset, const std::string& message)
      : std::runtime_error(message), offset(offset) {}
  std::size_t offset;
};

std::uint64_t ArcCount(const fst::StdFst& fst) {
  std::uint64_t count = 0;
  for (fst::StateIterator<fst::StdFst> states(fst); !states.Done(); states.Next()) {
    const std::uint64_t arcs = fst.NumArcs(states.Value());
    if (arcs > UINT64_MAX - count) return UINT64_MAX;
    count += arcs;
  }
  return count;
}

std::uint64_t StateCount(const fst::StdFst& fst) {
  std::uint64_t count = 0;
  for (fst::StateIterator<fst::StdFst> states(fst); !states.Done(); states.Next())
    ++count;
  return count;
}

void MakeEpsilon(StdMutableFst* fst) {
  fst->DeleteStates();
  const auto state = fst->AddState();
  fst->SetStart(state);
  fst->SetFinal(state, StdArc::Weight::One());
}

void MakeSymbols(const std::vector<Symbol>& symbols, StdMutableFst* fst) {
  fst->DeleteStates();
  const auto start = fst->AddState();
  const auto final = fst->AddState();
  fst->SetStart(start);
  fst->SetFinal(final, StdArc::Weight::One());
  for (Symbol symbol : symbols)
    fst->AddArc(start, StdArc(symbol, symbol, StdArc::Weight::One(), final));
}

void MakeSequence(const SymbolString& symbols, StdMutableFst* fst) {
  fst->DeleteStates();
  auto state = fst->AddState();
  fst->SetStart(state);
  for (Symbol symbol : symbols) {
    const auto next = fst->AddState();
    fst->AddArc(state, StdArc(symbol, symbol, StdArc::Weight::One(), next));
    state = next;
  }
  fst->SetFinal(state, StdArc::Weight::One());
}

class Parser {
 public:
  Parser(std::string_view expression, const ExprCompileContext& context)
      : expression_(expression), context_(context) {}

  void Parse(StdMutableFst* output, bool quoted) {
    ParseExpression(output, quoted);
    if (position_ != expression_.size())
      Fail("unexpected syntax character");
    Check(*output);
  }

 private:
  bool AtEnd() const { return position_ == expression_.size(); }
  char Peek() const { return AtEnd() ? '\0' : expression_[position_]; }
  void Fail(const std::string& message) const {
    throw ParseFailure(position_, message);
  }
  void Check(const fst::StdFst& fst) const {
    if (StateCount(fst) > context_.limits.max_states ||
        ArcCount(fst) > context_.limits.max_arcs)
      throw ParseFailure(position_, "expression exceeds FST resource limits");
  }
  bool CanStartAtom(bool quoted) const {
    const char ch = Peek();
    return ch != '\0' && ch != '|' && ch != '&' && ch != ')' && ch != '>' &&
           (ch != '"' || !quoted);
  }

  void ParseExpression(StdMutableFst* output, bool quoted) {
    ParseBranch(output, quoted);
    while (Peek() == '|') {
      ++position_;
      StdVectorFst branch;
      ParseBranch(&branch, quoted);
      fst::Union(output, branch);
      Check(*output);
    }
  }

  void ParseBranch(StdMutableFst* output, bool quoted) {
    std::vector<StdVectorFst> factors;
    StdVectorFst first;
    ParseFactor(&first, quoted);
    factors.push_back(first);
    while (Peek() == '&') {
      ++position_;
      StdVectorFst next;
      ParseFactor(&next, quoted);
      factors.push_back(next);
    }
    IntersectExprs(factors, output);
    Check(*output);
  }

  void ParseFactor(StdMutableFst* output, bool quoted) {
    MakeEpsilon(output);
    while (CanStartAtom(quoted)) {
      StdVectorFst piece;
      ParsePiece(&piece, quoted);
      fst::Concat(output, piece);
      Check(*output);
    }
  }

  void ParsePiece(StdMutableFst* output, bool quoted) {
    StdVectorFst atom;
    ParseAtom(&atom, quoted);
    int minimum = 1;
    int maximum = 1;
    if (Peek() == '*') {
      minimum = 0;
      maximum = INT_MAX;
      ++position_;
    } else if (Peek() == '+') {
      maximum = INT_MAX;
      ++position_;
    } else if (Peek() == '?') {
      minimum = 0;
      ++position_;
    } else if (Peek() == '{') {
      ++position_;
      minimum = ParseNumber();
      maximum = minimum;
      if (Peek() == ',') {
        ++position_;
        maximum = Peek() == '}' ? INT_MAX : ParseNumber();
      }
      if (Peek() != '}') Fail("unterminated quantifier");
      ++position_;
      if (maximum < minimum || (maximum != INT_MAX && maximum > 255))
        Fail("invalid quantifier range");
    }

    MakeEpsilon(output);
    for (int i = 0; i < minimum; ++i) fst::Concat(output, atom);
    if (maximum == INT_MAX) {
      StdVectorFst repeated(atom);
      fst::Closure(&repeated, fst::CLOSURE_STAR);
      fst::Concat(output, repeated);
    } else {
      for (int i = minimum; i < maximum; ++i) {
        StdVectorFst optional(atom);
        StdVectorFst epsilon;
        MakeEpsilon(&epsilon);
        fst::Union(&optional, epsilon);
        fst::Concat(output, optional);
      }
    }
    Check(*output);
  }

  int ParseNumber() {
    if (Peek() < '0' || Peek() > '9') Fail("expected number");
    unsigned long long value = 0;
    while (Peek() >= '0' && Peek() <= '9') {
      value = value * 10 + static_cast<unsigned int>(Peek() - '0');
      if (value > INT_MAX) Fail("quantifier is too large");
      ++position_;
    }
    return static_cast<int>(value);
  }

  void AddTransparentBoundaries(StdMutableFst* fst, bool quoted) {
    const auto start = fst->Start();
    std::vector<fst::StdArc::StateId> finals;
    for (fst::StateIterator<fst::StdFst> states(*fst); !states.Done(); states.Next())
      if (fst->Final(states.Value()) != StdArc::Weight::Zero())
        finals.push_back(states.Value());
    fst->AddArc(start, StdArc(kPositionBreak, kPositionBreak,
                              StdArc::Weight::One(), start));
    for (const auto final : finals)
      fst->AddArc(final, StdArc(kPositionBreak, kPositionBreak,
                                StdArc::Weight::One(), final));
    if (!quoted) {
      const Symbol space = EncodeScalar(U' ');
      fst->AddArc(start, StdArc(space, space, StdArc::Weight::One(), start));
      for (const auto final : finals)
        fst->AddArc(final, StdArc(space, space, StdArc::Weight::One(), final));
    }
  }

  void ParseAtom(StdMutableFst* output, bool quoted) {
    if (Peek() == '"' && !quoted) {
      ++position_;
      ParseExpression(output, true);
      if (Peek() != '"') Fail("unterminated quote");
      ++position_;
      return;
    }
    if (Peek() == '(') {
      ++position_;
      ParseExpression(output, quoted);
      if (Peek() != ')') Fail("unterminated group");
      ++position_;
      return;
    }
    if (Peek() == '<') {
      ++position_;
      ParseAnagram(output, quoted);
      if (Peek() != '>') Fail("unterminated anagram");
      ++position_;
      return;
    }

    std::vector<Symbol> symbols;
    if (Peek() == '[') {
      ParseCharacterClass(&symbols);
      MakeSymbols(symbols, output);
    } else if (Peek() == '.' || Peek() == '_' || Peek() == '#' ||
               Peek() == 'A' || Peek() == 'C' || Peek() == 'V' ||
               Peek() == '-') {
      ParseClassElement(&symbols, false);
      MakeSymbols(symbols, output);
    } else {
      MakeSequence(ParseLiteral(), output);
    }
    AddTransparentBoundaries(output, quoted);
  }

  void ParseCharacterClass(std::vector<Symbol>* symbols) {
    ++position_;
    bool negate = false;
    if (Peek() == '^') {
      negate = true;
      ++position_;
    }
    if (Peek() == ']') Fail("empty character class");
    while (!AtEnd() && Peek() != ']') {
      const std::size_t first_offset = position_;
      std::vector<Symbol> first;
      ParseClassElement(&first, true);
      if (Peek() == '-' && position_ + 1 < expression_.size() &&
          expression_[position_ + 1] != ']') {
        ++position_;
        const std::size_t last_offset = position_;
        std::vector<Symbol> last;
        ParseClassElement(&last, true);
        if (first.size() != 1 || last.size() != 1 ||
            first[0] > EncodeScalar(U'z') || last[0] > EncodeScalar(U'z') ||
            !((first[0] >= EncodeScalar(U'a') && last[0] <= EncodeScalar(U'z')) ||
              (first[0] >= EncodeScalar(U'0') && last[0] <= EncodeScalar(U'9')))) {
          throw ParseFailure(first_offset,
                             "non-ASCII character ranges are not supported");
        }
        if (last[0] < first[0])
          throw ParseFailure(last_offset, "descending character range");
        for (Symbol symbol = first[0]; symbol <= last[0]; ++symbol)
          symbols->push_back(symbol);
      } else {
        symbols->insert(symbols->end(), first.begin(), first.end());
      }
    }
    if (Peek() != ']') Fail("unterminated character class");
    ++position_;
    std::sort(symbols->begin(), symbols->end());
    symbols->erase(std::unique(symbols->begin(), symbols->end()), symbols->end());
    if (negate) {
      std::vector<Symbol> all = context_.alphabet;
      all.push_back(EncodeScalar(U' '));
      std::sort(all.begin(), all.end());
      all.erase(std::unique(all.begin(), all.end()), all.end());
      std::vector<Symbol> complement;
      std::set_difference(all.begin(), all.end(), symbols->begin(), symbols->end(),
                          std::back_inserter(complement));
      *symbols = std::move(complement);
    }
  }

  void ParseClassElement(std::vector<Symbol>* output, bool in_class) {
    if (AtEnd()) Fail("expected expression atom");
    const char ch = Peek();
    if (ch == '.' || ch == '_' || ch == '#' || ch == 'A' || ch == 'C' ||
        ch == 'V' || (ch == '-' && !in_class)) {
      ++position_;
      if (ch == '.') {
        *output = context_.alphabet;
        output->push_back(EncodeScalar(U' '));
      } else if (ch == '_') {
        *output = context_.alphabet;
        output->erase(std::remove(output->begin(), output->end(),
                                  EncodeScalar(U' ')),
                      output->end());
      } else if (ch == '#') {
        for (char32_t cp = U'0'; cp <= U'9'; ++cp) output->push_back(EncodeScalar(cp));
      } else if (ch == 'A') {
        for (char32_t cp = U'a'; cp <= U'z'; ++cp) output->push_back(EncodeScalar(cp));
      } else if (ch == 'C') {
        for (char32_t cp = U'a'; cp <= U'z'; ++cp)
          if (std::strchr("aeiou", static_cast<char>(cp)) == nullptr)
            output->push_back(EncodeScalar(cp));
      } else if (ch == 'V') {
        for (const char* vowel = "aeiou"; *vowel; ++vowel)
          output->push_back(EncodeScalar(*vowel));
      } else {
        output->push_back(kEpsilon);
        output->push_back(EncodeScalar(U' '));
      }
      std::sort(output->begin(), output->end());
      output->erase(std::unique(output->begin(), output->end()), output->end());
      return;
    }

    const SymbolString normalized = ParseLiteral();
    if (in_class && normalized.size() != 1)
      throw ParseFailure(position_,
                         "character class element must normalize to one code point");
    output->insert(output->end(), normalized.begin(), normalized.end());
  }

  SymbolString ParseLiteral() {
    const std::size_t start = position_;
    std::size_t length = 1;
    const unsigned char first = static_cast<unsigned char>(expression_[position_]);
    if (first >= 0xC2 && first <= 0xDF) length = 2;
    else if (first >= 0xE0 && first <= 0xEF) length = 3;
    else if (first >= 0xF0 && first <= 0xF4) length = 4;
    if (start + length > expression_.size()) Fail("invalid UTF-8 literal");
    const std::string_view raw = expression_.substr(start, length);
    try {
      DecodeUtf8Strict(raw);
      const SymbolString normalized = NormalizeQueryLiteral(raw);
      if (normalized.empty())
        throw ParseFailure(start, "literal normalizes to an empty sequence");
      position_ += length;
      return normalized;
    } catch (const Utf8Error&) {
      throw ParseFailure(start, "invalid UTF-8 literal");
    } catch (const UnicodeInputError& error) {
      throw ParseFailure(start, error.what());
    }
    throw ParseFailure(start, "invalid Unicode literal");
  }

  void ParseAnagram(StdMutableFst* output, bool quoted) {
    struct Part {
      StdVectorFst expression;
      int count = 1;
    };
    std::vector<Part> parts;
    while (!AtEnd() && Peek() != '>') {
      StdVectorFst expression;
      ParsePiece(&expression, quoted);
      StdVectorFst optimized;
      OptimizeExpr(expression, &optimized);
      bool merged = false;
      for (Part& part : parts) {
        if (fst::Equivalent(part.expression, optimized)) {
          ++part.count;
          merged = true;
          break;
        }
      }
      if (!merged) parts.push_back({optimized, 1});
    }
    if (parts.empty()) Fail("empty anagram");

    StdVectorFst any;
    int total = 0;
    for (const Part& part : parts) {
      fst::Union(&any, part.expression);
      total += part.count;
    }
    StdVectorFst length;
    MakeEpsilon(&length);
    for (int i = 0; i < total; ++i) fst::Concat(&length, any);
    std::vector<StdVectorFst> intersections = {length};
    for (std::size_t i = 0; i < parts.size(); ++i) {
      StdVectorFst others;
      for (std::size_t j = 0; j < parts.size(); ++j)
        if (i != j) fst::Union(&others, parts[j].expression);
      if (others.NumStates() == 0) MakeEpsilon(&others);
      fst::Closure(&others, fst::CLOSURE_STAR);
      StdVectorFst contains(others);
      for (int n = 0; n < parts[i].count; ++n) {
        fst::Concat(&contains, parts[i].expression);
        fst::Concat(&contains, others);
      }
      intersections.push_back(contains);
    }
    IntersectExprs(intersections, output);
    Check(*output);
  }

  std::string_view expression_;
  const ExprCompileContext& context_;
  std::size_t position_ = 0;
};

}  // namespace

bool CompileExpr(std::string_view expression, const ExprCompileContext& context,
                 fst::StdMutableFst* output, ExprError* error, bool quoted) {
  try {
    Parser(expression, context).Parse(output, quoted);
    return true;
  } catch (const ParseFailure& failure) {
    if (error != nullptr) {
      error->byte_offset = failure.offset;
      error->message = failure.what();
    }
    output->DeleteStates();
    return false;
  } catch (const std::exception& failure) {
    if (error != nullptr) {
      error->byte_offset = 0;
      error->message = failure.what();
    }
    output->DeleteStates();
    return false;
  }
}

const char* ParseExpr(const char* expression, StdMutableFst* output, bool quoted) {
  static const std::vector<Symbol> alphabet = [] {
    std::vector<Symbol> result;
    for (char32_t cp = U'0'; cp <= U'9'; ++cp) result.push_back(EncodeScalar(cp));
    for (char32_t cp = U'a'; cp <= U'z'; ++cp) result.push_back(EncodeScalar(cp));
    result.push_back(EncodeScalar(U' '));
    std::sort(result.begin(), result.end());
    return result;
  }();
  const ExprCompileContext context{alphabet, {}};
  ExprError error;
  if (!CompileExpr(expression, context, output, &error, quoted)) {
    if (std::getenv("DEBUG_FST") != nullptr)
      std::fprintf(stderr, "parse error at byte %zu: %s\n", error.byte_offset,
                   error.message.c_str());
    return nullptr;
  }
  return expression + std::strlen(expression);
}
