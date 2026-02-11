#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/perf_event.h>
#include <asm/unistd.h>

static int fd = -1;
static uint64_t last = 0;

static int perf_event_open(struct perf_event_attr* hw_event, pid_t pid,
                           int cpu, int group_fd, unsigned long flags) {
  return syscall(__NR_perf_event_open, hw_event, pid, cpu, group_fd, flags);
}

static void pmu_init_once() {
  if (fd != -1) return;
  struct perf_event_attr pe;
  memset(&pe, 0, sizeof(pe));
  pe.type = PERF_TYPE_HARDWARE;
  pe.size = sizeof(pe);
  pe.config = PERF_COUNT_HW_CPU_CYCLES;
  pe.disabled = 0;
  pe.exclude_kernel = 1;
  pe.exclude_hv = 1;

  fd = perf_event_open(&pe, 0, -1, -1, 0);
}

static uint64_t read_cycles() {
  uint64_t val = 0;
  if (fd == -1) pmu_init_once();
  if (fd != -1) read(fd, &val, sizeof(val));
  return val;
}

__attribute__((no_instrument_function))
void __rt_record_line(const char* file, int line) {
  uint64_t now = read_cycles();
  uint64_t delta = (last == 0) ? 0 : (now - last);
  last = now;

  dprintf(2, "LINE %s:%d cycles=%llu\n", file, line,
          (unsigned long long)delta);
}

static uint64_t loop_start = 0;
static const char* loop_file = NULL;
static int loop_line = 0;

__attribute__((no_instrument_function))
void __rt_loop_start(const char* file, int line) {
  loop_start = read_cycles();
  loop_file = file;
  loop_line = line;
}

__attribute__((no_instrument_function))
void __rt_loop_end(const char* file, int line) {
  (void)file;
  (void)line;
  uint64_t end = read_cycles();
  if (loop_start != 0) {
    uint64_t delta = end - loop_start;
    dprintf(2, "LOOP %s:%d total_cycles=%llu\n",
            loop_file ? loop_file : "?", loop_line,
            (unsigned long long)delta);
  }
  loop_start = 0;
}
