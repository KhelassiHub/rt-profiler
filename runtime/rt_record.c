#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static struct {
  int id; uint64_t start, total, hits; const char* file; int line;
} the_bb;

__attribute__((no_instrument_function))
static void __rt_report_bb_stats(void) {
  if (!the_bb.hits) return;
  dprintf(2, "BB %d %s:%d hits=%llu total_cycles=%llu avg=%llu\n",
          the_bb.id, the_bb.file ? the_bb.file : "?", the_bb.line,
          (unsigned long long)the_bb.hits,
          (unsigned long long)the_bb.total,
          (unsigned long long)(the_bb.total / the_bb.hits));
}

__attribute__((constructor, no_instrument_function))
static void __rt_init(void) { atexit(__rt_report_bb_stats); }

__attribute__((no_instrument_function))
void __rt_bb_start(int id, const char* file, int line) {
  the_bb.id = id; the_bb.file = file; the_bb.line = line;
  // asm volatile("isb"                 ::: "memory");
  asm volatile("mrs %0, pmccntr_el0" : "=r"(the_bb.start));
  // asm volatile("isb"                 ::: "memory");
}

__attribute__((no_instrument_function))
void __rt_bb_end(int id) {
  uint64_t end;
  asm volatile("mrs %0, pmccntr_el0" : "=r"(end));
  // asm volatile("isb"                 ::: "memory");
  if (the_bb.id != id) return;
  the_bb.total += end - the_bb.start;
  the_bb.hits++;
}
