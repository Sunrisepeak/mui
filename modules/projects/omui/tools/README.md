# omui's tools

omui's toolkit (plan §4.5, D-203): what builds, runs and drives an omui application, kept with the project so that omui can be taken out on its own.

| Package | |
|---|---|
| [cli](cli) | `omui`: the one entry of an omui application — new, dev, run, build, test, package; agent (ls, describe, snapshot, act, invoke, wait), check, fmt, schema, mcp |
| [gen](gen) | `omui-gen`: the registry and contracts to the C ABI, C++ partitions, script stubs, schemas, operation catalogs; what the build plugins in `mcpp/` run |
| [mcp](mcp) | An MCP server for any running omui application; `--way` serves the semantic tools, or (for comparison, D-142) an accessibility tree or screenshots |

They depend on omui and the libraries beneath it, never on the repository's root `tools/` (lint `splittable`).
