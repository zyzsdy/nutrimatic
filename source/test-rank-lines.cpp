#include "rank-lines-lib.h"
#include "index.h"

#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <sstream>
#include <fstream>
#include <vector>

static void ExpectTrue(const char* name, bool value);

struct OutputRow {
  double score;
  size_t line_number;
  std::string text;
};

static std::vector<OutputRow> ParseRows(const std::string& output) {
  std::vector<OutputRow> rows;
  std::istringstream lines(output);
  std::string line;
  while (std::getline(lines, line)) {
    size_t first_tab = line.find('\t');
    size_t second_tab = line.find('\t', first_tab + 1);
    ExpectTrue("TSV first separator", first_tab != std::string::npos);
    ExpectTrue("TSV second separator", second_tab != std::string::npos);
    rows.push_back({
        std::stod(line.substr(0, first_tab)),
        static_cast<size_t>(std::stoull(
            line.substr(first_tab + 1, second_tab - first_tab - 1))),
        line.substr(second_tab + 1),
    });
  }
  return rows;
}

class CountingBuffer : public std::stringbuf {
 public:
  int sync_count = 0;

  int sync() override {
    ++sync_count;
    return std::stringbuf::sync();
  }
};

class FailingSyncBuffer : public std::stringbuf {
 public:
  int sync() override { return -1; }
};

static void ExpectTrue(const char* name, bool value) {
  if (!value) {
    fprintf(stderr, "FAIL: %s\n", name);
    exit(1);
  }
}

static void WriteTestIndex() {
  FILE* fp = fopen("test-rank-lines.index", "wb");
  if (fp == NULL) {
    fprintf(stderr, "FAIL: cannot create test index\n");
    exit(1);
  }

  IndexWriter writer(fp);
  writer.next("cat ", 0, 100);
  writer.next("cat dog ", 3, 80);
  writer.next("catfish ", 3, 5);
  writer.next("dog ", 0, 90);
  writer.next(NULL, 0, 0);
  fclose(fp);
}

static void ExpectEqual(const char* name,
                        const std::string& actual,
                        const std::string& expected) {
  if (actual != expected) {
    fprintf(stderr, "FAIL: %s: got [%s], expected [%s]\n",
            name, actual.c_str(), expected.c_str());
    exit(1);
  }
}

