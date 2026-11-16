# mlibs.xdef

Declared data's container, the tool kernel's: a data file's name, its format identifier, the registry of kinds that each owner keeps in its manifest, a data module's snapshot in its one canonical form, and the checker (D-209; design: `.agents/docs/2026-11-16-xdef-declared-data-design.md`). `xdef` is its thin program: the reference evaluator and checker.

It knows no kind's fields. A kind's content is its owner's: its define module, its reader, its CTS. `xdef` is an internal code name (D-208); its suffix is persisted, so a rename of it comes with a migration command.

```sh
xdef kinds                           # kind@major, its owner, its define module
xdef snapshot rise.motion.xdef.ts    # the module's canonical snapshot (--write: beside it, .json)
xdef check .                         # every data file's kind registered, every committed snapshot current
```

## The container (xdef 0.1)

1. **A file's name** is `<name>.<kind>.xdef.<extension>`, the extension `ts` (a data module), `json` (a snapshot, or data a tool writes) or `jsonl` (records a line each). `<kind>` is lowercase letters, digits, `-` and `_`. A file named with the earlier suffix (`.omui.`, before RC3.15) is moved by `xdef migrate [DIR] [--write]`: renamed, and every text that names it rewritten (D-208).
2. **A format identifier** is `<kind>@<major>`, the major a whole number from 1. In a snapshot it is the top-level `"$format"` and it is the authority; the suffix is a hint (RC3.15's step 4 writes it).
3. **The registry** is data: each owner registers its kinds in its own manifest. A kind has one owner, one major version and at most one define module; a kind with two owners is a problem.

   ```toml
   [package.metadata.openmui.xdef]
   kinds   = { config = 1, ops = 1, report = 1 }      # each kind and its major version
   modules = { "omui/define" = ["config", "ops"] }   # where a data module imports define<Kind> from (report: only tools write it)
   ```
4. **Versions**: within a major only fields are added; a new major comes with its owner's migration.
5. **The TypeScript subset** is the erasable one (mlibs.ts). Evaluation is deterministic: no clock, no random, no files, no network. A module imports its kind owner's define module and its own files.
6. **A snapshot's canonical form**: one object `{"<kind>": value}`, the module's key order, compact UTF-8, a line break at its end. The same declaration gives the same bytes.
7. **Problems** name the file (and, from a reader, the JSON pointer); error codes go by the error contract (mlibs.error).

## The checker

`check(root, registry)` walks a tree, skipping build trees and version control:

- no data file keeps the earlier container's suffix (`xdef migrate` renames it);
- every data file's kind is registered by some package;
- every module with a committed snapshot beside it evaluates to that snapshot's bytes now.
