#include "index.h"

#include <algorithm>
#include <limits>

IndexReader::IndexReader(FILE* file) {
  if (file == nullptr) throw IndexFormatError("null index input file");
  if (std::fseek(file, 0, SEEK_END) != 0)
    throw IndexFormatError("cannot seek index file");
  const long size = std::ftell(file);
  if (size < 0) throw IndexFormatError("cannot determine index file length");
  if (std::fseek(file, 0, SEEK_SET) != 0)
    throw IndexFormatError("cannot rewind index file");
  data_.resize(static_cast<std::size_t>(size));
  if (!data_.empty() &&
      std::fread(data_.data(), 1, data_.size(), file) != data_.size())
    throw IndexFormatError("cannot read index file");
  if (data_.size() < kIndexHeaderSize + kIndexFooterSize)
    throw IndexFormatError("index file is too short");

  metadata_ = DecodeHeader(data_.data(), data_.size());
  const std::size_t footer_offset = data_.size() - kIndexFooterSize;
  footer_ = DecodeFooter(data_.data() + footer_offset, kIndexFooterSize);
  if (footer_.file_length != data_.size())
    throw IndexFormatError("index footer file length mismatch");
  if (footer_.root_offset < kIndexHeaderSize ||
      footer_.root_offset >= footer_.alphabet_offset ||
      footer_.alphabet_offset >= footer_offset)
    throw IndexFormatError("invalid root or alphabet offset");

  std::uint64_t offset = kIndexHeaderSize;
  DecodedNode root;
  while (offset < footer_.alphabet_offset) {
    node_offsets_.insert(offset);
    const DecodedNode node = DecodeNode(data_.data(), footer_.alphabet_offset, offset);
    for (const EncodedNodeChild& child : node.children) {
      if (child.child_offset != 0 && node_offsets_.count(child.child_offset) == 0)
        throw IndexFormatError("child reference does not point to a node boundary");
    }
    if (offset == footer_.root_offset) root = node;
    if (node.encoded_size > std::numeric_limits<std::uint64_t>::max() - offset)
      throw IndexFormatError("node offset overflow");
    offset += node.encoded_size;
  }
  if (offset != footer_.alphabet_offset ||
      node_offsets_.count(footer_.root_offset) == 0)
    throw IndexFormatError("node section does not end at alphabet offset");
  if (root.aggregate_count != footer_.root_count)
    throw IndexFormatError("root frequency does not match footer");

  alphabet_ = DecodeAlphabet(data_.data() + footer_.alphabet_offset,
                             data_.data() + footer_offset,
                             footer_.alphabet_count);
}

std::uint64_t IndexReader::Children(Node parent, Symbol min, Symbol max,
                                    std::vector<Choice>* out) const {
  if (parent == 0) return 0;
  if (out == nullptr) throw IndexFormatError("null child output vector");
  if (node_offsets_.count(parent) == 0)
    throw IndexFormatError("requested offset is not an index node");
  const DecodedNode node = DecodeNode(data_.data(), footer_.alphabet_offset, parent);
  for (const EncodedNodeChild& child : node.children) {
    if (child.symbol < min || child.symbol > max) continue;
    out->push_back({child.symbol, child.count, child.child_offset});
  }
  return 0;
}
