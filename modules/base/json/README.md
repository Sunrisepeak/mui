# mlibs.json

JSON documents read with their places. Every value is a `Node` holding its JSON pointer (RFC 6901). A problem made at a node is placed at that pointer's line and column in the text. Members keep the order the text has them. Parsing is nlohmann.json's.

```cpp
import mlibs.json;

auto doc = mlibs::json::Document::parse(text, "piece.music.omui.json");
if (!doc) return doc.error().text();             // malformed: one problem at its line and column
const auto tempo = doc->root()["music"]["tempo"];
if (!tempo.number()) problems.push_back(doc->problem(tempo, "the tempo is a number", "tempo: 96"));
// piece.music.omui.json:3:14: /music/tempo: the tempo is a number (tempo: 96)

for (const auto& [key, member] : doc->root()["music"].members())
    if (!known(key)) problems.push_back(doc->problem(member, std::format("`{}` is not a key", key), mlibs::json::nearest(key, keys)));
```

- **Why it exists.** Data files are TypeScript modules evaluated to JSON snapshots (D-139). The readers below the interface layer read those snapshots, and they all need the same things: a value and where it is, strict keys, and the nearest name for a typo. This library is the single owner of that.
- **Absent is a node too.** `node["missing"]` is false and keeps its pointer, so a problem about a missing member is placed at the object that should hold it.
- **`locate(text, pointer)`** finds where a pointer's value begins without building anything. omui's `json_location` is this.
- **`nearest(name, options)`** gives "did you mean `x`?" within a third of the name, never for a whole one-letter name. Otherwise it lists the options. `distance` counts edits, a swap of two neighbours as one.

Its own package, not a tool of `mlibs.utils`, because it takes nlohmann.json and `mlibs.utils` takes the standard library alone.