int main() {
  ExpectEqual("normalization",
              NormalizeLine("  I'M--A\tTeST! \xE2\x98\x83  "),
              "im a test");
  ExpectEqual("empty normalization", NormalizeLine("'''!!!"), "");

  WriteTestIndex();
  FILE* fp = fopen("test-rank-lines.index", "rb");
  if (fp == NULL) {
    fprintf(stderr, "FAIL: cannot open test index\n");
    return 1;
  }
  {
    IndexReader reader(fp);
    LineRankResult joined = RankLine(reader, "catdog", 7);
    ExpectEqual("best segmentation", joined.text, "cat dog");
    ExpectTrue("best segmentation score", joined.score > 0.0);
    ExpectTrue("line number", joined.line_number == 7);

    LineRankResult fixed = RankLine(reader, "cat dog", 8);
    ExpectEqual("fixed existing space", fixed.text, "cat dog");

    LineRankResult missing = RankLine(reader, "zebra", 9);
    ExpectEqual("unmatched text", missing.text, "zebra");
    ExpectTrue("unmatched score", missing.score == 0.0);

    LineRankResult empty = RankLine(reader, "!!!", 10);
    ExpectEqual("empty text", empty.text, "");
    ExpectTrue("empty score", empty.score == 0.0);

    std::istringstream sorted_input(
        "catfish\ncatdog\nmissing\ncat\ncatfish\n");
    std::ostringstream sorted_output;
    ExpectTrue("process sorted lines",
               ProcessLines(reader, sorted_input, true, sorted_output));
    std::vector<OutputRow> sorted_rows = ParseRows(sorted_output.str());
    ExpectTrue("sorted row count", sorted_rows.size() == 5);
    ExpectEqual("sorted first text", sorted_rows[0].text, "cat");
    ExpectEqual("sorted second text", sorted_rows[1].text, "cat dog");
    ExpectEqual("sorted third text", sorted_rows[2].text, "catfish");
    ExpectEqual("sorted fourth text", sorted_rows[3].text, "catfish");
    ExpectTrue("equal scores preserve line order",
               sorted_rows[2].line_number == 1 &&
               sorted_rows[3].line_number == 5);
    ExpectEqual("sorted unmatched text", sorted_rows[4].text, "missing");
    for (size_t i = 1; i < sorted_rows.size(); ++i) {
      ExpectTrue("scores descending",
                 sorted_rows[i - 1].score >= sorted_rows[i].score);
    }

    std::istringstream streaming_input("catfish\ncatdog\nmissing\ncat\n");
    CountingBuffer streaming_buffer;
    std::ostream streaming_output(&streaming_buffer);
    ExpectTrue("process streaming lines",
               ProcessLines(reader, streaming_input, false, streaming_output));
    std::vector<OutputRow> streaming_rows = ParseRows(streaming_buffer.str());
    ExpectTrue("streaming row count", streaming_rows.size() == 4);
    ExpectEqual("streaming first text", streaming_rows[0].text, "catfish");
    ExpectEqual("streaming second text", streaming_rows[1].text, "cat dog");
    ExpectEqual("streaming third text", streaming_rows[2].text, "missing");
    ExpectEqual("streaming fourth text", streaming_rows[3].text, "cat");
    ExpectTrue("streaming flushes each line", streaming_buffer.sync_count == 4);

    std::istringstream failing_output_input("cat\n");
    FailingSyncBuffer failing_buffer;
    std::ostream failing_output(&failing_buffer);
    ExpectTrue("sorted output failure is reported",
               !ProcessLines(
                   reader, failing_output_input, true, failing_output));
  }
  fclose(fp);

  {
    const char* stdin_argv[] = {"rank-lines", "test-rank-lines.index"};
    std::istringstream stdin_input("catdog\n");
    std::ostringstream stdin_output;
    std::ostringstream stdin_error;
    ExpectTrue("stdin exit status",
               RunRankLines(2, stdin_argv, stdin_input,
                            stdin_output, stdin_error) == 0);
    std::vector<OutputRow> stdin_rows = ParseRows(stdin_output.str());
    ExpectTrue("stdin row count", stdin_rows.size() == 1);
    ExpectEqual("stdin output text", stdin_rows[0].text, "cat dog");
    ExpectEqual("stdin error", stdin_error.str(), "");

    std::ofstream input_file("test-rank-lines.txt", std::ios::binary);
    input_file << "catdog\n";
    input_file.close();
    const char* file_argv[] = {
        "rank-lines", "test-rank-lines.index", "test-rank-lines.txt"};
    std::istringstream unused_input;
    std::ostringstream file_output;
    std::ostringstream file_error;
    ExpectTrue("file exit status",
               RunRankLines(3, file_argv, unused_input,
                            file_output, file_error) == 0);
    ExpectEqual("file and stdin agree", file_output.str(), stdin_output.str());

    const char* no_sort_argv[] = {
        "rank-lines", "--no-sort", "test-rank-lines.index"};
    std::istringstream no_sort_input("catfish\ncatdog\n");
    std::ostringstream no_sort_output;
    std::ostringstream no_sort_error;
    ExpectTrue("no-sort exit status",
               RunRankLines(3, no_sort_argv, no_sort_input,
                            no_sort_output, no_sort_error) == 0);
    std::vector<OutputRow> no_sort_rows = ParseRows(no_sort_output.str());
    ExpectTrue("no-sort row count", no_sort_rows.size() == 2);
    ExpectEqual("no-sort first text", no_sort_rows[0].text, "catfish");
    ExpectEqual("no-sort second text", no_sort_rows[1].text, "cat dog");

    const char* bad_argv[] = {"rank-lines"};
    std::ostringstream bad_error;
    ExpectTrue("usage exit status",
               RunRankLines(1, bad_argv, unused_input,
                            file_output, bad_error) == 2);
    ExpectTrue("usage diagnostic",
               bad_error.str().find("usage:") != std::string::npos);

    const char* missing_argv[] = {"rank-lines", "missing.index"};
    std::ostringstream missing_error;
    ExpectTrue("missing index exit status",
               RunRankLines(2, missing_argv, unused_input,
                            file_output, missing_error) == 1);
    ExpectTrue("missing index diagnostic",
               missing_error.str().find("can't open") != std::string::npos);

    const char* missing_input_argv[] = {
        "rank-lines", "test-rank-lines.index", "missing.txt"};
    std::ostringstream missing_input_error;
    ExpectTrue("missing input exit status",
               RunRankLines(3, missing_input_argv, unused_input,
                            file_output, missing_input_error) == 1);
    ExpectTrue("missing input diagnostic",
               missing_input_error.str().find("can't open") !=
                   std::string::npos);
  }
  remove("test-rank-lines.txt");
  remove("test-rank-lines.index");
  return 0;
}
