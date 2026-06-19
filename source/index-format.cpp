#include "index-format.h"

#include <algorithm>
#include <cstring>
#include <limits>

namespace {

constexpr char kHeaderMagic[8] = {'N', 'U', 'T', 'I', 'D', 'X', '2', '\0'};
constexpr char kFooterMagic[8] = {'N', 'U', 'T', 'E', 'N', 'D', '2', '\0'};

void Require(bool condition, const std::string& message) {
  if (!condition) throw IndexFormatError(message);
}

std::uint64_t AddChecked(std::uint64_t left, std::uint64_t right,
                         const char* message) {
  if (right > std::numeric_limits<std::uint64_t>::max() - left)
    throw IndexFormatError(message);
  return left + right;
}

void AppendBytes(std::vector<std::uint8_t>* out, const char* bytes,
                 std::size_t size) {
  out->insert(out->end(), reinterpret_cast<const std::uint8_t*>(bytes),
              reinterpret_cast<const std::uint8_t*>(bytes) + size);
}

}  // namespace

IndexFormatError::IndexFormatError(const std::string& message)
    : std::runtime_error(message) {}

bool operator==(const IndexMetadata& left, const IndexMetadata& right) {
  return left.format_version == right.format_version &&
         left.flags == right.flags &&
         left.normalization_profile == right.normalization_profile &&
         left.max_chain_symbols == right.max_chain_symbols &&
         left.title_multiplier == right.title_multiplier &&
         left.unicode_version == right.unicode_version;
}

bool operator!=(const IndexMetadata& left, const IndexMetadata& right) {
  return !(left == right);
}

bool operator==(const IndexFooter& left, const IndexFooter& right) {
  return left.root_offset == right.root_offset &&
         left.root_count == right.root_count &&
         left.alphabet_offset == right.alphabet_offset &&
         left.alphabet_count == right.alphabet_count &&
         left.file_length == right.file_length;
}

ByteCursor::ByteCursor(const std::uint8_t* begin, const std::uint8_t* end)
    : current_(begin), end_(end) {
  Require(begin <= end, "invalid byte range");
}

std::uint8_t ByteCursor::ReadByte() {
  Require(current_ != end_, "truncated index data");
  return *current_++;
}

std::uint16_t ByteCursor::ReadU16() {
  std::uint16_t result = 0;
  for (unsigned int shift = 0; shift < 16; shift += 8)
    result |= static_cast<std::uint16_t>(ReadByte()) << shift;
  return result;
}

std::uint32_t ByteCursor::ReadU32() {
  std::uint32_t result = 0;
  for (unsigned int shift = 0; shift < 32; shift += 8)
    result |= static_cast<std::uint32_t>(ReadByte()) << shift;
  return result;
}

std::uint64_t ByteCursor::ReadU64() {
  std::uint64_t result = 0;
  for (unsigned int shift = 0; shift < 64; shift += 8)
    result |= static_cast<std::uint64_t>(ReadByte()) << shift;
  return result;
}

std::uint64_t ByteCursor::ReadUleb128() {
  std::uint64_t result = 0;
  for (unsigned int index = 0; index < 10; ++index) {
    const std::uint8_t byte = ReadByte();
    if (index == 9 && (byte & 0xFE) != 0)
      throw IndexFormatError("ULEB128 overflow");
    result |= static_cast<std::uint64_t>(byte & 0x7F) << (index * 7);
    if ((byte & 0x80) == 0) return result;
  }
  throw IndexFormatError("ULEB128 is longer than 10 bytes");
}

std::size_t ByteCursor::remaining() const {
  return static_cast<std::size_t>(end_ - current_);
}

void WriteU16(std::vector<std::uint8_t>* out, std::uint16_t value) {
  for (unsigned int shift = 0; shift < 16; shift += 8)
    out->push_back(static_cast<std::uint8_t>(value >> shift));
}

void WriteU32(std::vector<std::uint8_t>* out, std::uint32_t value) {
  for (unsigned int shift = 0; shift < 32; shift += 8)
    out->push_back(static_cast<std::uint8_t>(value >> shift));
}

void WriteU64(std::vector<std::uint8_t>* out, std::uint64_t value) {
  for (unsigned int shift = 0; shift < 64; shift += 8)
    out->push_back(static_cast<std::uint8_t>(value >> shift));
}

void WriteUleb128(std::vector<std::uint8_t>* out, std::uint64_t value) {
  do {
    std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7F);
    value >>= 7;
    if (value != 0) byte |= 0x80;
    out->push_back(byte);
  } while (value != 0);
}

