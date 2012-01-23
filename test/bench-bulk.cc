#include "test.h"

TEST_START("bulk set benchmark", "bulk-bench")

  const int num = 500000;
  const int delta = 20000;
  int i, start;

  char* keys[num];

  /* init keys */
  for (int i = 0; i < num; i++) {
    keys[i] = (char*) malloc(20);
    sprintf(keys[i], "%*0d", 20, i);
  }

  for (start = 0; start < num; start += delta) {
    fprintf(stdout, "%d items in db\n", start);

    BENCH_START(bulk, delta)
    bp_bulk_sets(&db,
                 delta,
                 (const char**) keys + start,
                 (const char**) keys + start);
    BENCH_END(bulk, delta)
  }

  /* free keys */
  for (int i = 0; i < num; i++) {
    free(keys[i]);
  }

TEST_END("bulk set benchmark", "bulk-bench")
