#!/usr/bin/env bash
# Runs a build or test command within 90% of this machine (AGENTS.md §5): no
# more concurrent compiles and links than 90% of the cores, nor than 90% of
# the memory allows at about 2.25 GB a compile (a C++-modules compile of this
# tree peaks near that; the rare heavier one is held by the governor's memory
# floor), at low CPU and I/O priority, so the machine stays
# usable. MCPP_JOBS asks mcpp for that many; the governor (`omui-devtools
# govern`, tools/devtools, built here when it is not) holds the whole process
# tree to it, because mcpp builds workspace groups and host tools side by side,
# each with its own job count (measured: 37 compilers under MCPP_JOBS=22), and
# pauses more of them while available memory is under 10%.
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
    by_mem=$(( mem_mb * 9 / 10 / 2250 ))
    [ "$by_mem" -lt "$jobs" ] && jobs=$by_mem
fi
[ "$jobs" -lt 1 ] && jobs=1
if [ -n "${MCPP_JOBS:-}" ] && [ "$MCPP_JOBS" -lt "$jobs" ] 2>/dev/null; then jobs=$MCPP_JOBS; fi
export MCPP_JOBS=$jobs
low=(nice -n 10)
command -v ionice > /dev/null && low=(ionice -c 3 "${low[@]}")
root=$(cd "$(dirname "$0")/../.." && pwd)
if [ -r /proc/meminfo ]; then
    # The program itself, as mcpp's launch envelope names it (argv[0]): mcpp run's
    # environment is the tool's, not the command's.
    if launch=$(cd "$root" && "${low[@]}" "${MCPP:-mcpp}" run omui-devtools -p tools/devtools --print-launch 2> /dev/null); then
        governor=$(printf '%s\n' "$launch" | sed -n '/"argv": \[/{n;s/^ *"\(.*\)",\{0,1\}$/\1/p;q;}')
        [ -x "$governor" ] && exec "${low[@]}" "$governor" govern --jobs "$jobs" -- "$@"
    fi
    echo "capped.sh: omui-devtools did not build; MCPP_JOBS=$jobs alone holds the command" >&2
fi
exec "${low[@]}" "$@"
