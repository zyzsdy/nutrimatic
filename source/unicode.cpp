#include "unicode.h"

#include <unicode/uchar.h>
#include <unicode/unorm2.h>
#include <unicode/uscript.h>
#include <unicode/uversion.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <limits>
#include <sstream>

namespace {

constexpr char32_t kInvalidBoundary = 0xFFFFFFFF;

bool IsScalar(char32_t cp) {
  return cp <= 0x10FFFF && !(cp >= 0xD800 && cp <= 0xDFFF);
}

bool IsContinuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

struct DecodeResult {
  std::vector<char32_t> codepoints;
  std::uint64_t invalid_sequences = 0;
};

DecodeResult Decode(std::string_view bytes, bool strict) {
  DecodeResult result;
  for (std::size_t i = 0; i < bytes.size();) {
    const std::size_t start = i;
    const unsigned char first = static_cast<unsigned char>(bytes[i]);
    char32_t cp = 0;
    std::size_t length = 0;
    if (first <= 0x7F) {
      cp = first;
      length = 1;
    } else if (first >= 0xC2 && first <= 0xDF) {
      cp = first & 0x1F;
      length = 2;
    } else if (first >= 0xE0 && first <= 0xEF) {
      cp = first & 0x0F;
      length = 3;
    } else if (first >= 0xF0 && first <= 0xF4) {
      cp = first & 0x07;
      length = 4;
    }

    bool valid = length != 0 && i + length <= bytes.size();
    if (valid) {
      for (std::size_t j = 1; j < length; ++j) {
        const unsigned char next = static_cast<unsigned char>(bytes[i + j]);
        if (!IsContinuation(next)) {
          valid = false;
          break;
        }
        cp = (cp << 6) | (next & 0x3F);
      }
    }
    if (valid) {
      static constexpr std::array<char32_t, 5> minimum = {
          0, 0, 0x80, 0x800, 0x10000};
      valid = cp >= minimum[length] && IsScalar(cp);
    }

    if (valid) {
      result.codepoints.push_back(cp);
      i += length;
      continue;
    }

    if (strict) throw Utf8Error(start, "invalid UTF-8 sequence");
    ++result.invalid_sequences;
    result.codepoints.push_back(kInvalidBoundary);
    ++i;
    while (i < bytes.size() && IsContinuation(static_cast<unsigned char>(bytes[i])) &&
           length > 1 && i - start < length) {
      ++i;
    }
  }
  return result;
}

bool HasScript(char32_t cp, UScriptCode script) {
  return uscript_hasScript(static_cast<UChar32>(cp), script);
}

bool IsLetterClass(AllowedCharacterClass value) {
  return value == AllowedCharacterClass::kAsciiLetter ||
         value == AllowedCharacterClass::kLatinLetter ||
         value == AllowedCharacterClass::kHan ||
         value == AllowedCharacterClass::kHiragana ||
         value == AllowedCharacterClass::kKatakana ||
         value == AllowedCharacterClass::kHangul;
}

bool IsCjk(AllowedCharacterClass value) {
  return value == AllowedCharacterClass::kHan ||
         value == AllowedCharacterClass::kHiragana ||
         value == AllowedCharacterClass::kKatakana ||
         value == AllowedCharacterClass::kHangul;
}

std::vector<char32_t> NormalizeRun(const std::vector<char32_t>& run) {
  if (run.empty()) return {};
  std::vector<UChar> input;
  for (char32_t cp : run) {
    if (cp <= 0xFFFF) {
      input.push_back(static_cast<UChar>(cp));
    } else {
      cp -= 0x10000;
      input.push_back(static_cast<UChar>(0xD800 + (cp >> 10)));
      input.push_back(static_cast<UChar>(0xDC00 + (cp & 0x3FF)));
    }
  }

  UErrorCode status = U_ZERO_ERROR;
  const UNormalizer2* normalizer = unorm2_getNFKCCasefoldInstance(&status);
  if (U_FAILURE(status)) throw std::runtime_error("cannot initialize ICU NFKC Casefold");
  int32_t needed = unorm2_normalize(normalizer, input.data(), input.size(), nullptr, 0,
                                    &status);
  if (status != U_BUFFER_OVERFLOW_ERROR && U_FAILURE(status))
    throw std::runtime_error("ICU normalization failed");
  status = U_ZERO_ERROR;
  std::vector<UChar> output(static_cast<std::size_t>(needed));
  unorm2_normalize(normalizer, input.data(), input.size(), output.data(), needed,
                   &status);
  if (U_FAILURE(status)) throw std::runtime_error("ICU normalization failed");

  std::vector<char32_t> result;
  for (std::size_t i = 0; i < output.size(); ++i) {
    char32_t cp = output[i];
    if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < output.size()) {
      cp = 0x10000 + ((cp - 0xD800) << 10) + (output[++i] - 0xDC00);
    }
    result.push_back(cp);
  }
  return result;
}

