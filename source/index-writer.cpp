#include "index.h"

#include <algorithm>
#include <limits>

namespace {

std::uint64_t AddChecked(std::uint64_t left, std::uint64_t right) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    throw IndexFormatError("index frequency overflow");
  return left + right;
}

}  // namespace

IndexWriter::IndexWriter(FILE* file, const IndexMetadata& metadata)
    : file_(file), metadata_(metadata) {
  if (file_ == nullptr) throw IndexFormatError("null index output file");
  std::vector<std::uint8_t> header;
  EncodeHeader(metadata_, &header);
  WriteBytes(header);
  chain_.resize(1);
  chain_size_ = 1;
}

IndexWriter::~IndexWriter() = default;

void IndexWriter::WriteBytes(const std::vector<std::uint8_t>& bytes) {
  if (!bytes.empty() &&
      std::fwrite(bytes.data(), 1, bytes.size(), file_) != bytes.size())
    throw IndexFormatError("failed to write index file");
  if (bytes.size() > std::numeric_limits<std::uint64_t>::max() - position_)
    throw IndexFormatError("index file offset overflow");
  position_ += bytes.size();
}

IndexWriter::Saved IndexWriter::WritePending(const Pending& pending) {
  Saved saved;
  saved.symbol = pending.symbol;
  saved.count = pending.terminal_count;
  if (pending.choices.empty()) {
    if (saved.count == 0) throw IndexFormatError("empty leaf in index trie");
    return saved;
  }
  if (pending.terminal_count != 0)
    throw IndexFormatError("index chain terminates without END symbol");

  std::vector<EncodedNodeChild> children;
  children.reserve(pending.choices.size());
  for (const Saved& choice : pending.choices) {
    saved.count = AddChecked(saved.count, choice.count);
    children.push_back({choice.symbol, choice.count, choice.node_offset});
  }
  const std::vector<std::uint8_t> encoded = EncodeNode(position_, children);
  saved.node_offset = position_;
  WriteBytes(encoded);
  return saved;
}

void IndexWriter::Next(const SymbolString* symbols, std::size_t same,
                       std::uint64_t count) {
  if (finished_) throw IndexFormatError("index writer is already finished");
  if (symbols == nullptr || symbols->empty() || count == 0)
    throw IndexFormatError("invalid index chain");
  if (symbols->back() != kEnd)
    throw IndexFormatError("index chain must end with END");
  if (same > symbols->size() || same + 1 > chain_size_)
    throw IndexFormatError("invalid common prefix length");

  while (same + 1 < chain_size_ && same < symbols->size() &&
         (*symbols)[same] == chain_[same + 1].symbol) {
    ++same;
  }
  while (chain_size_ - 1 > same) {
    Pending& pending = chain_[--chain_size_];
    Pending& parent = chain_[chain_size_ - 1];
    parent.choices.push_back(WritePending(pending));
    pending.choices.clear();
    pending.terminal_count = 0;
  }

  for (std::size_t i = same; i < symbols->size(); ++i) {
    if (++chain_size_ > chain_.size()) chain_.resize(chain_size_);
    Pending& pending = chain_[chain_size_ - 1];
    if (!pending.choices.empty() || pending.terminal_count != 0)
      throw IndexFormatError("dirty index writer state");
    pending.symbol = (*symbols)[i];
    if (pending.symbol == kEpsilon || pending.symbol > kPositionBreak)
      throw IndexFormatError("invalid symbol in index chain");
    if (pending.symbol < kEnd) alphabet_.insert(pending.symbol);
  }
  Pending& leaf = chain_[chain_size_ - 1];
  leaf.terminal_count = AddChecked(leaf.terminal_count, count);
}

void IndexWriter::Finish() {
  if (finished_) throw IndexFormatError("index writer is already finished");
  if (chain_size_ <= 1) throw IndexFormatError("cannot finish an empty index");
  while (chain_size_ > 1) {
    Pending& pending = chain_[--chain_size_];
    Pending& parent = chain_[chain_size_ - 1];
    parent.choices.push_back(WritePending(pending));
    pending.choices.clear();
    pending.terminal_count = 0;
  }
  const Saved root = WritePending(chain_[0]);

  const std::vector<Symbol> alphabet(alphabet_.begin(), alphabet_.end());
  const std::uint64_t alphabet_offset = position_;
  WriteBytes(EncodeAlphabet(alphabet));

  IndexFooter footer;
  footer.root_offset = root.node_offset;
  footer.root_count = root.count;
  footer.alphabet_offset = alphabet_offset;
  footer.alphabet_count = static_cast<std::uint32_t>(alphabet.size());
  footer.file_length = position_ + kIndexFooterSize;
  std::vector<std::uint8_t> encoded_footer;
  EncodeFooter(footer, &encoded_footer);
  WriteBytes(encoded_footer);
  if (std::fflush(file_) != 0) throw IndexFormatError("failed to flush index file");
  finished_ = true;
}
