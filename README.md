# GPIL

Per-line instrumentation with loop-boundary fallback for Raspberry Pi 3 using GCC plugins and PMU counters.

## Structure
- gcc_plugin/line_instrument_plugin.cc: GCC plugin
- runtime/rt_record.c: PMU reader + logging hooks
- tools/next_target.py: advance target line (skip loops)
- tools/run_all.sh: compile/run/advance automation

## Build the plugin and runtime
```
# Build plugin
g++ -shared -fPIC -o ./gcc_plugin/line_instrument_plugin.so \
  -I"$(gcc -print-file-name=plugin)/include" \
  ./gcc_plugin/line_instrument_plugin.cc -fno-rtti -O2

# Build runtime object
gcc -c -O0 -g ./runtime/rt_record.c -o ./runtime/rt_record.o
```

## Run a full sweep (per line)
```
chmod +x ./tools/run_all.sh
chmod +x ./tools/next_target.py

./tools/run_all.sh ./your_program.c 1
```

## Notes
- Use `-O0 -g` for best line fidelity.
- The loop map is written to `./loop_map.json` at compile time.
- If the target line is inside a loop, the plugin instruments the loop boundary instead, then the driver jumps to the first line after the loop.
