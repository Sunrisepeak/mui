#!/usr/bin/env bash
# M1 acceptance ② (implementation plan §19.2): `omui agent snapshot / find /
# act / wait` operate example 07 running as its own process, and an
# irreversible action without its answer ends in needs-answer (exit code 2).
set -euo pipefail
cd "$(dirname "$0")/../.."

# PROFILE=release uses the release build (CI's release row); the newest binaries are this build's.
mcpp build -p openmui-example-headless-and-agent -p openmui-cli ${PROFILE:+--profile "$PROFILE"} > /dev/null
newest() { find target -path "*/bin/$1" -type f -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2; }
app=$(newest openmui-example-headless-and-agent/notes-agent)
omui=$(newest openmui-cli/omui)
dir=$(mktemp -d)
trap 'kill "$pid" 2> /dev/null || true; rm -rf "$dir"' EXIT

"$app" --serve --agent-dir "$dir" > "$dir/app.log" 2>&1 &
pid=$!
for _ in $(seq 100); do [ -e "$dir/org.openmui.examples.notes.endpoint.omui.json" ] && break; sleep 0.1; done

agent() { "$omui" agent "$@" --dir "$dir"; }
expect() {  # expect <exit code> <command…>
    local want=$1; shift
    set +e; out=$("$@"); got=$?; set -e
    if [ "$got" != "$want" ]; then echo "FAIL ($got, wanted $want): $*"; echo "$out"; exit 1; fi
    echo "ok   $*"
    last=$out
}

expect 0 agent ls
grep -q '^org.openmui.examples.notes' <<< "$last"
expect 0 agent snapshot org.openmui.examples.notes --interactive
expect 0 agent find org.openmui.examples.notes '{"role":"button","name":"Delete*"}'
grep -q '"effect":"irreversible"' <<< "$last"
expect 0 agent act org.openmui.examples.notes main/compose/draft set_value --args '{"value":"From the shell"}'
expect 0 agent act org.openmui.examples.notes main/compose/add activate
expect 0 agent wait org.openmui.examples.notes --q '{"name":"3 notes"}' --timeout 5
expect 2 agent act org.openmui.examples.notes main/delete-all activate --actor 'agent:cleaner;via=person:alice'
grep -q 'needs-answer' <<< "$last"
expect 0 agent find org.openmui.examples.notes --name '3 notes'
expect 0 agent act org.openmui.examples.notes main/delete-all activate --answer delete
expect 0 agent wait org.openmui.examples.notes --q '{"name":"0 notes"}' --settled --timeout 5
expect 0 agent capture org.openmui.examples.notes --out "$dir/after.png"
[ -s "$dir/after.png" ]
echo "agent e2e: ok"