std::string CodePointName(char32_t cp) {
  char buffer[16];
  std::snprintf(buffer, sizeof(buffer), cp <= 0xFFFF ? "U+%04X" : "U+%06X",
                static_cast<unsigned int>(cp));
  return buffer;
}

NormalizeResult Normalize(std::string_view bytes, bool strict_query) {
  const DecodeResult decoded = Decode(bytes, strict_query);
  std::vector<char32_t> filtered;
  std::vector<char32_t> run;
  bool previous_latin = false;
  AllowedCharacterClass previous_class = AllowedCharacterClass::kDisallowed;

  const auto flush = [&] {
    const std::vector<char32_t> normalized = NormalizeRun(run);
    run.clear();
    bool attached_latin = false;
    for (char32_t cp : normalized) {
      const AllowedCharacterClass cls = ClassifyAllowed(cp, attached_latin);
      if (cls == AllowedCharacterClass::kDisallowed) {
        if (strict_query)
          throw UnicodeInputError("disallowed query character " + CodePointName(cp));
        if (filtered.empty() || filtered.back() != U' ') filtered.push_back(U' ');
        attached_latin = false;
      } else {
        filtered.push_back(cp);
        attached_latin = cls == AllowedCharacterClass::kAsciiLetter ||
                         cls == AllowedCharacterClass::kLatinLetter ||
                         cls == AllowedCharacterClass::kLatinMark;
      }
    }
  };

  for (std::size_t i = 0; i < decoded.codepoints.size(); ++i) {
    const char32_t cp = decoded.codepoints[i];
    if (cp == kInvalidBoundary || u_isUWhiteSpace(static_cast<UChar32>(cp))) {
      flush();
      if (filtered.empty() || filtered.back() != U' ') filtered.push_back(U' ');
      previous_latin = false;
      previous_class = AllowedCharacterClass::kDisallowed;
      continue;
    }

    const bool apostrophe = cp == U'\'' || cp == U'\u2019';
    if (apostrophe) {
      bool next_is_letter = false;
      if (i + 1 < decoded.codepoints.size()) {
        next_is_letter = IsLetterClass(
            ClassifyAllowed(decoded.codepoints[i + 1], false));
      }
      if (IsLetterClass(previous_class) && next_is_letter) continue;
    }

    AllowedCharacterClass cls = ClassifyAllowed(cp, previous_latin);
    const bool kana_mark = (cp == U'\u3099' || cp == U'\u309A') &&
                           (previous_class == AllowedCharacterClass::kHiragana ||
                            previous_class == AllowedCharacterClass::kKatakana);
    if (cls == AllowedCharacterClass::kDisallowed && !kana_mark) {
      if (strict_query)
        throw UnicodeInputError("disallowed query character " + CodePointName(cp));
      flush();
      if (filtered.empty() || filtered.back() != U' ') filtered.push_back(U' ');
      previous_latin = false;
      previous_class = AllowedCharacterClass::kDisallowed;
      continue;
    }
    run.push_back(cp);
    previous_latin = cls == AllowedCharacterClass::kAsciiLetter ||
                     cls == AllowedCharacterClass::kLatinLetter ||
                     cls == AllowedCharacterClass::kLatinMark;
    if (!kana_mark) previous_class = cls;
  }
  flush();

  if (!strict_query) {
    while (!filtered.empty() && filtered.front() == U' ')
      filtered.erase(filtered.begin());
    while (!filtered.empty() && filtered.back() == U' ') filtered.pop_back();
  }
  filtered.erase(std::unique(filtered.begin(), filtered.end(), [](char32_t a, char32_t b) {
                   return a == U' ' && b == U' ';
                 }),
                 filtered.end());

  NormalizeResult result;
  result.invalid_utf8_sequences = decoded.invalid_sequences;
  AllowedCharacterClass previous_visible = AllowedCharacterClass::kDisallowed;
  bool attached_latin = false;
  for (char32_t cp : filtered) {
    const AllowedCharacterClass cls = ClassifyAllowed(cp, attached_latin);
    if (cp != U' ' && previous_visible != AllowedCharacterClass::kDisallowed &&
        (IsCjk(previous_visible) || IsCjk(cls)) &&
        cls != AllowedCharacterClass::kLatinMark && !strict_query) {
      result.symbols.push_back(kPositionBreak);
    }
    result.symbols.push_back(EncodeScalar(cp));
    if (cp == U' ') {
      previous_visible = AllowedCharacterClass::kDisallowed;
      attached_latin = false;
    } else {
      previous_visible = cls;
      attached_latin = cls == AllowedCharacterClass::kAsciiLetter ||
                       cls == AllowedCharacterClass::kLatinLetter ||
                       cls == AllowedCharacterClass::kLatinMark;
    }
  }
  return result;
}

}  // namespace

