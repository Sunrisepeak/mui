# openmui.record

Sessions as data: a header and timestamped entries, one JSON object per line
(`<name>.session.omui.jsonl`). Entries stay ordered by time; a session can be
sought by binary search, sliced, merged with another, written and read back
byte-identical. What an entry means is its kind's business — the runtime
records frames with input events, semantic actions with their actor and
answer, and checkpoints of the semantic tree (`openmui.semantic`), and replays
them exactly in headless mode.

An independent component (D-57): it depends on the standard library,
nlohmann/json and mcpplibs.os (files), and knows nothing of nodes or views.

```cpp
import openmui.record;

omui::record::Session s;
s.header.app = "org.example.notes";
s.add(0.5, "act", R"({"node":"main/save","action":"activate"})");
s.save("bug.session.omui.jsonl");
```
