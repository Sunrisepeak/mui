# tests/plugins

A third-party package built with openmui's build plugins (`mcpp/README.md`), the way someone else's package would be; its tests check what the plugins made. One package, `acme.plugins`, with two modules as two packages would have them:

| Module | Plugin | What it checks |
|---|---|---|
| `acme.ui` ([src/ui](src/ui), [contract](contract)) | `omui.build.contracts` | A badge's contract became its typed options, two-way count, enum, event and action (`tests/badge_test.cpp`) |
| `acme.data` ([src/data](src/data), [data](data), [themes](themes), [behaviors](behaviors)) | `omui.build.embed` | Each file is a partition holding its text, byte for byte. Stems with `-` and `.`, and UTF-8, are covered. The tag's table lists every file of the suffix and no other; `themes()` and `behaviors()` are covered too (`tests/embed_test.cpp`) |

`omui.build.toy` is checked on the packages it makes: `omui-devtools check toy` checks the games and `omui-devtools check site` checks the site.
