#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
make
sudo insmod ./pmu_el0_enable.ko || true
if lsmod | grep -q '^pmu_el0_enable\b'; then
  echo "pmu_el0_enable loaded"
else
  echo "Failed to load pmu_el0_enable" >&2
  exit 1
fi