Utf8Error::Utf8Error(std::size_t offset, const std::string& message)
    : std::runtime_error(message), byte_offset(offset) {}

UnicodeInputError::UnicodeInputError(const std::string& message)
    : std::runtime_error(message) {}

SymbolString DecodeUtf8Strict(std::string_view bytes) {
  SymbolString symbols;
  for (char32_t cp : Decode(bytes, true).codepoints) symbols.push_back(EncodeScalar(cp));
  return symbols;
}

NormalizeResult NormalizeCorpusText(std::string_view bytes) {
  return Normalize(bytes, false);
}

SymbolString NormalizeQueryLiteral(std::string_view bytes) {
  return Normalize(bytes, true).symbols;
}

std::string EncodeVisibleUtf8(const SymbolString& symbols) {
  std::string result;
  for (Symbol symbol : symbols) {
    if (symbol == kEnd || symbol == kPositionBreak || symbol == kEpsilon)
      throw UnicodeInputError("internal symbol cannot be encoded as visible UTF-8");
    const char32_t cp = static_cast<char32_t>(symbol - 1);
    if (!IsScalar(cp)) throw UnicodeInputError("invalid Unicode symbol");
    if (cp <= 0x7F) {
      result.push_back(static_cast<char>(cp));
    } else if (cp <= 0x7FF) {
      result.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp <= 0xFFFF) {
      result.push_back(static_cast<char>(0xE0 | (cp >> 12)));
      result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
      result.push_back(static_cast<char>(0xF0 | (cp >> 18)));
      result.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
      result.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
  }
  return result;
}

AllowedCharacterClass ClassifyAllowed(char32_t cp, bool attached_to_latin) {
  if (cp == U' ') return AllowedCharacterClass::kSpace;
  if (cp >= U'a' && cp <= U'z') return AllowedCharacterClass::kAsciiLetter;
  if (cp >= U'A' && cp <= U'Z') return AllowedCharacterClass::kAsciiLetter;
  if (cp >= U'0' && cp <= U'9') return AllowedCharacterClass::kAsciiDigit;
  if (!IsScalar(cp)) return AllowedCharacterClass::kDisallowed;

  const int8_t category = u_charType(static_cast<UChar32>(cp));
  const bool letter = category == U_UPPERCASE_LETTER ||
                      category == U_LOWERCASE_LETTER ||
                      category == U_TITLECASE_LETTER ||
                      category == U_MODIFIER_LETTER ||
                      category == U_OTHER_LETTER;
  const bool mark = category == U_NON_SPACING_MARK ||
                    category == U_COMBINING_SPACING_MARK ||
                    category == U_ENCLOSING_MARK;
  if (mark && attached_to_latin && HasScript(cp, USCRIPT_LATIN))
    return AllowedCharacterClass::kLatinMark;
  if (!letter) return AllowedCharacterClass::kDisallowed;
  if (HasScript(cp, USCRIPT_LATIN)) return AllowedCharacterClass::kLatinLetter;
  if (category == U_OTHER_LETTER && HasScript(cp, USCRIPT_HAN))
    return AllowedCharacterClass::kHan;
  if (HasScript(cp, USCRIPT_HIRAGANA)) return AllowedCharacterClass::kHiragana;
  if (HasScript(cp, USCRIPT_KATAKANA)) return AllowedCharacterClass::kKatakana;
  if (HasScript(cp, USCRIPT_HANGUL)) return AllowedCharacterClass::kHangul;
  return AllowedCharacterClass::kDisallowed;
}

std::string UnicodeVersion() {
  UVersionInfo version;
  u_getUnicodeVersion(version);
  char text[U_MAX_VERSION_STRING_LENGTH];
  u_versionToString(version, text);
  return text;
}
