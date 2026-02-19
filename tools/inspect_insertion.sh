#!/usr/bin/env bash
set -euo pipefail

FULL_MODE=0
if [[ "${1:-}" == "--full" ]]; then
  FULL_MODE=1
  shift
fi

if [[ $# -lt 2 ]]; then
  echo "usage: inspect_insertion.sh [--full] <source.c> <file:line>" >&2
  echo "example: inspect_insertion.sh ./examples/basic_example.c ./examples/basic_example.c:10" >&2
  echo "example: inspect_insertion.sh --full ./examples/basic_example.c ./examples/basic_example.c:10" >&2
  exit 1
fi

SRC="$1"
TARGET="$2"
PLUGIN="./gcc_plugin/rt_instrument_plugin.so"
OUTDIR="./inspect"
BASENAME="$(basename "$SRC" .c)"
ASM_OUT="${OUTDIR}/${BASENAME}.s"
HOOKS_OUT="${OUTDIR}/hooks_summary.txt"

mkdir -p "$OUTDIR"
rm -rf "$OUTDIR"/*

echo "[inspect] target=$TARGET"

# Default mode: keep outputs minimal (assembly + loop map + summary).
# Full mode: include full GCC tree/RTL dumps for deep debugging.
EXTRA_DUMPS=()
if [[ "$FULL_MODE" -eq 1 ]]; then
  EXTRA_DUMPS=("-save-temps=obj" "-fdump-tree-all" "-fdump-rtl-expand")
fi

gcc -O0 -g -S -fverbose-asm \
  -fplugin="$PLUGIN" \
  -fplugin-arg-rt_instrument_plugin-target="$TARGET" \
  -fplugin-arg-rt_instrument_plugin-loopmap="${OUTDIR}/loop_map.json" \
  "${EXTRA_DUMPS[@]}" \
  "$SRC" -o "$ASM_OUT" 2>"${OUTDIR}/build.log" || {
    echo "[inspect] build failed; see ${OUTDIR}/build.log" >&2
    exit 1
  }

if [[ "$FULL_MODE" -eq 1 ]]; then
  find . -maxdepth 1 -type f \( -name "*.c.*" -o -name "*.i" \) -exec mv -f {} "$OUTDIR"/ \; 2>/dev/null || true
fi

echo "[inspect] Generated artifacts in ${OUTDIR}/"

grep -n "__rt_line_start\|__rt_line_end\|__rt_loop_start\|__rt_loop_end" "$ASM_OUT" > "$HOOKS_OUT" || true

echo "[inspect] Hook summary: ${HOOKS_OUT}"
cat "$HOOKS_OUT" || true

echo "[inspect] Loop map: ${OUTDIR}/loop_map.json"
if [[ -f "${OUTDIR}/loop_map.json" ]]; then
  cat "${OUTDIR}/loop_map.json"
fi

if [[ "$FULL_MODE" -eq 1 ]]; then
  echo "[inspect] Full dump mode enabled: tree/RTL dumps kept in ${OUTDIR}/"
fi

echo "[inspect] Done"
