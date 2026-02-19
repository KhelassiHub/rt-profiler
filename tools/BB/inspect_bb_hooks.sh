#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  echo "usage: inspect_bb_hooks.sh <source.c> [bbid]" >&2
  echo "example: inspect_bb_hooks.sh ./examples/basic_example.c" >&2
  echo "example: inspect_bb_hooks.sh ./examples/basic_example.c 12345" >&2
  exit 1
fi

SRC="$1"
BBID="${2:-}"
PLUGIN="./gcc_plugin/rt_instrument_plugin.so"
RUNTIME_OBJ="./runtime/rt_record.o"
OUTDIR="./inspect_bb"
BIN="$OUTDIR/bb_inspect.out"
BBMAP="$OUTDIR/bb_map.json"
DISASM="$OUTDIR/bb_disasm.txt"
HOOKS="$OUTDIR/hook_calls.txt"

mkdir -p "$OUTDIR"
rm -f "$OUTDIR"/*

# Build plugin + runtime
g++ -shared -fPIC -o "$PLUGIN" \
  -I"$(gcc -print-file-name=plugin)/include" \
  ./gcc_plugin/rt_instrument_plugin.cc -fno-rtti -O2

gcc -c -O0 -g ./runtime/rt_record.c -o "$RUNTIME_OBJ"

PLUGIN_ARGS=(
  -fplugin="$PLUGIN"
  -fplugin-arg-rt_instrument_plugin-bbmap="$BBMAP"
)

if [[ -n "$BBID" ]]; then
  PLUGIN_ARGS+=("-fplugin-arg-rt_instrument_plugin-bbid=$BBID")
fi

gcc "${PLUGIN_ARGS[@]}" -O0 -g "$SRC" "$RUNTIME_OBJ" -o "$BIN"

objdump -drwC "$BIN" > "$DISASM"

grep -n "__rt_bb_start\|__rt_bb_end" "$DISASM" > "$HOOKS" || true

start_count=$(grep -c "__rt_bb_start" "$HOOKS" 2>/dev/null || true)
end_count=$(grep -c "__rt_bb_end" "$HOOKS" 2>/dev/null || true)

echo "[inspect-bb] binary : $BIN"
echo "[inspect-bb] bb_map : $BBMAP"
echo "[inspect-bb] disasm : $DISASM"
echo "[inspect-bb] hooks  : $HOOKS"
echo "[inspect-bb] start hooks: $start_count"
echo "[inspect-bb] end hooks  : $end_count"

if [[ -n "$BBID" ]]; then
  echo "[inspect-bb] targeted bbid: $BBID"
  if [[ "$start_count" -ne 1 || "$end_count" -ne 1 ]]; then
    echo "[inspect-bb] warning: expected exactly one start/end hook for single-bbid mode" >&2
  fi
fi

if [[ -f "$BBMAP" ]]; then
  echo "[inspect-bb] bb_map preview:" 
  head -n 40 "$BBMAP"
fi