std::array<char, 16> UnicodeVersionArray() {
  std::array<char, 16> result{};
  const std::string version = UnicodeVersion();
  Require(version.size() < result.size(), "ICU Unicode version is too long");
  std::copy(version.begin(), version.end(), result.begin());
  return result;
}

void EncodeHeader(const IndexMetadata& metadata, std::vector<std::uint8_t>* out) {
  Require(metadata.format_version == 2, "unsupported index version");
  Require(metadata.flags == 0, "unsupported index flags");
  Require(metadata.normalization_profile == 1,
          "unsupported normalization profile");
  Require(metadata.max_chain_symbols != 0, "zero maximum chain length");
  const std::size_t start = out->size();
  AppendBytes(out, kHeaderMagic, sizeof(kHeaderMagic));
  WriteU16(out, metadata.format_version);
  WriteU16(out, kIndexHeaderSize);
  WriteU32(out, metadata.flags);
  WriteU32(out, metadata.normalization_profile);
  WriteU32(out, metadata.max_chain_symbols);
  WriteU32(out, metadata.title_multiplier);
  WriteU32(out, 0);
  AppendBytes(out, metadata.unicode_version.data(), metadata.unicode_version.size());
  out->insert(out->end(), 16, 0);
  Require(out->size() - start == kIndexHeaderSize, "internal header size error");
}

IndexMetadata DecodeHeader(const std::uint8_t* data, std::size_t size) {
  Require(size >= kIndexHeaderSize, "truncated index header");
  Require(std::memcmp(data, kHeaderMagic, sizeof(kHeaderMagic)) == 0,
          "unsupported index format; rebuild this index with make-index v2");
  ByteCursor cursor(data + 8, data + kIndexHeaderSize);
  IndexMetadata result;
  result.format_version = cursor.ReadU16();
  Require(result.format_version == 2, "unsupported index format version");
  Require(cursor.ReadU16() == kIndexHeaderSize, "invalid index header length");
  result.flags = cursor.ReadU32();
  Require(result.flags == 0, "unsupported index flags");
  result.normalization_profile = cursor.ReadU32();
  Require(result.normalization_profile == 1,
          "unsupported normalization profile");
  result.max_chain_symbols = cursor.ReadU32();
  Require(result.max_chain_symbols != 0, "invalid zero maximum chain length");
  result.title_multiplier = cursor.ReadU32();
  Require(cursor.ReadU32() == 0, "nonzero reserved header field");
  for (char& ch : result.unicode_version) ch = static_cast<char>(cursor.ReadByte());
  Require(result.unicode_version.back() == '\0',
          "Unicode version is not NUL terminated");
  while (cursor.remaining() != 0)
    Require(cursor.ReadByte() == 0, "nonzero reserved header byte");
  return result;
}

void EncodeFooter(const IndexFooter& footer, std::vector<std::uint8_t>* out) {
  const std::size_t start = out->size();
  AppendBytes(out, kFooterMagic, sizeof(kFooterMagic));
  WriteU64(out, footer.root_offset);
  WriteU64(out, footer.root_count);
  WriteU64(out, footer.alphabet_offset);
  WriteU32(out, footer.alphabet_count);
  WriteU32(out, 0);
  WriteU64(out, footer.file_length);
  Require(out->size() - start == kIndexFooterSize, "internal footer size error");
}

IndexFooter DecodeFooter(const std::uint8_t* data, std::size_t size) {
  Require(size >= kIndexFooterSize, "truncated index footer");
  Require(std::memcmp(data, kFooterMagic, sizeof(kFooterMagic)) == 0,
          "invalid index footer magic");
  ByteCursor cursor(data + 8, data + kIndexFooterSize);
  IndexFooter result;
  result.root_offset = cursor.ReadU64();
  result.root_count = cursor.ReadU64();
  result.alphabet_offset = cursor.ReadU64();
  result.alphabet_count = cursor.ReadU32();
  Require(cursor.ReadU32() == 0, "nonzero reserved footer field");
  result.file_length = cursor.ReadU64();
  return result;
}

