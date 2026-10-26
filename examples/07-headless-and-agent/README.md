# 07 — headless and agents

A small notes app (`src/notes.cppm`) and what headless openmui does with it:

- **H0** (semantics only), **H1** (layout, hit testing) and **H2** (pixels)
  give the same semantic tree;
- an **agent script** (`tests/data/delete-all.agent.omui.jsonl`): requests of
  `openmui.tree@0.1`, each with the outcome it expects;
- **effects**: "Delete all" invokes an irreversible verb, so its action is
  stopped with `needs-answer` (exit code 2) until called with `answer: delete`;
- a **recorded session** replays to the same tree.

```sh
mcpp run -- --serve &
omui agent find org.openmui.examples.notes '{"role":"button","name":"Delete*"}'
omui agent act org.openmui.examples.notes main/delete-all activate              # exit 2: needs-answer
omui agent act org.openmui.examples.notes main/delete-all activate --answer delete
```

`tools/ci/agent-e2e.sh` runs exactly this in CI, with the app in its own process.
