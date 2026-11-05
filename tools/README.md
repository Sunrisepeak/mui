# tools

Every tool has two faces that read the same records: a command line (JSON, exit codes) and an interface written in omui (D-61).

| Package | |
|---|---|
| [cli](cli) | `omui`: agent (ls, describe, snapshot, act, invoke, wait), check, fmt, schema, mcp |
| [gen](gen) | `omui-gen`: the registry and contracts to the C ABI, C++ partitions, script stubs, schemas |
| [mcp](mcp) | An MCP server for any running openmui application |
| [inspector](inspector) | Semantic tree, node details, frame times and time control; Reaction lab with editable cellular projects, CLI and actual process capture |

[lint](lint) checks the repository's rules; [ci](ci) holds the scripts CI runs. [repin.py](repin.py) re-pins hashes from a test run's "expected …, got …" failures, on the very lines, once the pictures have been looked at (`mcpp test … 2>&1 | python3 tools/repin.py`).

The [dual-user tools plan](../.agents/docs/2026-11-04-dual-user-tools-validation-design.md)
selects six core tool families and their real validation workflows. Future
designers, animation/audio/MIDI editors and simulation/material workbenches
reuse public engine APIs, shared project operations and openas semantics.
They follow their engine capabilities; the table above lists existing tools,
not a claim that the planned editors are already implemented.
