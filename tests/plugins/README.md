# tests/plugins

One third-party package per build plugin (`mcpp/README.md`). Each package is built with the plugin, the way someone else's package would be, and its tests check what the plugin made.

| Package | Plugin | What it checks |
|---|---|---|
| [contracts](contracts) (`acme.ui`) | `openmui.build.contracts` | A badge's contract became its typed options, two-way count, enum, event and action |
| [embed](embed) (`acme.data`) | `openmui.build.embed` | Each file is a partition holding its text, byte for byte. Stems with `-` and `.`, and UTF-8, are covered. The tag's table lists every file of the suffix and no other; `themes()` and `behaviors()` are covered too |

`openmui.build.toy` is checked on the packages it makes: `tools/ci/toy.sh` checks the games and `tools/ci/site.sh` checks the site.
