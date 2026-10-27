# openmui-ui-markdown

The Markdown component (module `openmui.ui.markdown`, canvas-markdown §2): CommonMark and GitHub's extensions rendered as semantic nodes over openmui-markdown's document model. It is an official component package, not in the umbrella's defaults, and contract-first. `contract/markdown.contract.omui.toml` is its interface; the base class, the builder, the stubs and the schema are generated from it when the package builds (`openmui.rules`).

```cpp
import openmui.ui.markdown;

omui::markdown({.id = "answer", .origin = omui::MarkdownOrigin::remote, .source = reply,
                .on_link_activated = [](std::string href) { open_in_browser(href); }});
```

- **The semantic tree is the document**:
  - headings (`value` is the level, `description` the anchor), paragraphs, `blockquote`, lists and items;
  - task checkboxes, named by their item;
  - `code` (with its language) and its *Copy code* button;
  - tables with rows, column headers and cells; separators;
  - links (`value` is the address) and images (named by their alt text).

  `origin` marks every node: text from an LLM is `remote` all the way down.
- **Links reach outside** (effect `external`, answer `yes`). An agent states the answer or gets `needs-answer`; a person's own click or Enter is the answer (spec S-2a). The document's `open_link` follows only addresses the document links to. A `#anchor` link also scrolls to its heading.
- **Tasks**: toggling (a click, the checkbox's `toggle`, the document's `toggle_task`) rewrites the mark in the source. A bound `source` signal learns it, and `task_toggled` reports it.
- **Streaming**: a new `source` that extends the current one, or the `append` action, re-parses only after the last stable block boundary. Blocks are keyed by their content, so an unchanged block keeps its node, shaped text and layout.
- **Tables** size their columns from content: each column's widest content is the flex basis of its cells and its longest word the minimum. Every row therefore lays out to the same widths, narrow tables keep their natural size, and wide ones shrink as CSS's automatic table layout does.
- **Agent actions**: `append`, `scroll_to_anchor`, `open_link`, `toggle_task`, `copy` (the document's text) and `find` (the blocks that contain a phrase, as node ids). Events: `link_activated`, `task_toggled`, `heading_visible`, `changed`.

The look comes from the theme's tokens (`title`, `heading`, `body`, `code`, `accent`, `surface_variant` …). Images with a source on disk render as image nodes, resolved against `base_url`. Other images show their alt text, with an `image` node over it.

Tests (`tests/markdown_view_test.cpp`):
- the tree's roles, names, values and actions, with no spec violations;
- links: the agent's answer versus a person's click;
- anchors and scrolling, tasks and the binding;
- streaming that keeps nodes, copy and find, remote origins;
- the pixels of a README, pinned for every platform.
