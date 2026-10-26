#!/usr/bin/env bash
# M0 acceptance ⑤ (plan §19.2): a feature selects one of two mutually
# exclusive source files. Each choice builds and passes its test (the fixture
# with its default `sink-stdout`; a consumer that asks for `sink-memory`),
# and asking for both is refused before anything compiles: both files
# provide the module partition `openmui.features:sink`.
set -euo pipefail
cd "$(dirname "$0")/../.."

mcpp test -p openmui-feature-fixture -p openmui-feature-fixture-memory > /tmp/omui-features.log 2>&1 || { cat /tmp/omui-features.log; exit 1; }
grep -q '^sink: stdout' /tmp/omui-features.log
echo "ok   sink-stdout (default) and sink-memory (a consumer's choice) build and pass"

set +e
mcpp build -p openmui-feature-fixture --features sink-memory > /tmp/omui-features.log 2>&1
code=$?
set -e
if [ "$code" = 0 ]; then echo "FAIL: both sinks at once built"; exit 1; fi
grep -q "module 'openmui.features:sink' is provided by" /tmp/omui-features.log || { cat /tmp/omui-features.log; exit 1; }
echo "ok   both at once is refused: the partition would be provided twice"
rm -f /tmp/omui-features.log
echo "features: ok"
