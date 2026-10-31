#!/usr/bin/env bash
# Native accessibility, through the protocol a screen reader speaks (D-21):
# example 03 runs on SDL's offscreen driver and tools/ci/atspi.py, an AT-SPI
# client over D-Bus, reads its tree (names, roles, states, values) and
# operates it (Action.DoAction, Value.CurrentValue) on a private session
# bus with the accessibility bus turned on, as Orca turns it on.
# Linux x86_64 (AccessKit's library); at-spi2-core and busctl.
set -euo pipefail
cd "$(dirname "$0")/../.."

mcpp build -p openmui.example.counter ${PROFILE:+--profile "$PROFILE"} > /dev/null
newest() { find target -path "*/bin/*" -name "$1" -type f -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2; }
app=$(newest tally)
out=$(mktemp)
trap 'rm -f "$out"' EXIT
status=0
dbus-run-session -- python3 tools/ci/atspi.py "$app" > "$out" || status=$?
# The registry's own line comes before the report.
sed -n '/^{/,$p' "$out"
exit "$status"
