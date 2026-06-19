#include "index.h"
#include "cli-utf8.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

bool IsCjkClass(AllowedCharacterClass value) {
  return value == AllowedCharacterClass::kHan ||
         value == AllowedCharacterClass::kHiragana ||
         value == AllowedCharacterClass::kKatakana ||
         value == AllowedCharacterClass::kHangul;
}

SymbolString NormalizeExplorePath(const char* text) {
  const SymbolString visible = NormalizeQueryLiteral(text);
  SymbolString path;
  AllowedCharacterClass previous = AllowedCharacterClass::kDisallowed;
  bool attached_latin = false;
  for (Symbol symbol : visible) {
    const char32_t cp = static_cast<char32_t>(symbol - 1);
    const AllowedCharacterClass current = ClassifyAllowed(cp, attached_latin);
    if (!path.empty() && symbol != EncodeScalar(U' ') &&
        previous != AllowedCharacterClass::kDisallowed &&
        (IsCjkClass(previous) || IsCjkClass(current)) &&
        current != AllowedCharacterClass::kLatinMark) {
      path.push_back(kPositionBreak);
    }
    path.push_back(symbol);
    if (symbol == EncodeScalar(U' ')) {
      previous = AllowedCharacterClass::kDisallowed;
      attached_latin = false;
    } else {
      previous = current;
      attached_latin = current == AllowedCharacterClass::kAsciiLetter ||
                       current == AllowedCharacterClass::kLatinLetter ||
                       current == AllowedCharacterClass::kLatinMark;
    }
  }
  return path;
}

std::string FormatSymbol(Symbol symbol) {
  if (symbol == kPositionBreak) return "<PB>";
  if (symbol == kEnd) return "<END>";
  return EncodeVisibleUtf8(SymbolString{symbol});
}

void Walk(const IndexReader& reader, IndexReader::Node node,
          const SymbolString& path, std::size_t path_position, int depth,
          std::string* so_far) {
  if (depth == 0 || node == 0) return;
  std::vector<IndexReader::Choice> children;
  if (path_position < path.size()) {
    reader.Children(node, path[path_position], path[path_position], &children);
    ++path_position;
  } else {
    reader.Children(node, kEpsilon, kPositionBreak, &children);
  }
  std::sort(children.begin(), children.end(),
            [](const auto& left, const auto& right) {
              return left.count > right.count;
            });
  for (const auto& child : children) {
    const std::size_t old_size = so_far->size();
    *so_far += FormatSymbol(child.symbol);
    std::printf("%s (%llu) @%llu\n", so_far->c_str(),
                static_cast<unsigned long long>(child.count),
                static_cast<unsigned long long>(child.next));
    Walk(reader, child.next, path, path_position, depth - 1, so_far);
    so_far->resize(old_size);
  }
}

}  // namespace

int main(int argc, char** argv) {
  Utf8CommandLine command_line(argc, argv);
  argc = command_line.argc();
  argv = command_line.argv();
  ConfigureBinaryStandardStreams();
  if (argc < 3 || argc > 4) {
    std::fprintf(stderr, "usage: %s input.index \"path\" [depth]\n", argv[0]);
    return 2;
  }
  FILE* file = OpenFileUtf8(argv[1], "rb");
  if (file == nullptr) {
    std::fprintf(stderr, "error: can't open \"%s\"\n", argv[1]);
    return 1;
  }
  try {
    IndexReader reader(file);
    const SymbolString path = NormalizeExplorePath(argv[2]);
    int depth = static_cast<int>(path.size());
    if (argc == 4) {
      depth = std::atoi(argv[3]);
      if (depth <= 0) throw std::runtime_error("depth must be positive");
    }
    std::printf("Root (%llu) @%llu\n",
                static_cast<unsigned long long>(reader.count()),
                static_cast<unsigned long long>(reader.root()));
    std::string so_far;
    Walk(reader, reader.root(), path, 0, depth, &so_far);
    return 0;
  } catch (const std::exception& error) {
    std::fprintf(stderr, "error: %s\n", error.what());
    return 1;
  }
}
