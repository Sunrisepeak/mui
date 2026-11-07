#!/usr/bin/env bash
# Native accessibility, through the protocol a screen reader speaks (D-21):
# examples 03 and 07 run on SDL's offscreen driver and tools/ci/atspi.py, an
# AT-SPI client over D-Bus, reads their trees (names, roles, states, values,
# a text field's text, caret, selection and words) and operates them
# (Action, Value, Text, EditableText) on a private session bus with the
# accessibility bus turned on, as Orca turns it on.
# Linux x86_64 (AccessKit's library); at-spi2-core and busctl.
set -euo pipefail
cd "$(dirname "$0")/../.."

mcpp build -p omui.example.counter -p omui.example.headless-and-agent ${PROFILE:+--profile "$PROFILE"} > /dev/null
newest() { find target -path "*/bin/*" -name "$1" -type f -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2; }
app=$(newest tally)
notes=$(newest notes-agent)
out=$(mktemp)
trap 'rm -f "$out"' EXIT
status=0
dbus-run-session -- python3 tools/ci/atspi.py "$app" "$notes" > "$out" || status=$?
# The registry's own line comes before the report.
sed -n '/^{/,$p' "$out"
exit "$status"
