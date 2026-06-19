#include "index.h"
#include "cli-utf8.h"

#include <cstdio>
#include <string>

namespace {

std::string FormatChain(const SymbolString& chain) {
  std::string result;
  SymbolString visible;
  const auto flush = [&] {
    result += EncodeVisibleUtf8(visible);
    visible.clear();
  };
  for (Symbol symbol : chain) {
    if (symbol == kPositionBreak) {
      flush();
      result += "<PB>";
    } else if (symbol == kEnd) {
      flush();
      result += "<END>";
    } else {
      visible.push_back(symbol);
    }
  }
  flush();
  return result;
}

}  // namespace

int main(int argc, char** argv) {
  Utf8CommandLine command_line(argc, argv);
  argc = command_line.argc();
  argv = command_line.argv();
  ConfigureBinaryStandardStreams();
  if (argc != 2) {
    std::fprintf(stderr, "usage: %s input.index\n", argv[0]);
    return 2;
  }
  FILE* file = OpenFileUtf8(argv[1], "rb");
  if (file == nullptr) {
    std::fprintf(stderr, "error: can't open \"%s\"\n", argv[1]);
    return 1;
  }
  try {
    IndexReader reader(file);
    IndexWalker walker(&reader, reader.root(), reader.count());
    while (walker.chain != nullptr) {
      const std::string formatted = FormatChain(*walker.chain);
      std::printf("%llu [%s]\n", static_cast<unsigned long long>(walker.count),
                  formatted.c_str());
      walker.Next();
    }
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
