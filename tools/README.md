# tools

Every tool has two faces that read the same records: a command line (JSON, exit codes) and an interface written in openmui (D-61).

| Package | |
|---|---|
| [cli](cli) | `omui`: agent (ls, describe, snapshot, act, invoke, wait), check, fmt, schema, mcp |
| [gen](gen) | `omui-gen`: the registry and contracts to the C ABI, C++ partitions, script stubs, schemas |
| [mcp](mcp) | An MCP server for any running openmui application |
| [inspector](inspector) | The Inspector: semantic tree, node details, frame times, pipeline and time control |

[lint](lint) checks the repository's rules; [ci](ci) holds the scripts CI runs.
