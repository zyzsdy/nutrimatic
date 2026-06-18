#pragma once

#include <cstddef>
#include <string>
#include <iosfwd>

class IndexReader;

struct LineRankResult {
  double score;
  size_t line_number;
  std::string text;
};

std::string NormalizeLine(const std::string& input);
LineRankResult RankLine(const IndexReader& reader,
                        const std::string& input,
                        size_t line_number);
bool ProcessLines(const IndexReader& reader,
                  std::istream& input,
                  bool sort_results,
                  std::ostream& output);
int RunRankLines(int argc,
                 const char* const argv[],
                 std::istream& standard_input,
                 std::ostream& standard_output,
                 std::ostream& standard_error);
