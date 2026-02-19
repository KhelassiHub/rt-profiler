#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: compare_aggregate.sh <source.c> <start_line>" >&2
  echo "example: compare_aggregate.sh ./examples/basic_example.c 1" >&2
  exit 1
fi

SRC="$1"
START_LINE="$2"
PLUGIN="./gcc_plugin/rt_instrument_plugin.so"
RUNTIME_OBJ="./runtime/rt_record.o"
AGG_LOG="./measurements.log"
FULL_LOG="./full_program.log"
FULL_BIN="./full_program.out"

# Build plugin + runtime
 g++ -shared -fPIC -o "$PLUGIN" \
  -I"$(gcc -print-file-name=plugin)/include" \
  ./gcc_plugin/rt_instrument_plugin.cc -fno-rtti -O2

gcc -c -O0 -g ./runtime/rt_record.c -o "$RUNTIME_OBJ"

# Per-line/loop sweep aggregate
bash ./tools/run_all.sh "$SRC" "$START_LINE"

line_sum=$(grep '^LINE ' "$AGG_LOG" | awk -F'cycles=' '{sum += $2+0} END {print sum+0}')
loop_sum=$(grep '^LOOP ' "$AGG_LOG" | awk -F'total_cycles=' '{sum += $2+0} END {print sum+0}')
agg_total=$((line_sum + loop_sum))

# Full-program one shot
: > "$FULL_LOG"
gcc -fplugin="$PLUGIN" \
  -fplugin-arg-rt_instrument_plugin-mode=full \
  -O0 -g "$SRC" "$RUNTIME_OBJ" -o "$FULL_BIN"
"$FULL_BIN" 2>> "$FULL_LOG" >/dev/null || true

full_total=$(grep '^PROGRAM ' "$FULL_LOG" | tail -n1 | awk -F'total_cycles=' '{print $2+0}')

if [[ -z "${full_total:-}" ]]; then
  echo "Failed to extract PROGRAM total from $FULL_LOG" >&2
  exit 1
fi

diff=$((agg_total - full_total))

if [[ "$full_total" -gt 0 ]]; then
  ratio=$(awk -v a="$agg_total" -v b="$full_total" 'BEGIN {printf "%.6f", a/b}')
else
  ratio="nan"
fi

echo "=== Comparison ==="
echo "source           : $SRC"
echo "start_line       : $START_LINE"
echo "line_sum         : $line_sum"
echo "loop_sum         : $loop_sum"
echo "aggregate_total  : $agg_total"
echo "full_program     : $full_total"
echo "difference       : $diff"
echo "ratio(agg/full)  : $ratio"
echo "logs             : $AGG_LOG, $FULL_LOG"
