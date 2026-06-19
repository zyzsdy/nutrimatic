#include "index.h"
#include "cli-utf8.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace {

bool IsFrequencyBoundary(Symbol symbol) {
  return symbol == EncodeScalar(U' ') || symbol == kPositionBreak ||
         symbol == kEnd;
}

struct ReaderCompare {
  bool operator()(const IndexWalker* left, const IndexWalker* right) const {
    return *left->chain > *right->chain;
  }
};

class FrequencyCutoffWriter {
 public:
  FrequencyCutoffWriter(IndexWriter* output, std::uint64_t cutoff)
      : output_(output), cutoff_(cutoff) {
    boundaries_.push_back({0, 0});
  }

  void Next(const SymbolString* chain, std::size_t same, std::uint64_t count) {
    if (chain != nullptr) {
      while (same < saved_.size() && same < chain->size() &&
             (*chain)[same] == saved_[same]) ++same;
      if (!std::equal(saved_.begin(), saved_.begin() + same, chain->begin()))
        throw IndexFormatError("merge input is not sorted");
      if (std::lexicographical_compare(chain->begin() + same, chain->end(),
                                       saved_.begin() + same, saved_.end()))
        throw IndexFormatError("merge input is not sorted");
    }

    while (boundaries_.back().first > same) {
      const auto boundary = boundaries_.back();
      boundaries_.pop_back();
      saved_.resize(boundary.first);
      output_same_ = std::min(output_same_, saved_.size());
      if (boundary.second >= cutoff_ ||
          (boundary.second > 0 && output_same_ == boundary.first)) {
        output_->Next(&saved_, output_same_, boundary.second);
        output_same_ = boundaries_.back().first;
      } else {
        if (boundary.second > UINT64_MAX - boundaries_.back().second)
          throw IndexFormatError("merged frequency overflow");
        boundaries_.back().second += boundary.second;
        output_same_ = std::min(output_same_, boundaries_.back().first);
      }
    }

    saved_.resize(same);
    if (chain != nullptr) {
      saved_.insert(saved_.end(), chain->begin() + same, chain->end());
      for (std::size_t i = same; i < chain->size(); ++i) {
        if (IsFrequencyBoundary((*chain)[i])) boundaries_.push_back({i + 1, 0});
      }
    }
    if (count > UINT64_MAX - boundaries_.back().second)
      throw IndexFormatError("merged frequency overflow");
    boundaries_.back().second += count;
  }

  void Finish() {
    Next(nullptr, 0, 0);
    output_->Finish();
  }

 private:
  IndexWriter* output_;
  std::uint64_t cutoff_;
  std::size_t output_same_ = 0;
  SymbolString saved_;
  std::vector<std::pair<std::size_t, std::uint64_t>> boundaries_;
};

}  // namespace

int main(int argc, char** argv) {
  Utf8CommandLine command_line(argc, argv);
  argc = command_line.argc();
  argv = command_line.argv();
  ConfigureBinaryStandardStreams();
  if (argc < 4) {
    std::fprintf(stderr, "usage: %s min input.index ... out.index\n", argv[0]);
    return 2;
  }
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(argv[1], &end, 10);
  if (end == argv[1] || *end != '\0' || parsed == 0) {
    std::fprintf(stderr, "error: illegal frequency threshold \"%s\"\n", argv[1]);
    return 2;
  }

  try {
    std::priority_queue<IndexWalker*, std::vector<IndexWalker*>, ReaderCompare> queue;
    std::vector<std::unique_ptr<IndexReader>> readers;
    std::vector<std::unique_ptr<IndexWalker>> walkers;
    IndexMetadata metadata;
    bool have_metadata = false;
    for (int i = 2; i < argc - 1; ++i) {
      FILE* file = OpenFileUtf8(argv[i], "rb");
      if (file == nullptr)
        throw IndexFormatError(std::string("cannot read \"") + argv[i] + "\"");
      auto reader = std::make_unique<IndexReader>(file);
      std::fclose(file);
      if (!have_metadata) {
        metadata = reader->metadata();
        have_metadata = true;
      } else if (reader->metadata() != metadata) {
        throw IndexFormatError(std::string("index metadata mismatch in \"") +
                               argv[i] + "\"");
      }
      auto walker =
          std::make_unique<IndexWalker>(reader.get(), reader->root(), reader->count());
      if (walker->chain != nullptr) queue.push(walker.get());
      readers.push_back(std::move(reader));
      walkers.push_back(std::move(walker));
    }

    FILE* existing = OpenFileUtf8(argv[argc - 1], "rb");
    if (existing != nullptr) {
      std::fclose(existing);
      throw IndexFormatError(std::string("output already exists: \"") +
                             argv[argc - 1] + "\"");
    }
    FILE* output_file = OpenFileUtf8(argv[argc - 1], "wb");
    if (output_file == nullptr)
      throw IndexFormatError(std::string("cannot write \"") + argv[argc - 1] +
                             "\"");
    IndexWriter output(output_file, metadata);
    FrequencyCutoffWriter writer(&output, parsed);
    while (!queue.empty()) {
      IndexWalker* next = queue.top();
      queue.pop();
      writer.Next(next->chain, next->same, next->count);
      next->Next();
      if (next->chain != nullptr) queue.push(next);
    }
    writer.Finish();
    std::fclose(output_file);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
