#include "index.h"
#include "search.h"

#include <stdio.h>
#include <string.h>

void PrintAll(SearchDriver* d, size_t max_steps) {
  for (size_t count = 0; count < max_steps; ++count) {
    const size_t step_number = count + 1;
    if (!(step_number % 100000)) {
      printf("# %zu\n", step_number);
      fflush(stdout);
    }
    if (d->step()) {
      if (d->text == NULL) break;
      int len = strlen(d->text);
      while (len > 0 && d->text[len - 1] == ' ') --len;
      printf("%.8g %.*s\n", d->score, len, d->text);
    }
  }
}
