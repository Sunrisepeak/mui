# omui-devtools

The contributors' tool (release design §6.6): the workspace's own checks that are more than a line of shell.

## ci

A CI run read row by row: each failing member, program and case with its first lines, set against the [red list](../../.agents/docs/STATUS.md#ci-red-list) (R12).

```sh
omui-devtools ci run 43685505783          # from GitHub (gh run view)
omui-devtools ci log run.log --jobs jobs.json   # a saved log (gh run view RUN --log-failed, or one job's from the API while the run goes on)
omui-devtools ci --json run 43685505783   # the same as JSON
```

Each failure lists the red rows that name it (its package, the package a `.cts` member checks, its suite's label, its case's words, its program); one no row names is marked `NEW`. A timeout shows the case that passed last before it; a member whose package did not build shows the build's errors. Exit 0 when every failure is in the red list, 1 when one is new, 2 when the run could not be read.

## govern

A command held within a share of the machine (AGENTS.md §5): at most `--jobs` compilers, assemblers and linkers of all its descendants run at once, whatever the command plans (mcpp builds workspace groups and host tools side by side, each with its own job count); while available memory is under `--memory-floor` (10%) of the total, fewer. The newest beyond the limit are paused and continued as the oldest finish; test programs and the build tools are never paused, since a test may measure time. Interrupting or terminating it continues what is paused and passes the request to the whole command.

```sh
omui-devtools govern --jobs 22 -- mcpp test --workspace
```

[`tools/dev/capped.sh`](../dev/capped.sh) runs every local build and test through it, building it first when it is not. What runs is read from the system's process table (`ps`) and paused and continued with `kill`, both run through mcpplibs.os, which does not yet list or pause programs it did not start; on Windows the command runs with its job count alone. `capped.sh` uses it where `/proc/meminfo` gives the memory figure.

## incremental

The same build twice: the second, unchanged, must leave every compilation output its databases name untouched (a generated source rewritten with the same bytes would recompile every importer). Wall time is reported, never judged: it is the runner's. The report, both builds' events and logs land beside `--report` (`target/incremental.report.omui.json`). CI's desktop rows run it after building every member.

```sh
omui-devtools incremental --workspace --profile ci --exclude omui.reflect
omui-devtools incremental -p mlibs.math
```

## atspi

Examples 03 (the tally) and 07 (the notes) read and operated through AT-SPI, as a screen reader does (D-21): the accessibility bus turned on as Orca turns it on, each example on SDL's offscreen driver found in the registry by its window's name, its tree walked (names, roles, states, values), the +1 button pressed through Action, the goal slider read and set through Value, the draft field's text read through Text and written through EditableText, its caret and a word's selection moved, and the note added. It speaks D-Bus through mpal's own client (no libdbus, no busctl); mpal is preview, so `atspi` is the one part of this tool that rests on a preview package. A JSON report of every check; exit 1 when one failed.

```sh
dbus-run-session -- omui-devtools atspi <tally> <notes-agent>    # tools/ci/atspi.sh builds both and runs it (CI's Linux x64 rows)
```

## api

A package's exported surface as text: the declarations its module interface units export, a class's public members, re-exported modules; bodies, initialisers and raw strings left out, one declaration a line. Committed beside a stable package as `api.omui.txt` (release plan §2.1: its API is v1), a change to the surface is a change to that file in the same commit; the tool's test holds every committed file to its package. It reads text, not the compiler's model. xrender's 2D packages (raster, vector, text, graph, frame, effect) hold theirs.

```sh
omui-devtools api -p modules/projects/xrender/vector            # print
omui-devtools api -p modules/projects/xrender/vector --write    # after a change meant to be one
```

## lint

The repository's rules (AGENTS.md; D-206: the lint in C++, a subcommand here rather than a script), 43 of them: the sources' text (macros, headers, platform, naming, the capability owners…), the manifests (layering, project-dag, layout, licences, platforms, pins, closures), what the sources import (imports, module-closure), and the data and documents (view documents against their schema through `mlibs.json`'s check, operations and skills, the agent surface, matter, STATUS, maturity, the platform table, the lead ledger). Sources are read in parallel; the whole repository takes a few seconds. `--self-test` plants each rule's violation in a small workspace of its own and shows the rule catches it and nothing else complains. CI's lint row runs both; `devtools.lint` holds the repository to every rule in each test row.

```sh
omui-devtools lint                 # every rule
omui-devtools lint --rule layering # one
omui-devtools lint --list          # the rules and what each checks
omui-devtools lint --self-test
```

## lead

The lead ledger's pages, `docs/{en,zh}/lead.md`, written from the ledger, the metrics and the features (the same ledger, the same text), and the check of the signature features against it (lint `lead`). `tools/eval/lead.py` still samples, judges and records; it moves here next.
