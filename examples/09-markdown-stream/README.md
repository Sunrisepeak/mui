# Example 09 — Markdown as it streams

`md-stream` renders Markdown while it is still being written: an LLM's answer piped into it, or the built-in demo.

```sh
some-llm "explain C++ modules" | mcpp run -p openmui.example.markdown-stream
mcpp run -p openmui.example.markdown-stream -- --demo --rate 40      # pieces per second
mcpp run -p openmui.example.markdown-stream -- --demo --no-fade --dark
```

- **Only the tail is parsed again.** Each piece is appended to the document's stream (`openmui.markdown`'s `Stream`). The blocks before the last stable boundary are frozen; the status line shows how many bytes each piece parsed.
- **Only the tail is laid out again.** The Markdown component keys blocks by content, so every finished block keeps its node, its shaped text and its layout.
- **New words fade in.** The newest block gets a glyph motion over just its new bytes (D-68): letters rise and fade in one after another. The text that was already there stays still, and the component needs no animation code.
- The document is marked `remote` (an LLM's text) on every node, so an agent reading it knows where it came from.

Standard input is read on a thread of its own, and what arrives is appended on the UI thread. The end of input finishes the status line.

The tests (`tests/stream_test.cpp`) cover:
- the demo played on the virtual clock: partial on the way, the whole answer at the end;
- motion that starts exactly where the block ended, or on all of a new block, and none with `--no-fade`;
- the parse cost of tiny pieces;
- the finished answer's pixels, pinned.
