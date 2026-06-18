#include "rank-lines-lib.h"

#include "index.h"
#include "search.h"
#include "expr.h"

#include "fst/concat.h"

#include <algorithm>
#include <iomanip>
#include <fstream>
#include <istream>
#include <ostream>
#include <stdio.h>
#include <string.h>
#include <vector>

using namespace fst;

std::string NormalizeLine(const std::string& input) {
  std::string output;
  bool pending_space = false;

  for (unsigned char ch : input) {
    if (ch >= 'A' && ch <= 'Z') ch = ch - 'A' + 'a';

    if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
      if (pending_space && !output.empty()) output.push_back(' ');
      output.push_back(static_cast<char>(ch));
      pending_space = false;
    } else if (ch != '\'') {
      pending_space = true;
    }
  }

  return output;
}

LineRankResult RankLine(const IndexReader& reader,
                        const std::string& input,
                        size_t line_number) {
  LineRankResult result = {0.0, line_number, NormalizeLine(input)};
  if (result.text.empty()) return result;

  StdVectorFst parsed;
  const char* end = ParseExpr(result.text.c_str(), &parsed, false);
  if (end == NULL || *end != '\0') return result;

  StdVectorFst space;
  ParseExpr(" ", &space, true);
  Concat(&parsed, space);

  ExprFilter filter(parsed);
  SearchDriver driver(&reader, &filter, filter.start(), 1e-6);
  driver.next();
  if (driver.text == NULL) return result;

  result.score = driver.score;
  result.text = driver.text;
  while (!result.text.empty() && result.text.back() == ' ') {
    result.text.pop_back();
  }
  return result;
}

static bool WriteResult(const LineRankResult& result, std::ostream& output) {
  output << std::defaultfloat << std::setprecision(8) << result.score << '\t'
         << result.line_number << '\t' << result.text << '\n';
  return static_cast<bool>(output);
}

bool ProcessLines(const IndexReader& reader,
                  std::istream& input,
                  bool sort_results,
                  std::ostream& output) {
  std::vector<LineRankResult> results;
  std::string line;
  size_t line_number = 0;

  while (std::getline(input, line)) {
    LineRankResult result = RankLine(reader, line, ++line_number);
    if (sort_results) {
      results.push_back(result);
    } else {
      if (!WriteResult(result, output)) return false;
      output.flush();
      if (!output) return false;
    }
  }
  if (input.bad() || (!input.eof() && input.fail())) return false;

  if (sort_results) {
    std::stable_sort(results.begin(), results.end(),
        [](const LineRankResult& left, const LineRankResult& right) {
          if (left.score != right.score) return left.score > right.score;
          return left.line_number < right.line_number;
        });
    for (const LineRankResult& result : results) {
      if (!WriteResult(result, output)) return false;
    }
    output.flush();
    if (!output) return false;
  }

  return true;
}

int RunRankLines(int argc,
                 const char* const argv[],
                 std::istream& standard_input,
                 std::ostream& standard_output,
                 std::ostream& standard_error) {
  bool sort_results = true;
  int next_arg = 1;
  if (next_arg < argc && strcmp(argv[next_arg], "--no-sort") == 0) {
    sort_results = false;
    ++next_arg;
  }

  int positional_count = argc - next_arg;
  if (positional_count < 1 || positional_count > 2 ||
      (next_arg < argc && argv[next_arg][0] == '-')) {
    standard_error << "usage: " << argv[0]
                   << " [--no-sort] INDEX [INPUT]\n";
    return 2;
  }

  const char* index_path = argv[next_arg++];
  FILE* index_file = fopen(index_path, "rb");
  if (index_file == NULL) {
    standard_error << "error: can't open \"" << index_path << "\"\n";
    return 1;
  }

  std::ifstream input_file;
  std::istream* input = &standard_input;
  if (next_arg < argc && strcmp(argv[next_arg], "-") != 0) {
    input_file.open(argv[next_arg], std::ios::in | std::ios::binary);
    if (!input_file) {
      standard_error << "error: can't open \"" << argv[next_arg] << "\"\n";
      fclose(index_file);
      return 1;
    }
    input = &input_file;
  }

  bool success;
  {
    IndexReader reader(index_file);
    success = ProcessLines(
        reader, *input, sort_results, standard_output);
  }
  fclose(index_file);

  if (!success) {
    standard_error << "error: failed to read input or write output\n";
    return 1;
  }
  return 0;
}
