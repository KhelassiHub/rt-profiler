# rt-profiler

Per-line instrumentation with loop-boundary fallback for Raspberry Pi 3 using GCC plugins and direct PMU counter reads.

## Structure
- gcc_plugin/rt_instrument_plugin.cc: GCC plugin
- runtime/rt_record.c: PMU reader + logging hooks
- tools/next_target.py: advance target line (skip loops)
- tools/run_all.sh: compile/run/advance automation

## Build the plugin and runtime
```
# Build plugin
g++ -shared -fPIC -o ./gcc_plugin/rt_instrument_plugin.so \
  -I"$(gcc -print-file-name=plugin)/include" \
  ./gcc_plugin/rt_instrument_plugin.cc -fno-rtti -O2

# Build runtime object
gcc -c -O0 -g ./runtime/rt_record.c -o ./runtime/rt_record.o
```

## Run a full sweep (per line)
```
chmod +x ./tools/run_all.sh
chmod +x ./tools/next_target.py

./tools/run_all.sh ./your_program.c 1
```

## Work locally, execute on Raspberry Pi 3

### Continuous sync only
```
./tools/watch_sync.sh pi@<pi-ip> /home/pi/rt-profiler .
```

## Basic example (recommended first test)

Source file:
- `examples/basic_example.c`

On Raspberry Pi (inside project folder), build runtime/plugin and run sweep:
```
g++ -shared -fPIC -o ./gcc_plugin/rt_instrument_plugin.so \
  -I"$(gcc -print-file-name=plugin)/include" \
  ./gcc_plugin/rt_instrument_plugin.cc -fno-rtti -O2

gcc -c -O0 -g ./runtime/rt_record.c -o ./runtime/rt_record.o

./tools/run_all.sh ./examples/basic_example.c 1
```

Expected behavior:
- non-loop lines produce `LINE ... cycles=...`
- loop-covered target lines produce `LOOP ... total_cycles=...`
- next target jumps to the first line after loop (from `loop_map.json`)
- all runs are appended to `measurements.log`

## PMU read self-test (without plugin)

Use this first to verify PMU access on the Pi:
```
gcc -O2 -Wall -Wextra ./examples/pmu_read_test.c -o ./examples/pmu_read_test
./examples/pmu_read_test
```

If `perf_event_open` fails due to permissions:
```
Direct PMU mode requires EL1 to enable PMU user access (PMCCNTR_EL0).
If this test fails with SIGILL, enable PMU user access in kernel/EL1 first.
```

### Enable direct PMU access from EL1 (kernel module)

On the Raspberry Pi:
```
sudo apt update
sudo apt install -y raspberrypi-kernel-headers build-essential

cd ~/rt-profiler/kernel_module
make
sudo insmod ./pmu_el0_enable.ko
```

Verify module is loaded:
```
lsmod | grep pmu_el0_enable
```

Then rerun PMU self-test:
```
cd ~/rt-profiler
gcc -O2 -Wall -Wextra ./examples/pmu_read_test.c -o ./examples/pmu_read_test
./examples/pmu_read_test
```

Unload when needed:
```
sudo rmmod pmu_el0_enable
```

## Notes
- Use `-O0 -g` for best line fidelity.
- The loop map is written to `./loop_map.json` at compile time.
- If the target line is inside a loop, the plugin instruments the loop boundary instead, then the driver jumps to the first line after the loop.

## Verify plugin insertion (GIMPLE + assembly)

To inspect exactly what was inserted for one target line:
```
./tools/inspect_insertion.sh ./examples/basic_example.c ./examples/basic_example.c:10
```

For full GCC dumps (verbose mode):
```
./tools/inspect_insertion.sh --full ./examples/basic_example.c ./examples/basic_example.c:10
```

This generates artifacts under `./inspect/`:
- `basic_example.s` : assembly with source annotations (`-fverbose-asm`)
- `hooks_summary.txt` : line numbers of inserted hook calls in assembly
- `loop_map.json` : loop ranges used by target skipping logic

In `--full` mode, additional `*.c.*` tree/RTL dumps are also generated.

## Compare aggregated sweep vs full-program total

This runs both:
- per-line + loop-boundary sweep (aggregated)
- one-shot full-program measurement (`main` entry to return)

```
./tools/compare_aggregate.sh ./examples/basic_example.c 1
```

Output fields:
- `aggregate_total`: `LINE` + `LOOP` sums from sweep
- `full_program`: total from `PROGRAM ... total_cycles=...`
- `ratio(agg/full)`: useful to estimate instrumentation overhead accumulation

## Basic-block instrumentation workflow

Per-run BB sweep (one BB instrumented per run, all BBs iterated):
```
./tools/BB/bb_sweep_all.sh ./examples/basic_example.c
```

This performs one run per BB id (recompile + run each step), similar to the per-line workflow.
Non-zero application return codes are accepted (only illegal-instruction/signal-like exits stop the sweep).

In BB mode, instrumentation is per-BB only: hooks are inserted only when `bbid` is selected.
If `mode=bb` is used without `bbid`, only `bb_map.json` is generated (no BB hooks inserted).

Generated outputs:
- `bb_map.json`: deterministic BB id mapping (`id`, `seq`, `preds`, `succs`, `function`, `file`, `line`)
- `bb_sweep_summary.txt`: compact per-BB table (`BBID`, `Location`, `Hits`, `TotalCycles`, `Avg`, `Status`)
- `bb_sweep.log`: detailed per-run raw outputs and runtime logs

Validate BB hook placement in binary:
```
./tools/BB/inspect_bb_hooks.sh ./examples/basic_example.c
./tools/BB/inspect_bb_hooks.sh ./examples/basic_example.c <bbid>
```

Outputs are written under `inspect_bb/`:
- `bb_disasm.txt`: full disassembly
- `hook_calls.txt`: only `__rt_bb_start/__rt_bb_end` call sites
- `bb_map.json`: BB metadata produced by the plugin

How it works:
- sweep mode recompiles once per BB id and inserts `__rt_bb_start/__rt_bb_end` only for that selected BB
- runtime aggregates total cycles and hits per BB id and prints at process exit

Quick checks:
- For non-loop target lines, expect `__rt_line_start` and `__rt_line_end`
- For loop-covered target lines, expect `__rt_loop_start` and `__rt_loop_end`
