#define _GNU_SOURCE
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

static sigjmp_buf jmp_env;

static void on_sigill(int signum) {
  (void)signum;
  siglongjmp(jmp_env, 1);
}

static uint64_t read_cycles_direct(void) {
#if defined(__aarch64__)
  uint64_t val = 0;
  asm volatile("isb" ::: "memory");
  asm volatile("mrs %0, pmccntr_el0" : "=r"(val));
  asm volatile("isb" ::: "memory");
  return val;
#else
  return 0;
#endif
}

int main(void) {
  struct sigaction sa;
  sa.sa_handler = on_sigill;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGILL, &sa, NULL);

  if (sigsetjmp(jmp_env, 1) != 0) {
    fprintf(stderr,
            "Direct PMU read failed (SIGILL).\n"
            "Enable PMU user access in EL1 before running this test.\n");
    return 1;
  }

  uint64_t start = read_cycles_direct();

  volatile uint64_t sink = 0;
  for (uint64_t i = 0; i < 20000000ULL; ++i) {
    sink += (i ^ (i >> 3)) & 0xFF;
  }

  uint64_t end = read_cycles_direct();
  uint64_t cycles = end - start;

  printf("PMU cycle read OK\n");
  printf("cycles=%" PRIu64 "\n", cycles);
  printf("sink=%" PRIu64 "\n", sink);

  if (cycles == 0) {
    fprintf(stderr, "cycles is 0: PMU likely not configured/accessible\n");
    return 2;
  }

  return 0;
}