std::vector<std::uint8_t> EncodeNode(
    std::uint64_t current_offset,
    const std::vector<EncodedNodeChild>& children) {
  Require(!children.empty(), "index node has no children");
  std::uint64_t aggregate = 0;
  Symbol previous = 0;
  for (const EncodedNodeChild& child : children) {
    Require(child.symbol != kEpsilon, "epsilon cannot appear in an index node");
    Require(child.symbol > previous, "node labels are not strictly increasing");
    Require(child.count != 0, "zero subtree frequency");
    Require(child.symbol != kEnd || child.child_offset == 0,
            "END must be a leaf edge");
    Require(child.child_offset == 0 ||
                (child.child_offset >= kIndexHeaderSize &&
                 child.child_offset < current_offset),
            "invalid child node reference");
    aggregate = AddChecked(aggregate, child.count, "node frequency overflow");
    previous = child.symbol;
  }

  std::vector<std::uint8_t> out;
  WriteUleb128(&out, aggregate);
  WriteUleb128(&out, children.size());
  previous = 0;
  for (const EncodedNodeChild& child : children) {
    WriteUleb128(&out, child.symbol - previous);
    WriteUleb128(&out, child.count);
    const std::uint64_t reference = child.child_offset == 0
                                        ? 0
                                        : current_offset - child.child_offset + 1;
    WriteUleb128(&out, reference);
    previous = child.symbol;
  }
  return out;
}

DecodedNode DecodeNode(const std::uint8_t* file_data, std::size_t file_size,
                       std::uint64_t node_offset) {
  Require(node_offset >= kIndexHeaderSize && node_offset < file_size,
          "node offset outside index data");
  ByteCursor cursor(file_data + node_offset, file_data + file_size);
  DecodedNode result;
  result.aggregate_count = cursor.ReadUleb128();
  const std::uint64_t child_count = cursor.ReadUleb128();
  Require(child_count != 0, "index node has no children");
  Require(child_count <= cursor.remaining() / 3,
          "impossible index node child count");
  Symbol previous = 0;
  std::uint64_t sum = 0;
  result.children.reserve(static_cast<std::size_t>(child_count));
  for (std::uint64_t i = 0; i < child_count; ++i) {
    const std::uint64_t delta = cursor.ReadUleb128();
    Require(delta != 0 && delta <= std::numeric_limits<Symbol>::max() - previous,
            "invalid node label delta");
    EncodedNodeChild child;
    child.symbol = previous + static_cast<Symbol>(delta);
    Require(child.symbol <= kPositionBreak, "index label outside symbol range");
    child.count = cursor.ReadUleb128();
    Require(child.count != 0, "zero subtree frequency");
    const std::uint64_t reference = cursor.ReadUleb128();
    if (reference != 0) {
      Require(reference <= node_offset + 1, "child reference underflow");
      child.child_offset = node_offset - (reference - 1);
      Require(child.child_offset >= kIndexHeaderSize &&
                  child.child_offset < node_offset,
              "child reference is not backward");
    }
    Require(child.symbol != kEnd || child.child_offset == 0,
            "END must be a leaf edge");
    sum = AddChecked(sum, child.count, "node frequency overflow");
    result.children.push_back(child);
    previous = child.symbol;
  }
  Require(sum == result.aggregate_count, "node aggregate frequency mismatch");
  result.encoded_size = static_cast<std::size_t>(cursor.current() -
                                                 (file_data + node_offset));
  return result;
}

std::vector<std::uint8_t> EncodeAlphabet(const std::vector<Symbol>& alphabet) {
  std::vector<std::uint8_t> out;
  WriteUleb128(&out, alphabet.size());
  Symbol previous = 0;
  for (Symbol symbol : alphabet) {
    Require(symbol > previous && symbol < kEnd,
            "alphabet labels are not strictly increasing visible symbols");
    WriteUleb128(&out, symbol - previous);
    previous = symbol;
  }
  return out;
}

std::vector<Symbol> DecodeAlphabet(const std::uint8_t* begin,
                                   const std::uint8_t* end,
                                   std::uint32_t expected_count) {
  ByteCursor cursor(begin, end);
  const std::uint64_t count = cursor.ReadUleb128();
  Require(count == expected_count, "alphabet count does not match footer");
  std::vector<Symbol> result;
  result.reserve(expected_count);
  Symbol previous = 0;
  for (std::uint64_t i = 0; i < count; ++i) {
    const std::uint64_t delta = cursor.ReadUleb128();
    Require(delta != 0 && delta < kEnd - previous,
            "invalid alphabet label delta");
    previous += static_cast<Symbol>(delta);
    result.push_back(previous);
  }
  Require(cursor.remaining() == 0, "trailing bytes in alphabet section");
  return result;
}
