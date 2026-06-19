#ifndef NUTRIMATIC_CLI_UTF8_H_
#define NUTRIMATIC_CLI_UTF8_H_

#include <cstdio>
#include <string>
#include <vector>

std::vector<std::string> GetUtf8Arguments(int argc, char** argv);

class Utf8CommandLine {
 public:
  Utf8CommandLine(int argc, char** argv);
  int argc() const { return static_cast<int>(values_.size()); }
  char** argv() { return pointers_.data(); }

 private:
  std::vector<std::string> values_;
  std::vector<char*> pointers_;
};

FILE* OpenFileUtf8(const std::string& path, const char* mode);
void ConfigureBinaryStandardStreams();

#endif  // NUTRIMATIC_CLI_UTF8_H_
