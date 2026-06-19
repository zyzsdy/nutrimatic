#include "index-format.h"
#include "index.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace {

void Expect(bool condition, const std::string& message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(1);
  }
}

template <typename Fn>
void ExpectFormatError(Fn fn) {
  try {
    fn();
  } catch (const IndexFormatError&) {
    return;
  }
  Expect(false, "expected IndexFormatError");
}

void TestIntegers() {
  std::vector<std::uint8_t> bytes;
  WriteU16(&bytes, 0xABCD);
  WriteU32(&bytes, 0x89ABCDEF);
  WriteU64(&bytes, UINT64_MAX);
  WriteUleb128(&bytes, 0);
  WriteUleb128(&bytes, 127);
  WriteUleb128(&bytes, 128);
  WriteUleb128(&bytes, UINT64_MAX);
  ByteCursor cursor(bytes.data(), bytes.data() + bytes.size());
  Expect(cursor.ReadU16() == 0xABCD, "u16 little endian");
  Expect(cursor.ReadU32() == 0x89ABCDEF, "u32 little endian");
  Expect(cursor.ReadU64() == UINT64_MAX, "u64 little endian");
  Expect(cursor.ReadUleb128() == 0, "ULEB128 zero");
  Expect(cursor.ReadUleb128() == 127, "ULEB128 127");
  Expect(cursor.ReadUleb128() == 128, "ULEB128 128");
  Expect(cursor.ReadUleb128() == UINT64_MAX, "ULEB128 uint64 max");
  Expect(cursor.remaining() == 0, "integer cursor exhausted");

  const std::vector<std::uint8_t> truncated = {0x80};
  ExpectFormatError([&] {
    ByteCursor bad(truncated.data(), truncated.data() + truncated.size());
    bad.ReadUleb128();
  });
  const std::vector<std::uint8_t> overflow(11, 0x81);
  ExpectFormatError([&] {
    ByteCursor bad(overflow.data(), overflow.data() + overflow.size());
    bad.ReadUleb128();
  });
}

void TestHeaderFooter() {
  IndexMetadata metadata;
  metadata.unicode_version = UnicodeVersionArray();
  std::vector<std::uint8_t> header;
  EncodeHeader(metadata, &header);
  Expect(header.size() == kIndexHeaderSize, "64-byte header");
  Expect(DecodeHeader(header.data(), header.size()) == metadata,
         "header round trip");

  IndexFooter footer;
  footer.root_offset = 100;
  footer.root_count = UINT64_C(1) << 40;
  footer.alphabet_offset = 120;
  footer.alphabet_count = 300;
  footer.file_length = 200;
  std::vector<std::uint8_t> encoded;
  EncodeFooter(footer, &encoded);
  Expect(encoded.size() == kIndexFooterSize, "48-byte footer");
  Expect(DecodeFooter(encoded.data(), encoded.size()) == footer,
         "footer round trip");

  header[8] = 3;
  ExpectFormatError([&] { DecodeHeader(header.data(), header.size()); });
}

void TestIndexRoundTrip() {
  const char* filename = "test-index-format.index";
  FILE* fp = std::fopen(filename, "wb");
  Expect(fp != nullptr, "open output index");
  IndexMetadata metadata;
  metadata.unicode_version = UnicodeVersionArray();
  IndexWriter writer(fp, metadata);
  std::vector<SymbolString> chains = {
      {EncodeScalar(U'a'), kEnd},
      {EncodeScalar(U'\u4E2D'), kPositionBreak, EncodeScalar(U'\u56FD'), kEnd},
      {EncodeScalar(U'\U00020000'), kEnd},
  };
  std::sort(chains.begin(), chains.end());
  SymbolString previous;
  for (const SymbolString& chain : chains) {
    std::size_t same = 0;
    while (same < previous.size() && same < chain.size() &&
           previous[same] == chain[same]) ++same;
    writer.Next(&chain, same, 1);
    previous = chain;
  }
  writer.Finish();
  std::fclose(fp);

  fp = std::fopen(filename, "rb");
  Expect(fp != nullptr, "open input index");
  IndexReader reader(fp);
  Expect(reader.count() == chains.size(), "root frequency");
  Expect(reader.alphabet() ==
             std::vector<Symbol>({EncodeScalar(U'a'), EncodeScalar(U'\u4E2D'),
                                  EncodeScalar(U'\u56FD'), EncodeScalar(U'\U00020000')}),
         "visible alphabet");
  IndexWalker walker(&reader, reader.root(), 0);
  std::size_t index = 0;
  while (walker.chain != nullptr) {
    Expect(index < chains.size(), "walker result count");
    Expect(*walker.chain == chains[index], "walker chain");
    Expect(walker.count == 1, "walker frequency");
    ++index;
    walker.Next();
  }
  Expect(index == chains.size(), "all chains walked");
  std::fclose(fp);
  std::remove(filename);
}

void TestMergeTerminatesFrequencyBoundaryPrefixes() {
  const char* input_filename = "test-merge-prefix-input.index";
  const char* output_filename = "test-merge-prefix-output.index";
  std::remove(output_filename);

  FILE* fp = std::fopen(input_filename, "wb");
  Expect(fp != nullptr, "open merge prefix input");
  IndexMetadata metadata;
  metadata.unicode_version = UnicodeVersionArray();
  IndexWriter writer(fp, metadata);
  const SymbolString first = {
      EncodeScalar(U'a'), EncodeScalar(U' '), EncodeScalar(U'b'), kEnd};
  const SymbolString second = {
      EncodeScalar(U'a'), EncodeScalar(U' '), EncodeScalar(U'c'), kEnd};
  writer.Next(&first, 0, 1);
  writer.Next(&second, 2, 1);
  writer.Finish();
  std::fclose(fp);

  const int result = std::system(
      "build\\merge-indexes.exe 2 test-merge-prefix-input.index "
      "test-merge-prefix-output.index >NUL 2>&1");
  Expect(result == 0, "merge frequency boundary prefix");

  fp = std::fopen(output_filename, "rb");
  Expect(fp != nullptr, "open merged prefix output");
  IndexReader reader(fp);
  IndexWalker walker(&reader, reader.root(), reader.count());
  const SymbolString expected = {
      EncodeScalar(U'a'), EncodeScalar(U' '), kEnd};
  Expect(walker.chain != nullptr && *walker.chain == expected,
         "merged prefix has explicit END");
  Expect(walker.count == 2, "merged prefix frequency");
  walker.Next();
  Expect(walker.chain == nullptr, "only merged prefix remains");
  std::fclose(fp);

  std::remove(input_filename);
  std::remove(output_filename);
}

}  // namespace

int main() {
  TestIntegers();
  TestHeaderFooter();
  TestIndexRoundTrip();
  TestMergeTerminatesFrequencyBoundaryPrefixes();
  return 0;
}
