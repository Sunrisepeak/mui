#!/usr/bin/env bash
# W5 (D-144): interface generated into a TypeScript application as it runs.
# A player application whose omui.config.ts declares a surface (a slot) and
# opens generation to `limited` in a release; an agent, through `omui agent
# call`, drafts a view from TypeScript code, previews it, commits it, undoes
# and redoes it — and is refused a component it was not given and code that
# does not end.
set -euo pipefail
cd "$(dirname "$0")/../.."

mcpp build -p omui.example.hello-ts -p omui.cli ${PROFILE:+--profile "$PROFILE"} > /dev/null
newest() { find target -path "*/bin/*" -name "$1" -type f -printf '%T@ %p\n' | sort -rn | head -1 | cut -d' ' -f2; }
player=$(newest text-stats)  # the player entry, run with another manifest
omui=$(newest omui)
dir=$(mktemp -d)
trap 'kill "$pid" 2> /dev/null || true; rm -rf "$dir"' EXIT

mkdir -p "$dir/app/logic" "$dir/endpoints"
cat > "$dir/app/mcpp.toml" << 'TOML'
[package]
name = "plan"
version = "0.1.0"

[package.metadata.openmui]
kind = "app"
id = "org.openmui.plan"
title = "Plan"
size = "360x240"

[package.metadata.openmui.scripts]
main = "logic/main.ts"
TOML
cat > "$dir/app/logic/main.ts" << 'TS'
import { column, text, slot, mount } from "openmui";
mount("main", column({ padding: 8, gap: 6 }, [text({ id: "plan", text: "Basic plan" }), slot("extras")]));
TS
cat > "$dir/app/omui.config.ts" << 'TS'
import { defineConfig } from "omui/define";
export default defineConfig({
  generation: { surfaces: { extras: "cards an agent adds under the plan" }, open: { release: "limited" }, components: ["text", "column"] },
});
TS

"$player" --manifest "$dir/app" --serve --agent-dir "$dir/endpoints" > "$dir/app.log" 2>&1 &
pid=$!
for _ in $(seq 100); do [ -e "$dir/endpoints/org.openmui.plan.endpoint.omui.json" ] && break; sleep 0.1; done

call() { "$omui" agent call org.openmui.plan "$1" --dir "$dir/endpoints"; }
expect() {  # expect <exit code> <request>
    local want=$1; shift
    set +e; out=$(call "$1"); got=$?; set -e
    if [ "$got" != "$want" ]; then echo "FAIL ($got, wanted $want): $1"; echo "$out"; cat "$dir/app.log"; exit 1; fi
    echo "ok   $1" | cut -c1-120
    last=$out
}

expect 0 '{"op":"invoke","verb":"generate.surfaces"}'
grep -q '"openness":"limited"' <<< "$last"
grep -q '"shown":true' <<< "$last"
code='import { defineView } from \"openmui/define\";\nconst plans = [[\"Pro\", 9], [\"Team\", 29]];\nexport default defineView({ omui: \"0.1\", view: { root: { type: \"column\", children: plans.map(([name, price]) => ({ type: \"text\", id: name.toLowerCase(), props: { text: `${name}, ${price} a month` } })) } } });'
expect 0 "{\"op\":\"invoke\",\"verb\":\"generate.draft\",\"args\":{\"surface\":\"extras\",\"language\":\"ts\",\"code\":\"$code\"}}"
grep -q '"stage":"prepared"' <<< "$last"
expect 0 '{"op":"invoke","verb":"generate.preview","args":{"draft":"d1"}}'
grep -q 'Pro, 9 a month' <<< "$last"
grep -q '"after":{"format":"png"' <<< "$last"
expect 0 '{"op":"find","q":{"name":"*a month"}}'
grep -q '"nodes":\[\]' <<< "$last"  # the preview left nothing
expect 0 '{"op":"invoke","verb":"generate.commit","args":{"draft":"d1"}}'
expect 0 '{"op":"wait","settled":true,"timeout":5}'  # it comes in as a design step does
expect 0 '{"op":"find","q":{"name":"Team, 29 a month"}}'
grep -q 'main/extras/team' <<< "$last"
expect 0 '{"op":"undo"}'
expect 0 '{"op":"wait","settled":true,"timeout":5}'  # played back
expect 0 '{"op":"find","q":{"name":"*a month"}}'
grep -q '"nodes":\[\]' <<< "$last"
expect 0 '{"op":"redo"}'
expect 0 '{"op":"invoke","verb":"generate.versions","args":{"surface":"extras"}}'
grep -q '"shown":1' <<< "$last"
# Kept: started again, the version shown is shown again.
kill "$pid"; wait "$pid" 2> /dev/null || true
"$player" --manifest "$dir/app" --serve --agent-dir "$dir/endpoints" > "$dir/app.log" 2>&1 &
pid=$!
for _ in $(seq 100); do [ -e "$dir/endpoints/org.openmui.plan.endpoint.omui.json" ] && break; sleep 0.1; done
expect 0 '{"op":"wait","q":{"name":"Team, 29 a month"},"timeout":5}'
expect 1 '{"op":"invoke","verb":"generate.draft","args":{"surface":"extras","document":{"omui":"0.1","view":{"root":{"type":"box"}}}}}'
grep -q 'not among the components' <<< "$last"
expect 1 '{"op":"invoke","verb":"generate.draft","args":{"surface":"extras","language":"ts","code":"while (true) {}"}}'
grep -q 'out of its time' <<< "$last"
echo "generation e2e: ok"
