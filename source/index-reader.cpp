#include "index.h"

#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <limits>

namespace {

std::uint64_t FileSize(FILE* file) {
#ifdef _WIN32
  const intptr_t descriptor = _get_osfhandle(_fileno(file));
  if (descriptor == -1) throw IndexFormatError("cannot get index file handle");
  LARGE_INTEGER size;
  if (!GetFileSizeEx(reinterpret_cast<HANDLE>(descriptor), &size) ||
      size.QuadPart < 0)
    throw IndexFormatError("cannot determine index file length");
  return static_cast<std::uint64_t>(size.QuadPart);
#else
  struct stat status;
  if (fstat(fileno(file), &status) != 0 || status.st_size < 0)
    throw IndexFormatError("cannot determine index file length");
  return static_cast<std::uint64_t>(status.st_size);
#endif
}

}  // namespace

IndexReader::IndexReader(FILE* file) {
  if (file == nullptr) throw IndexFormatError("null index input file");
  const std::uint64_t size = FileSize(file);
  if (size < kIndexHeaderSize + kIndexFooterSize)
    throw IndexFormatError("index file is too short");
  if (size > std::numeric_limits<std::size_t>::max())
    throw IndexFormatError("index file is too large for this process");
  data_size_ = static_cast<std::size_t>(size);

#ifdef _WIN32
  const intptr_t descriptor = _get_osfhandle(_fileno(file));
  HANDLE mapping = CreateFileMappingW(reinterpret_cast<HANDLE>(descriptor), nullptr,
                                      PAGE_READONLY, 0, 0, nullptr);
  if (mapping == nullptr) throw IndexFormatError("cannot map index file");
  mapping_handle_ = mapping;
  data_ = static_cast<const std::uint8_t*>(
      MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0));
  if (data_ == nullptr) {
    CloseHandle(mapping);
    mapping_handle_ = nullptr;
    throw IndexFormatError("cannot map index file view");
  }
#else
  void* mapping = mmap(nullptr, data_size_, PROT_READ, MAP_SHARED, fileno(file), 0);
  if (mapping == MAP_FAILED) throw IndexFormatError("cannot map index file");
  data_ = static_cast<const std::uint8_t*>(mapping);
#endif

  try {
    metadata_ = DecodeHeader(data_, data_size_);
    const std::size_t footer_offset = data_size_ - kIndexFooterSize;
    footer_ = DecodeFooter(data_ + footer_offset, kIndexFooterSize);
    if (footer_.file_length != data_size_)
      throw IndexFormatError("index footer file length mismatch");
    if (footer_.root_offset < kIndexHeaderSize ||
        footer_.root_offset >= footer_.alphabet_offset ||
        footer_.alphabet_offset >= footer_offset)
      throw IndexFormatError("invalid root or alphabet offset");

    const DecodedNode root =
        DecodeNode(data_, footer_.alphabet_offset, footer_.root_offset);
    if (root.encoded_size != footer_.alphabet_offset - footer_.root_offset)
      throw IndexFormatError("root node does not end at alphabet offset");
    if (root.aggregate_count != footer_.root_count)
      throw IndexFormatError("root frequency does not match footer");

    alphabet_ = DecodeAlphabet(data_ + footer_.alphabet_offset,
                               data_ + footer_offset,
                               footer_.alphabet_count);
  } catch (...) {
    Unmap();
    throw;
  }
}

IndexReader::~IndexReader() { Unmap(); }

void IndexReader::Unmap() noexcept {
  if (data_ == nullptr) return;
#ifdef _WIN32
  UnmapViewOfFile(data_);
  if (mapping_handle_ != nullptr)
    CloseHandle(reinterpret_cast<HANDLE>(mapping_handle_));
  mapping_handle_ = nullptr;
#else
  munmap(const_cast<std::uint8_t*>(data_), data_size_);
#endif
  data_ = nullptr;
  data_size_ = 0;
}

std::uint64_t IndexReader::Children(Node parent, Symbol min, Symbol max,
                                    std::vector<Choice>* out) const {
  if (parent == 0) return 0;
  if (out == nullptr) throw IndexFormatError("null child output vector");
  if (parent < kIndexHeaderSize || parent >= footer_.alphabet_offset)
    throw IndexFormatError("requested offset is outside the node section");
  const DecodedNode node = DecodeNode(data_, footer_.alphabet_offset, parent);
  for (const EncodedNodeChild& child : node.children) {
    if (child.symbol < min || child.symbol > max) continue;
    out->push_back({child.symbol, child.count, child.child_offset});
  }
  return 0;
}
