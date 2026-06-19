#include "unicode.h"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>

namespace {

void Fail(const std::string& message) {
  std::cerr << "FAIL: " << message << '\n';
  std::exit(1);
}

void Expect(bool condition, const std::string& message) {
  if (!condition) Fail(message);
}

template <typename Fn>
void ExpectUtf8Error(std::size_t offset, Fn fn) {
  try {
    fn();
  } catch (const Utf8Error& error) {
    Expect(error.byte_offset == offset, "wrong UTF-8 error offset");
    return;
  }
  Fail("expected Utf8Error");
}

Symbol S(char32_t cp) { return EncodeScalar(cp); }

void TestUtf8() {
  const std::string valid = "ASCII \xE4\xB8\xAD \xF0\xA0\x80\x80 e\xCC\x81";
  const SymbolString decoded = DecodeUtf8Strict(valid);
  Expect(EncodeVisibleUtf8(decoded) == valid, "UTF-8 round trip");

  ExpectUtf8Error(0, [] { DecodeUtf8Strict("\x80"); });
  ExpectUtf8Error(0, [] { DecodeUtf8Strict("\xE4\xB8"); });
  ExpectUtf8Error(0, [] { DecodeUtf8Strict("\xC0\xAF"); });
  ExpectUtf8Error(0, [] { DecodeUtf8Strict("\xED\xA0\x80"); });
  ExpectUtf8Error(0, [] { DecodeUtf8Strict("\xF4\x90\x80\x80"); });

  const NormalizeResult lossy = NormalizeCorpusText("a\x80\x80" "b");
  Expect(lossy.invalid_utf8_sequences == 2, "count invalid UTF-8 sequences");
  Expect(lossy.symbols == SymbolString({S(U'a'), S(U' '), S(U'b')}),
         "invalid UTF-8 becomes one folded boundary");
}

void TestClassification() {
  Expect(ClassifyAllowed(U'a', false) == AllowedCharacterClass::kAsciiLetter,
         "ASCII letter");
  Expect(ClassifyAllowed(U'7', false) == AllowedCharacterClass::kAsciiDigit,
         "ASCII digit");
  Expect(ClassifyAllowed(U'\u00E9', false) == AllowedCharacterClass::kLatinLetter,
         "Latin letter");
  Expect(ClassifyAllowed(U'\u0301', true) == AllowedCharacterClass::kLatinMark,
         "attached Latin mark");
  Expect(ClassifyAllowed(U'\u0301', false) == AllowedCharacterClass::kDisallowed,
         "detached Latin mark");
  Expect(ClassifyAllowed(U'\u4E2D', false) == AllowedCharacterClass::kHan, "Han");
  Expect(ClassifyAllowed(U'\u304B', false) == AllowedCharacterClass::kHiragana,
         "Hiragana");
  Expect(ClassifyAllowed(U'\u30AB', false) == AllowedCharacterClass::kKatakana,
         "Katakana");
  Expect(ClassifyAllowed(U'\uD55C', false) == AllowedCharacterClass::kHangul,
         "Hangul");
  Expect(ClassifyAllowed(U'\u03B1', false) == AllowedCharacterClass::kDisallowed,
         "Greek rejected");
  Expect(ClassifyAllowed(U'\u2460', false) == AllowedCharacterClass::kDisallowed,
         "circled digit rejected before NFKC");
  Expect(ClassifyAllowed(U'\U0001D400', false) == AllowedCharacterClass::kDisallowed,
         "mathematical letter rejected before NFKC");
}

void TestNormalization() {
  const auto corpus = [](const std::string& text) {
    return NormalizeCorpusText(text).symbols;
  };
  Expect(corpus("\xEF\xBC\xA1\xEF\xBC\xA9\xE6\x97\xB6\xE4\xBB\xA3") ==
             SymbolString({S(U'a'), S(U'i'), kPositionBreak, S(U'\u65F6'),
                           kPositionBreak, S(U'\u4EE3')}),
         "fullwidth Latin and Han boundaries");
  Expect(corpus("\xE4\xB8\xAD\xE5\x9B\xBD\xE7\xA7\x91\xE5\xAD\xA6\xE9\x99\xA2") ==
             SymbolString({S(U'\u4E2D'), kPositionBreak, S(U'\u56FD'),
                           kPositionBreak, S(U'\u79D1'), kPositionBreak,
                           S(U'\u5B66'), kPositionBreak, S(U'\u9662')}),
         "Han position boundaries");
  Expect(corpus("I'm") == SymbolString({S(U'i'), S(U'm')}), "apostrophe removal");
  Expect(corpus("cafe\xCC\x81") == corpus("caf\xC3\xA9"),
         "canonical Latin normalization");
  Expect(corpus("  a---b  ") == SymbolString({S(U'a'), S(U' '), S(U'b')}),
         "boundary folding and trimming");
  Expect(corpus("\xE5\x8F\xB0") != corpus("\xE8\x87\xBA"),
         "simplified and traditional remain distinct");

  try {
    NormalizeQueryLiteral("\xCE\xB1");
  } catch (const UnicodeInputError& error) {
    Expect(std::string(error.what()).find("U+03B1") != std::string::npos,
           "query error names disallowed code point");
    return;
  }
  Fail("query must reject Greek");
}

}  // namespace

int main(int argc, char** argv) {
  const std::string section = argc == 2 ? argv[1] : "";
  if (section.empty() || section == "--section=utf8") TestUtf8();
  if (section.empty() || section == "--section=classification") TestClassification();
  if (section.empty() || section == "--section=normalization") TestNormalization();
  return 0;
}
