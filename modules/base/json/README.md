# mlibs.json

JSON documents read with their places. Every value is a `Node` holding its JSON pointer (RFC 6901). A problem made at a node is placed at that pointer's line and column in the text. Members keep the order the text has them. Parsing is nlohmann.json's; placing a pointer, and reading a few values in place (`Scan`), is the module's own scanner.

```cpp
import mlibs.json;

auto doc = mlibs::json::Document::parse(text, "piece.music.xdef.json");
if (!doc) return doc.error().text();             // malformed: one problem at its line and column
const auto tempo = doc->root()["music"]["tempo"];
if (!tempo.number()) problems.push_back(doc->problem(tempo, "the tempo is a number", "tempo: 96"));
// piece.music.xdef.json:3:14: /music/tempo: the tempo is a number (tempo: 96)

for (const auto& [key, member] : doc->root()["music"].members())
    if (!known(key)) problems.push_back(doc->problem(member, std::format("`{}` is not a key", key), mlibs::names::nearest(key, keys)));
```

- **Why it exists.** Data files are TypeScript modules evaluated to JSON snapshots (D-139). The readers below the interface layer read those snapshots, and they all need the same things: a value and where it is, and strict keys. This library is the single owner of that.
- **Absent is a node too.** `node["missing"]` is false and keeps its pointer, so a problem about a missing member is placed at the object that should hold it.
- **`locate(text, pointer)`** finds where a pointer's value begins without building anything. omui's `json_location` is this.
- **Read in place, no parser linked.** `Scan::read(text, file)` checks a text against RFC 8259's grammar with the module's own scanner (the one `locate` walks; a text's bytes are taken as they are, UTF-8 is not checked), and `text(pointer)`, `texts(pointer)` and `problem(pointer, …)` read a few values of it where they are. Nothing is built and nlohmann's parser is not linked: for a program that must stay small (omui-launch reads each runtime's `runtime.json` so, 71,652 B fewer than through `Document`). It takes and refuses what `Document::parse` does (the tests hold it to nlohmann on the grammar's edges). `quote(text)` writes a string as nlohmann's dump does.
- **The nearest name for a typo** is `mlibs.names`' (`mlibs.utils`): a reader's "did you mean `x`?" beside its problem.

Its own package, not a tool of `mlibs.utils`, because it takes nlohmann.json and `mlibs.utils` takes the standard library alone.
