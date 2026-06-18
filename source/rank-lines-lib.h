#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <iosfwd>

class IndexReader;

struct LineRankResult {
  double score;
  size_t line_number;
  std::string text;
};

constexpr size_t kDefaultMaxSearchSteps = 100000;

using LineRanker =
    std::function<LineRankResult(const std::string&, size_t)>;

std::string NormalizeLine(const std::string& input);
LineRankResult RankLine(const IndexReader& reader,
                        const std::string& input,
                        size_t line_number,
                        size_t max_steps = kDefaultMaxSearchSteps);
bool ProcessLines(const IndexReader& reader,
                  std::istream& input,
                  bool sort_results,
                  std::ostream& output,
                  size_t max_steps = kDefaultMaxSearchSteps,
                  size_t thread_count = 0);
bool ProcessLinesWithRanker(std::istream& input,
                            bool sort_results,
                            size_t thread_count,
                            std::ostream& output,
                            const LineRanker& ranker);
int RunRankLines(int argc,
                 const char* const argv[],
                 std::istream& standard_input,
                 std::ostream& standard_output,
                 std::ostream& standard_error);
