#!/usr/bin/env bash
# Runs a build or test command within 90% of this machine (AGENTS.md §5): no
# more concurrent compiles than 90% of the cores, nor than 90% of the memory
# allows at about 2.5 GB a compile (a C++-modules compile of this tree peaks
# near that), at low CPU and I/O priority, so the machine stays usable.
#
#     tools/dev/capped.sh mcpp build --workspace
#     tools/dev/capped.sh mcpp test -p omui.view
#
# MCPP_JOBS already set is kept if it is lower. Where the platform has no
# /proc/meminfo (macOS, Windows), the core count alone decides.
set -euo pipefail
cores=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
jobs=$(( cores * 9 / 10 ))
if [ -r /proc/meminfo ]; then
    mem_mb=$(awk '/^MemTotal:/ {print int($2 / 1024)}' /proc/meminfo)
    by_mem=$(( mem_mb * 9 / 10 / 2560 ))
    [ "$by_mem" -lt "$jobs" ] && jobs=$by_mem
fi
[ "$jobs" -lt 1 ] && jobs=1
if [ -n "${MCPP_JOBS:-}" ] && [ "$MCPP_JOBS" -lt "$jobs" ] 2>/dev/null; then jobs=$MCPP_JOBS; fi
export MCPP_JOBS=$jobs
low=(nice -n 10)
command -v ionice > /dev/null && low=(ionice -c 3 "${low[@]}")
exec "${low[@]}" "$@"
