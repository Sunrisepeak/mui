# openmui-markdown

Markdown to a document of blocks and inline marks (module `openmui.markdown`): CommonMark 0.31 and GitHub's extensions, parsed by md4c through [`mcpplibs.mwrap.md4c`](https://github.com/mcpplibs/md4c-m). An independent component (D-55): it depends on the standard library, md4c and openmui-trace.

- **The document**: blocks nested as in the source (quotes, bullet and ordered lists, items with their task state and the task mark's byte in the source, headings with unique GitHub-style anchors, fenced and indented code with info and language, raw HTML, tables with alignment). Every leaf has its text, with entities decoded, and its inline structure as ordered open / close / break marks, so nesting survives (`*(*a*)*`). Links, images and wikilinks are in a side table. Every block has a hash of its content, equal wherever the block is, which a renderer can use as the key of a layout cache.
- **`runs`**: the marks flattened into styled stretches (`bit(Span::strong) | bit(Span::link)` …), which a text engine can lay out.
- **`to_html`**: the HTML md4c-html writes for the source, written from the document alone.
- **`plain_text`, `slug`, `walk`**: the document read as text (copy, find, accessible names) and walked.
- **`Stream`**: text that arrives in pieces. An append re-parses only what follows the last stable block boundary. A line scanner proposes each boundary, and md4c confirms it by parsing the segment followed by a sentinel line. Link reference definitions switch the stream to re-parsing the whole source.

```cpp
import openmui.markdown;
namespace md = omui::md;

auto doc = md::parse("# Notes\n\n- [x] parse\n- [ ] render\n");
doc.root.children[0].anchor;                 // "notes"
doc.root.children[1].children[0].task;       // Task::done

md::Stream answer;                           // an LLM's reply, token by token
answer.append("Here is **the plan**:\n\n");
answer.append("1. read\n2. ");
answer.document();                           // always equal to md::parse(answer.source())
```

## Tests

- `conformance_test`: every example of md4c 0.5.3's suites (`tests/data`: the CommonMark 0.31.2 spec, GitHub's extensions, md4c's regressions and coverage cases; 808 examples). For each one, `to_html(parse(md))` must equal `md_html(md)` byte for byte, and the HTML must match the expected output after the suites' own normalisation (`normalize.py`, ported). CommonMark: 652 of 652; extensions and the rest: 156 of 156.
- `stream_test`: every example appended in random pieces equals `parse()` after every piece. So do long documents made of 60 examples, and those freeze. An LLM-style answer re-parses about 40 bytes per 4-byte token.
- `markdown_test`: the document model.

Benchmarks (`tests/bench`, with budgets): `markdown/parse` takes about 10 ns per byte, `markdown/stream-token` under 1 µs per token, and `markdown/to-html` about 3 ns per byte.
