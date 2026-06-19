#ifndef NUTRIMATIC_UNICODE_H_
#define NUTRIMATIC_UNICODE_H_

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using Symbol = std::uint32_t;
using SymbolString = std::vector<Symbol>;

constexpr Symbol kEpsilon = 0;
constexpr Symbol EncodeScalar(char32_t cp) {
  return static_cast<Symbol>(cp) + 1;
}
constexpr Symbol kEnd = 0x110001;
constexpr Symbol kPositionBreak = 0x110002;

enum class AllowedCharacterClass {
  kDisallowed,
  kSpace,
  kAsciiLetter,
  kAsciiDigit,
  kLatinLetter,
  kLatinMark,
  kHan,
  kHiragana,
  kKatakana,
  kHangul,
};

struct Utf8Error : std::runtime_error {
  Utf8Error(std::size_t offset, const std::string& message);
  std::size_t byte_offset;
};

struct UnicodeInputError : std::runtime_error {
  explicit UnicodeInputError(const std::string& message);
};

struct NormalizeResult {
  SymbolString symbols;
  std::uint64_t invalid_utf8_sequences = 0;
};

SymbolString DecodeUtf8Strict(std::string_view bytes);
NormalizeResult NormalizeCorpusText(std::string_view bytes);
SymbolString NormalizeQueryLiteral(std::string_view bytes);
std::string EncodeVisibleUtf8(const SymbolString& symbols);
AllowedCharacterClass ClassifyAllowed(char32_t cp, bool attached_to_latin);
std::string UnicodeVersion();

#endif  // NUTRIMATIC_UNICODE_H_
