#include "rank-lines-lib.h"

#include "index.h"
#include "search.h"
#include "expr.h"

#include "fst/concat.h"

#include <algorithm>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <iomanip>
#include <fstream>
#include <istream>
#include <limits>
#include <ostream>
#include <mutex>
#include <stdio.h>
#include <string.h>
#include <thread>
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
                        size_t line_number,
                        size_t max_steps) {
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
  bool finished = false;
  for (size_t step = 0; step < max_steps; ++step) {
    if (driver.step()) {
      finished = true;
      break;
    }
  }
  if (!finished) {
    result.score = -1.0;
    return result;
  }
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

struct LineTask {
  std::string text;
  size_t line_number;
};

bool ProcessLinesWithRanker(std::istream& input,
                            bool sort_results,
                            size_t thread_count,
                            std::ostream& output,
                            const LineRanker& ranker) {
  if (thread_count == 0) {
    thread_count = std::thread::hardware_concurrency();
    if (thread_count == 0) thread_count = 1;
  }

  std::mutex mutex;
  std::condition_variable task_available;
  std::condition_variable task_space_available;
  std::condition_variable result_available;
  std::deque<LineTask> tasks;
  std::deque<LineRankResult> completed;
  const size_t task_capacity = std::max<size_t>(1, thread_count * 2);
  size_t workers_remaining = thread_count;
  bool input_done = false;
  bool cancelled = false;
  bool output_ok = true;

  std::vector<std::thread> workers;
  workers.reserve(thread_count);
  for (size_t i = 0; i < thread_count; ++i) {
    workers.emplace_back([&] {
      while (true) {
        LineTask task;
        {
          std::unique_lock<std::mutex> lock(mutex);
          task_available.wait(lock, [&] {
            return cancelled || !tasks.empty() || input_done;
          });
          if (cancelled || (tasks.empty() && input_done)) break;
          task = std::move(tasks.front());
          tasks.pop_front();
          task_space_available.notify_one();
        }

        LineRankResult result = ranker(task.text, task.line_number);
        {
          std::lock_guard<std::mutex> lock(mutex);
          if (cancelled) break;
          completed.push_back(std::move(result));
        }
        result_available.notify_one();
      }

      {
        std::lock_guard<std::mutex> lock(mutex);
        --workers_remaining;
      }
      result_available.notify_one();
    });
  }

  std::thread writer([&] {
    std::vector<LineRankResult> results;
    while (true) {
      LineRankResult result;
      {
        std::unique_lock<std::mutex> lock(mutex);
        result_available.wait(lock, [&] {
          return cancelled || !completed.empty() || workers_remaining == 0;
        });
        if (cancelled) return;
        if (completed.empty() && workers_remaining == 0) break;
        result = std::move(completed.front());
        completed.pop_front();
      }

      if (sort_results) {
        results.push_back(std::move(result));
      } else {
        if (!WriteResult(result, output)) {
          std::lock_guard<std::mutex> lock(mutex);
          output_ok = false;
          cancelled = true;
          task_available.notify_all();
          task_space_available.notify_all();
          return;
        }
        output.flush();
        if (!output) {
          std::lock_guard<std::mutex> lock(mutex);
          output_ok = false;
          cancelled = true;
          task_available.notify_all();
          task_space_available.notify_all();
          return;
        }
      }
    }

    if (sort_results) {
      std::stable_sort(results.begin(), results.end(),
          [](const LineRankResult& left, const LineRankResult& right) {
            if (left.score != right.score) return left.score > right.score;
            return left.line_number < right.line_number;
          });
      for (const LineRankResult& result : results) {
        if (!WriteResult(result, output)) {
          output_ok = false;
          return;
        }
      }
      output.flush();
      if (!output) output_ok = false;
    }
  });

  std::string line;
  size_t line_number = 0;
  while (std::getline(input, line)) {
    std::unique_lock<std::mutex> lock(mutex);
    task_space_available.wait(lock, [&] {
      return cancelled || tasks.size() < task_capacity;
    });
    if (cancelled) break;
    tasks.push_back(LineTask{line, ++line_number});
    task_available.notify_one();
  }
  const bool input_ok = !input.bad() && (input.eof() || !input.fail());

  {
    std::lock_guard<std::mutex> lock(mutex);
    input_done = true;
  }
  task_available.notify_all();

  for (std::thread& worker : workers) worker.join();
  writer.join();
  return input_ok && output_ok;
}

static bool ParsePositiveSize(const char* text, size_t* value) {
  if (text == NULL || *text == '\0' || *text == '-') return false;
  char* end = NULL;
  errno = 0;
  unsigned long long parsed = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    return false;
  }
  *value = static_cast<size_t>(parsed);
  return true;
}

bool ProcessLines(const IndexReader& reader,
                  std::istream& input,
                  bool sort_results,
                  std::ostream& output,
                  size_t max_steps,
                  size_t thread_count) {
  return ProcessLinesWithRanker(
      input, sort_results, thread_count, output,
      [&](const std::string& text, size_t line_number) {
        return RankLine(reader, text, line_number, max_steps);
      });
}

int RunRankLines(int argc,
                 const char* const argv[],
                 std::istream& standard_input,
                 std::ostream& standard_output,
                 std::ostream& standard_error) {
  bool sort_results = true;
  size_t max_steps = kDefaultMaxSearchSteps;
  size_t thread_count = 0;
  int next_arg = 1;
  while (next_arg < argc && argv[next_arg][0] == '-') {
    if (strcmp(argv[next_arg], "--no-sort") == 0) {
      sort_results = false;
      ++next_arg;
    } else if (strcmp(argv[next_arg], "--max-steps") == 0) {
      if (++next_arg >= argc ||
          !ParsePositiveSize(argv[next_arg], &max_steps)) {
        standard_error << "error: --max-steps must be a positive integer\n";
        return 2;
      }
      ++next_arg;
    } else if (strcmp(argv[next_arg], "--threads") == 0) {
      if (++next_arg >= argc ||
          !ParsePositiveSize(argv[next_arg], &thread_count)) {
        standard_error << "error: --threads must be a positive integer\n";
        return 2;
      }
      ++next_arg;
    } else {
      standard_error << "error: unknown option \"" << argv[next_arg]
                     << "\"\n";
      return 2;
    }
  }

  int positional_count = argc - next_arg;
  if (positional_count < 1 || positional_count > 2 ||
      (next_arg < argc && argv[next_arg][0] == '-')) {
    standard_error << "usage: " << argv[0]
                   << " [--no-sort] [--max-steps N] [--threads N]"
                   << " INDEX [INPUT]\n";
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
        reader, *input, sort_results, standard_output,
        max_steps, thread_count);
  }
  fclose(index_file);

  if (!success) {
    standard_error << "error: failed to read input or write output\n";
    return 1;
  }
  return 0;
}
