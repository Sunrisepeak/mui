# openmui.build.plan

What openmui's build plugins share (`mcpp/README.md`); each plugin re-exports it. A plugin plans `edge`s:
- each edge has a command, its inputs and outputs;
- for a generated module unit, the edge also says what the unit provides and what it imports, because a file that does not exist yet cannot be scanned.

`submit()` hands the edges to mcpp as source actions. Other helpers:
- `generator()`: `omui-gen` as built for this build;
- `files_ending()`, `files_under()`, `table_names()`: the files a plugin reads;
- `utf8()`, `generic_utf8()`: a path's text, never through the process's code page;
- `namespace_of()`: a module's C++ namespace.
