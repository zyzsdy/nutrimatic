#include "cli-utf8.h"

#include "unicode.h"

#include <stdexcept>
#include <cwchar>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

#ifdef _WIN32
std::string WideToUtf8(const wchar_t* text, int length) {
  for (int i = 0; i < length; ++i) {
    const wchar_t unit = text[i];
    if (unit >= 0xD800 && unit <= 0xDBFF) {
      if (i + 1 >= length || text[i + 1] < 0xDC00 || text[i + 1] > 0xDFFF)
        throw Utf8Error(i, "unpaired UTF-16 high surrogate");
      ++i;
    } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
      throw Utf8Error(i, "unpaired UTF-16 low surrogate");
    }
  }
  const int needed = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text,
                                         length, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) throw std::runtime_error("cannot convert UTF-16 argument");
  std::string result(static_cast<std::size_t>(needed), '\0');
  if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text, length,
                          result.data(), needed, nullptr, nullptr) != needed)
    throw std::runtime_error("cannot convert UTF-16 argument");
  return result;
}

std::wstring Utf8ToWide(const std::string& text) {
  DecodeUtf8Strict(text);
  const int needed = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         text.data(), text.size(), nullptr, 0);
  if (needed <= 0) throw std::runtime_error("cannot convert UTF-8 path");
  std::wstring result(static_cast<std::size_t>(needed), L'\0');
  if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), text.size(),
                          result.data(), needed) != needed)
    throw std::runtime_error("cannot convert UTF-8 path");
  return result;
}
#endif

}  // namespace

std::vector<std::string> GetUtf8Arguments(int argc, char** argv) {
#ifdef _WIN32
  int wide_argc = 0;
  wchar_t** wide_argv = CommandLineToArgvW(GetCommandLineW(), &wide_argc);
  if (wide_argv == nullptr) throw std::runtime_error("cannot read command line");
  std::vector<std::string> result;
  try {
    result.reserve(wide_argc);
    for (int i = 0; i < wide_argc; ++i)
      result.push_back(WideToUtf8(wide_argv[i], std::wcslen(wide_argv[i])));
  } catch (...) {
    LocalFree(wide_argv);
    throw;
  }
  LocalFree(wide_argv);
  return result;
#else
  std::vector<std::string> result;
  result.reserve(argc);
  for (int i = 0; i < argc; ++i) {
    DecodeUtf8Strict(argv[i]);
    result.emplace_back(argv[i]);
  }
  return result;
#endif
}

Utf8CommandLine::Utf8CommandLine(int argc, char** argv)
    : values_(GetUtf8Arguments(argc, argv)) {
  pointers_.reserve(values_.size() + 1);
  for (std::string& value : values_) pointers_.push_back(value.data());
  pointers_.push_back(nullptr);
}

FILE* OpenFileUtf8(const std::string& path, const char* mode) {
#ifdef _WIN32
  const std::wstring wide_path = Utf8ToWide(path);
  const std::wstring wide_mode = Utf8ToWide(mode);
  return _wfopen(wide_path.c_str(), wide_mode.c_str());
#else
  return std::fopen(path.c_str(), mode);
#endif
}

void ConfigureBinaryStandardStreams() {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  _setmode(_fileno(stderr), _O_BINARY);
#endif
}
