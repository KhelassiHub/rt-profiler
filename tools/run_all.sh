#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 2 ]]; then
  echo "usage: run_all.sh <source.c> <start_line>" >&2
  exit 1
fi

SRC="$1"
START_LINE="$2"
BIN="./a.out"
PLUGIN="./gcc_plugin/line_instrument_plugin.so"
RUNTIME_OBJ="./runtime/rt_record.o"
LOOPMAP="./loop_map.json"
LOG="./measurements.log"

MAX_LINE=$(awk 'END{print NR}' "$SRC")
TARGET="${SRC}:${START_LINE}"

> "$LOG"

while true; do
  LINE_NUM="${TARGET##*:}"
  if [[ "$LINE_NUM" -gt "$MAX_LINE" ]]; then
    echo "Done. Reached end of file." >&2
    break
  fi

  gcc -fplugin="$PLUGIN" \
    -fplugin-arg-line_instrument_plugin-target="$TARGET" \
    -fplugin-arg-line_instrument_plugin-loopmap="$LOOPMAP" \
    -O0 -g "$SRC" "$RUNTIME_OBJ" -o "$BIN"

  echo "Running target $TARGET" >&2
  "$BIN" 2>> "$LOG"

  TARGET=$(python3 ./tools/next_target.py "$TARGET")
done
