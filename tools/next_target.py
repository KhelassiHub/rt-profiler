#!/usr/bin/env python3
import json
import sys
import os

if len(sys.argv) != 2:
  print("usage: next_target.py file:line", file=sys.stderr)
  sys.exit(1)

file, line = sys.argv[1].rsplit(":", 1)
line = int(line)

loopmap_path = "./loop_map.json"

if not os.path.exists(loopmap_path):
  print(f"{file}:{line+1}")
  sys.exit(0)

with open(loopmap_path) as f:
  loops = json.load(f)

file_base = os.path.basename(file)

for L in loops:
  if os.path.basename(L["file"]) != file_base:
    continue
  if L["start"] <= line <= L["end"]:
    after = L.get("after", -1)
    if after > 0:
      print(f"{file}:{after}")
    else:
      print(f"{file}:{line+1}")
    sys.exit(0)

print(f"{file}:{line+1}")
