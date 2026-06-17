#include "index.h"

#include <assert.h>
#include <limits.h>
#ifdef _WIN32
#include <io.h>
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include <algorithm>

using namespace std;

IndexReader::IndexReader(FILE* fp) {
  // open the file
  if (fseeko(fp, 0, SEEK_END) != 0) {
    fprintf(stderr, "error: can't seek data file\n");
    exit(1);
  }
  length = ftello(fp);
  if (length < 0) {
    fprintf(stderr, "error: can't tell data file length\n");
    exit(1);
  }

#ifdef _WIN32
  HANDLE file = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(fp)));
  if (file == INVALID_HANDLE_VALUE) {
    fprintf(stderr, "error: can't get file handle for data file\n");
    exit(1);
  }
  HANDLE mapping = CreateFileMapping(file, NULL, PAGE_READONLY, 0, 0, NULL);
  if (mapping == NULL) {
    fprintf(stderr, "error: can't create mapping for data file (length %lld)\n",
            static_cast<long long>(length));
    exit(1);
  }
  void* map = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
  mapping_handle = mapping;
  data = static_cast<const unsigned char*>(map);
  if (map == NULL) {
    CloseHandle(mapping);
    fprintf(stderr, "error: can't mmap data file (length %lld)\n",
            static_cast<long long>(length));
    exit(1);
  }
#else
  void* map = mmap(NULL, length, PROT_READ, MAP_SHARED, fileno(fp), 0);
  data = (const unsigned char*) map;
  if (map == MAP_FAILED) {
    fprintf(stderr, "error: can't mmap data file (length %lld)\n",
            static_cast<long long>(length));
    exit(1);
  }
#endif

  // scan the top level nodes to compute the total
  std::vector<Choice> top;
  children(root(), 0, CHAR_MIN, CHAR_MAX, &top);
  while (top.size() == 1 && top[0].count == 0) {
    off_t node = top[0].next;
    top.clear();
    children(node, 0, CHAR_MIN, CHAR_MAX, &top);
  }

  total = 0;
  for (size_t i = 0; i < top.size(); ++i) total += top[i].count;
}

IndexReader::~IndexReader() {
#ifdef _WIN32
  UnmapViewOfFile(data);
  CloseHandle(reinterpret_cast<HANDLE>(mapping_handle));
#else
  munmap((void*) data, length);
#endif
}

int IndexReader::children(off_t n, int64_t count,
                          char min, char max,
                          std::vector<Choice>* out) const {
  if (n == (off_t) -1) return count;

  Choice choice;
  assert(n >= 1 && n <= length);
  int num = data[--n];
  assert(num >= 0 && num < 0x100);

  if (num >= 0x20 && num < 0x80) {
    if (n < 1) fail(n, "need immediate next");
    if (num >= min && num <= max) {
      choice.ch = num;
      choice.count = count;
      choice.next = n;
      out->push_back(choice);
    }
    return 0;
  }

  int count_size = (num < 0xC0) ? 1 : (num < 0xE0) ? 2 : 8;
  int offset_size = (num < 0x20) ? 0 : (num < 0xA0) ? 1 : (num < 0xE0) ? 2 : 8;

  num = num & 0x1F;
  if (num == 0) {
    if (n < 1) fail(n, "need count");
    num = data[--n];
  }

  ssize_t size = count_size + offset_size + 1;
  if (num == 0 || n < num * size) fail(n, "bad size");

  off_t start = n - num * size;
  for (off_t p = start; p < n; p += size) {
    choice.ch = data[p];
    if (choice.ch < min || choice.ch > max) continue;  // TODO: binary search

    if (count_size == 1) {
      choice.count = data[p + 1];
    } else if (count_size == 2) {
      choice.count = data[p + 1] | (data[p + 2] << 8);
    } else {
      choice.count = 0;
      for (int j = 0; j < count_size; ++j)
        choice.count |= data[p + 1 + j] << (j * 8);
    }

    if (choice.count <= 0) fail(p + 1, "bad count");
    
    if (offset_size == 0) {
      choice.next = (off_t) -1;
    } else if (offset_size == 1) {
      off_t offset = data[p + 1 + count_size];
      choice.next = (offset == 255) ? (off_t) -1 : start - offset;
    } else if (offset_size == 2) {
      off_t offset = data[p + count_size + 1] | (data[p + count_size + 2] << 8);
      choice.next = (offset == 65535) ? (off_t) -1 : start - offset;
    } else {
      off_t offset = 0;
      for (int j = 0; j < offset_size; ++j)
        offset |= (uint64_t)(data[p + 1 + count_size + j]) << (j * 8);
      assert(offset_size == sizeof(off_t));
      if (offset == (off_t) -1)
        choice.next = (off_t) -1;
      else
        choice.next = start - offset;
    }

    if (choice.next != (off_t) -1 && (choice.next < 0 || choice.next > start))
      fail(p + 1 + count_size, "bad offset");
    out->push_back(choice);
    count -= choice.count;
  }

  return count;
}

void IndexReader::fail(off_t n, const char* message) const {
  fprintf(stderr, "error: pos %lld = 0x%02x: %s\n", static_cast<long long>(n),
      data[n], message);
  exit(1);
}
