#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: bb_sweep_all.sh <source.c>" >&2
  echo "example: bb_sweep_all.sh ./examples/basic_example.c" >&2
  exit 1
fi

SRC="$1"
PLUGIN="./gcc_plugin/rt_instrument_plugin.so"
RUNTIME_OBJ="./runtime/rt_record.o"
BBMAP="./bb_map.json"
LOG="./bb_sweep.log"
SUMMARY="./bb_sweep_summary.txt"
BIN="./bb_profile.out"

# Build plugin + runtime once
g++ -shared -fPIC -o "$PLUGIN" \
  -I"$(gcc -print-file-name=plugin)/include" \
  ./gcc_plugin/rt_instrument_plugin.cc -fno-rtti -O2

gcc -c -O0 -g ./runtime/rt_record.c -o "$RUNTIME_OBJ"

# Generate BB map (compile only; no run)
gcc -fplugin="$PLUGIN" \
  -fplugin-arg-rt_instrument_plugin-bbmap="$BBMAP" \
  -O0 -g -c "$SRC" -o /tmp/bb_map_only.o

if [[ ! -f "$BBMAP" ]]; then
  echo "Failed to generate $BBMAP" >&2
  exit 1
fi

: > "$LOG"
: > "$SUMMARY"

ROW_FMT="%-4s | %-16s | %-22s | %6s | %13s | %10s | %-12s\n"
SEP_FMT="%-4s | %-16s | %-22s | %6s | %13s | %10s | %-12s\n"
printf "$ROW_FMT" \
  "Seq" "Function" "Location" "Hits" "TotalCycles" "Avg" "Status" | tee -a "$SUMMARY"
printf "$SEP_FMT" \
  "----" "----------------" "----------------------" "------" "-------------" "----------" "------------" | tee -a "$SUMMARY"

# Load full BB metadata once (id, seq, function, file, line)
# Location uses basename only to keep column width bounded.
BB_META=$(python3 - <<'PY'
import json, os
with open('./bb_map.json', 'r') as f:
    data = json.load(f)
for x in data:
    if isinstance(x, dict) and 'id' in x:
        loc = os.path.basename(x.get('file', 'unknown')) + ':' + str(x.get('line', 0))
        print(x['id'], x.get('seq', '?'), x.get('function', 'unknown'), loc)
PY
)

if [[ -z "$BB_META" ]]; then
  echo "No BB entries found in $BBMAP" >&2
  exit 1
fi

while IFS=' ' read -r id seq func location; do
  gcc -fplugin="$PLUGIN" \
    -fplugin-arg-rt_instrument_plugin-bbmap="$BBMAP" \
    -fplugin-arg-rt_instrument_plugin-bbid="$id" \
    -O0 -g "$SRC" "$RUNTIME_OBJ" -o "$BIN"

  set +e
  run_out=$("$BIN" 2>&1)
  run_rc=$?
  set -e

  {
    echo "=== BB seq=${seq} | ${func} | ${location} ==="
    printf "%s\n" "$run_out"
    echo ""
  } >> "$LOG"

  if [[ "$run_rc" -eq 132 ]]; then
    echo "BB seq=${seq} (${func} ${location}) failed: Illegal instruction — load PMU module first:" | tee -a "$SUMMARY"
    echo "  cd ~/rt-profiler/kernel_module && make && sudo insmod ./pmu_el0_enable.ko" | tee -a "$SUMMARY"
    exit 132
  fi

  if [[ "$run_rc" -ge 128 ]]; then
    echo "BB seq=${seq} (${func} ${location}) failed: terminated by signal" | tee -a "$SUMMARY"
    exit "$run_rc"
  fi

  bb_line=$(printf "%s\n" "$run_out" | grep "^BB $id " | tail -n 1 || true)

  if [[ -n "$bb_line" ]]; then
    hits=$(printf "%s\n" "$bb_line" | sed -n 's/.* hits=\([0-9][0-9]*\) .*/\1/p')
    total=$(printf "%s\n" "$bb_line" | sed -n 's/.* total_cycles=\([0-9][0-9]*\) .*/\1/p')
    avg=$(printf "%s\n" "$bb_line" | sed -n 's/.* avg=\([0-9][0-9]*\)$/\1/p')
    status="ok"
  else
    hits="0"
    total="0"
    avg="0"
    status="not-executed"
  fi

  [[ -n "$hits" ]] || hits="0"
  [[ -n "$total" ]] || total="0"
  [[ -n "$avg" ]] || avg="0"

  printf "$ROW_FMT" \
    "$seq" "$func" "$location" "$hits" "$total" "$avg" "$status" | tee -a "$SUMMARY"

done <<< "$BB_META"

echo ""
echo "BB sweep complete"
echo "map:     $BBMAP"
echo "log:     $LOG"
echo "summary: $SUMMARY"
