#ifndef NUTRIMATIC_INDEX_FORMAT_H_
#define NUTRIMATIC_INDEX_FORMAT_H_

#include "unicode.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

constexpr std::size_t kIndexHeaderSize = 64;
constexpr std::size_t kIndexFooterSize = 48;

struct IndexFormatError : std::runtime_error {
  explicit IndexFormatError(const std::string& message);
};

struct IndexMetadata {
  std::uint16_t format_version = 2;
  std::uint32_t flags = 0;
  std::uint32_t normalization_profile = 1;
  std::uint32_t max_chain_symbols = 40;
  std::uint32_t title_multiplier = 10;
  std::array<char, 16> unicode_version{};
};

bool operator==(const IndexMetadata& left, const IndexMetadata& right);
bool operator!=(const IndexMetadata& left, const IndexMetadata& right);

struct IndexFooter {
  std::uint64_t root_offset = 0;
  std::uint64_t root_count = 0;
  std::uint64_t alphabet_offset = 0;
  std::uint32_t alphabet_count = 0;
  std::uint64_t file_length = 0;
};

bool operator==(const IndexFooter& left, const IndexFooter& right);

class ByteCursor {
 public:
  ByteCursor(const std::uint8_t* begin, const std::uint8_t* end);
  std::uint8_t ReadByte();
  std::uint16_t ReadU16();
  std::uint32_t ReadU32();
  std::uint64_t ReadU64();
  std::uint64_t ReadUleb128();
  std::size_t remaining() const;
  const std::uint8_t* current() const { return current_; }

 private:
  const std::uint8_t* current_;
  const std::uint8_t* end_;
};

void WriteU16(std::vector<std::uint8_t>* out, std::uint16_t value);
void WriteU32(std::vector<std::uint8_t>* out, std::uint32_t value);
void WriteU64(std::vector<std::uint8_t>* out, std::uint64_t value);
void WriteUleb128(std::vector<std::uint8_t>* out, std::uint64_t value);

std::array<char, 16> UnicodeVersionArray();
void EncodeHeader(const IndexMetadata& metadata, std::vector<std::uint8_t>* out);
IndexMetadata DecodeHeader(const std::uint8_t* data, std::size_t size);
void EncodeFooter(const IndexFooter& footer, std::vector<std::uint8_t>* out);
IndexFooter DecodeFooter(const std::uint8_t* data, std::size_t size);

struct EncodedNodeChild {
  Symbol symbol = 0;
  std::uint64_t count = 0;
  std::uint64_t child_offset = 0;
};

struct DecodedNode {
  std::uint64_t aggregate_count = 0;
  std::vector<EncodedNodeChild> children;
  std::size_t encoded_size = 0;
};

std::vector<std::uint8_t> EncodeNode(
    std::uint64_t current_offset,
    const std::vector<EncodedNodeChild>& children);
DecodedNode DecodeNode(const std::uint8_t* file_data, std::size_t file_size,
                       std::uint64_t node_offset);
std::vector<std::uint8_t> EncodeAlphabet(const std::vector<Symbol>& alphabet);
std::vector<Symbol> DecodeAlphabet(const std::uint8_t* begin,
                                   const std::uint8_t* end,
                                   std::uint32_t expected_count);

#endif  // NUTRIMATIC_INDEX_FORMAT_H_
