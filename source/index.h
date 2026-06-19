#ifndef NUTRIMATIC_INDEX_H_
#define NUTRIMATIC_INDEX_H_

#include "index-format.h"
#include "unicode.h"

#include <cstdio>
#include <cstddef>
#include <cstdint>
#include <set>
#include <vector>

class IndexWriter {
 public:
  IndexWriter(FILE* file, const IndexMetadata& metadata);
  ~IndexWriter();
  void Next(const SymbolString* chain, std::size_t same, std::uint64_t count);
  void Finish();

 private:
  struct Saved {
    Symbol symbol = 0;
    std::uint64_t count = 0;
    std::uint64_t node_offset = 0;
  };
  struct Pending {
    Symbol symbol = 0;
    std::uint64_t terminal_count = 0;
    std::vector<Saved> choices;
  };

  Saved WritePending(const Pending& pending);
  void WriteBytes(const std::vector<std::uint8_t>& bytes);

  FILE* file_;
  IndexMetadata metadata_;
  std::uint64_t position_ = 0;
  std::vector<Pending> chain_;
  std::size_t chain_size_ = 0;
  std::set<Symbol> alphabet_;
  bool finished_ = false;
};

class IndexReader {
 public:
  using Node = std::uint64_t;
  struct Choice {
    Symbol symbol = 0;
    std::uint64_t count = 0;
    Node next = 0;
  };

  explicit IndexReader(FILE* file);
  ~IndexReader();
  IndexReader(const IndexReader&) = delete;
  IndexReader& operator=(const IndexReader&) = delete;
  const IndexMetadata& metadata() const { return metadata_; }
  const std::vector<Symbol>& alphabet() const { return alphabet_; }
  Node root() const { return footer_.root_offset; }
  std::uint64_t count() const { return footer_.root_count; }
  std::uint64_t Children(Node parent, Symbol min, Symbol max,
                         std::vector<Choice>* out) const;

 private:
  void Unmap() noexcept;

  const std::uint8_t* data_ = nullptr;
  std::size_t data_size_ = 0;
#ifdef _WIN32
  void* mapping_handle_ = nullptr;
#endif
  IndexMetadata metadata_;
  IndexFooter footer_;
  std::vector<Symbol> alphabet_;
};

class IndexWalker {
 public:
  const SymbolString* chain = nullptr;
  std::size_t same = 0;
  std::uint64_t count = 0;

  IndexWalker(const IndexReader* reader, IndexReader::Node node,
              std::uint64_t count);
  void Next();

 private:
  struct State {
    std::vector<IndexReader::Choice> choices;
    std::size_t next = 0;
  };

  const IndexReader* reader_;
  SymbolString buffer_;
  std::vector<State> stack_;
  std::size_t stack_size_ = 0;
};

#endif  // NUTRIMATIC_INDEX_H_
