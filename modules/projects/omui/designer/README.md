# omui.designer

The designer of Live Design (D-149, D-151): the design.* verbs, the design's history, previews and variants in stand-ins, and the timeline of an application in development mode.

- **What it adds.** A program that links it has the `design.*` verbs of Live Design's catalog (`modules/projects/omui/agent/design.ops.omui.ts`, served by `omui-mcp` as `design_*` tools). Nothing has to start it: the designer registers itself as the program starts, and every player in development mode (`omui dev`) has one.
- **Why a package of its own.** A release is built without it (D-149: the designer is left out at build time). The entry player links it only under its `design` feature, which `omui dev` turns on. The player itself knows only that a designer may be linked (`omui::player::provide_designer`). It gives the designer its sources through its API: `sources`, `text`, `edit`, `commit_edits`, `discard_edits`, `language_of`.
- **What it keeps.** The history lives beside the application in `.omui/design/`: texts addressed by content, commits, branches, tags and pictures (`DesignHistory`). `omui design sync` carries it into git as `design/<branch>`.

Tests: `mcpp test -p omui.designer` (design edits, built_at both ways, structured edits, history, merge, previews, variants, timeline, scenarios).
