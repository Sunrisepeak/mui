#!/usr/bin/env bash
# M1 acceptance ② (implementation plan §19.2): `omui agent snapshot / find /
# act / wait` operate example 07 running as its own process, and an
# irreversible action without its answer ends in needs-answer (exit code 2).
# Then the development operations against the same process: inspect, stats,
# a profile written as a Chrome trace, logs, and the debug overlays.
set -euo pipefail
cd "$(dirname "$0")/../.."

# PROFILE=release uses the release build (CI's release row); the newest binaries are this build's.
mcpp build -p openmui.example.headless-and-agent -p openmui.cli ${PROFILE:+--profile "$PROFILE"} > /dev/null
# By the binary's name: where mcpp puts it under bin/ is its business.
newest() { find target -path "*/bin/*" -name "$1" -type f -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2; }
app=$(newest notes-agent)
omui=$(newest omui)
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

app_id=org.openmui.examples.notes
expect 0 agent profile $app_id start
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

# Development: what the running application is doing.
expect 0 agent inspect $app_id main/compose/add
grep -q '"type":"button"' <<< "$last"
grep -q '"source":"[^"]*notes' <<< "$last"
expect 0 agent stats $app_id
grep -q '"p95_ms"' <<< "$last"
expect 0 agent debug $app_id --bounds --hud --slow 0.001 --log 'frame=debug,*=info'
expect 0 agent act $app_id main/compose/draft set_value --args '{"value":"traced"}'
expect 0 agent wait $app_id --settled --timeout 5
expect 0 agent capture $app_id --out "$dir/debug.png"
expect 0 agent logs $app_id --category frame --limit 5
grep -q '"category":"frame"' <<< "$last"
expect 0 agent debug $app_id --off
expect 0 agent profile $app_id stop --out "$dir/run.trace.json"
grep -q '"traceEvents"' "$dir/run.trace.json"
grep -q '"name":"layout"' "$dir/run.trace.json"
expect 0 agent profile $app_id start
expect 0 agent profile $app_id stop
grep -q '"summary"' <<< "$last"

# The application's time, controlled from outside (F4).
expect 0 agent clock $app_id pause
expect 0 agent clock $app_id step 0.5
expect 0 agent clock $app_id seek 0
expect 0 agent clock $app_id rate 0.25
expect 1 agent clock $app_id rate nope
expect 0 agent clock $app_id release
grep -q '"controlled":false' <<< "$last"

# The frame's pipeline, read and changed in the running process (D-66).
expect 0 agent pipeline $app_id
grep -q '"openmui.view.layout"' <<< "$last"
expect 0 agent pipeline $app_id --toml
grep -q '^\[\[stage\]\]' <<< "$last"
expect 0 agent pipeline $app_id remove --stage debug
expect 0 agent pipeline $app_id insert --stage debug --plugin openmui.view.debug --after paint
expect 1 agent pipeline $app_id replace --stage paint --plugin nonesuch
expect 0 agent wait $app_id --settled --timeout 5

# The MCP server (S3) over real pipes, as Claude Code runs it: the tools
# listed, a node found, an irreversible action refused to a client that
# cannot ask its user, a reversible one done.
mcp_out=$("$omui" mcp --dir "$dir" <<'JSON'
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"e2e"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"ui_find","arguments":{"name":"Delete*"}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"ui_act","arguments":{"node":"main/delete-all","action":"activate"}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"ui_act","arguments":{"node":"main/compose/draft","action":"set_value","args":{"value":"from MCP"}}}}
{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"ui_snapshot","arguments":{"root":"main/compose"}}}
JSON
)
echo "$mcp_out" | head -c 2000; echo
[ "$(grep -c '"jsonrpc":"2.0"' <<< "$mcp_out")" = 6 ] || { echo "FAIL: six responses from omui mcp"; exit 1; }
grep -q '"protocolVersion":"2025-06-18"' <<< "$mcp_out"
grep -q '"name":"ui_capture"' <<< "$mcp_out"
grep -q '"id":3.*main/delete-all' <<< "$mcp_out"
grep -q "\"id\":4.*needs the user's confirmation.*\"isError\":true" <<< "$mcp_out"
grep -q '"id":5.*done: set_value on main/compose/draft' <<< "$mcp_out"
grep -q '"id":6.*from MCP' <<< "$mcp_out"
echo "ok   omui mcp"
echo "agent e2e: ok"
