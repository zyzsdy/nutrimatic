#include "rank-lines-lib.h"

#include <iostream>
#include <vector>

int main(int argc, char* argv[]) {
  std::vector<const char*> args(argv, argv + argc);
  return RunRankLines(
      argc, args.data(), std::cin, std::cout, std::cerr);
}
