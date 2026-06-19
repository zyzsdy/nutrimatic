#include "index-builder.h"
#include "index.h"
#include "unicode.h"
#include "cli-utf8.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::size_t kChainsPerFile = 1000000;

void AddLine(const std::string& line, std::size_t multiplier,
             std::vector<SymbolString>* chains,
             std::uint64_t* invalid_utf8_sequences) {
  const NormalizeResult normalized = NormalizeCorpusText(line);
  *invalid_utf8_sequences += normalized.invalid_utf8_sequences;
  const std::vector<SymbolString> line_chains = GenerateIndexChains(normalized.symbols);
  for (std::size_t copy = 0; copy < multiplier; ++copy)
    chains->insert(chains->end(), line_chains.begin(), line_chains.end());
}

void WriteIndex(const std::string& prefix, int number,
                std::vector<SymbolString>* chains) {
  char suffix[32];
  std::snprintf(suffix, sizeof(suffix), ".%05d.index", number);
  const std::string filename = prefix + suffix;
  FILE* file = OpenFileUtf8(filename, "wb");
  if (file == nullptr)
    throw std::runtime_error("cannot open output index \"" + filename + "\"");

  IndexMetadata metadata;
  metadata.unicode_version = UnicodeVersionArray();
  IndexWriter writer(file, metadata);
  std::sort(chains->begin(), chains->end());
  SymbolString previous;
  for (const SymbolString& chain : *chains) {
    std::size_t same = 0;
    while (same < previous.size() && same < chain.size() &&
           previous[same] == chain[same]) ++same;
    writer.Next(&chain, same, 1);
    previous = chain;
  }
  writer.Finish();
  std::fclose(file);
  chains->clear();
}

}  // namespace

int main(int argc, char** argv) {
  Utf8CommandLine command_line(argc, argv);
  argc = command_line.argc();
  argv = command_line.argv();
  ConfigureBinaryStandardStreams();
  if (argc != 2 || argv[1][0] == '-') {
    std::fprintf(stderr, "usage: %s outfileprefix < textfile.txt\n", argv[0]);
    return 2;
  }

  try {
    int file_count = 0;
    std::vector<SymbolString> chains;
    std::string line;
    bool next_line_is_title = false;
    std::uint64_t invalid_utf8_sequences = 0;
    while (std::getline(std::cin, line)) {
      if (line.rfind("BEGIN ARTICLE:", 0) == 0) {
        AddLine(line.substr(14), 10, &chains, &invalid_utf8_sequences);
      } else if (line.rfind("<doc ", 0) == 0) {
        next_line_is_title = true;
      } else if (next_line_is_title) {
        AddLine(line, 10, &chains, &invalid_utf8_sequences);
        next_line_is_title = false;
      } else if (line.rfind("END ARTICLE:", 0) != 0 &&
                 line.rfind("</doc>", 0) != 0) {
        AddLine(line, 1, &chains, &invalid_utf8_sequences);
      }

      if (chains.size() >= kChainsPerFile)
        WriteIndex(argv[1], file_count++, &chains);
    }
    if (!chains.empty()) WriteIndex(argv[1], file_count++, &chains);
    if (invalid_utf8_sequences != 0) {
      std::fprintf(stderr,
                   "warning: replaced %llu invalid UTF-8 sequence(s) with boundaries\n",
                   static_cast<unsigned long long>(invalid_utf8_sequences));
    }
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
