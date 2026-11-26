# omui.ui.markdown

The Markdown component (module `omui.ui.markdown`, canvas-markdown §2): CommonMark and GitHub's extensions rendered as semantic nodes over openmui.markdown's document model. It is an official component package, not in the umbrella's defaults, and contract-first. `contract/markdown.contract.xdef.ts` is its interface; the base class, the builder, the stubs and the schema are generated from it when the package builds (`omui.build.contracts`).

```cpp
import omui.ui.markdown;

omui::markdown({.id = "answer", .origin = omui::MarkdownOrigin::remote, .source = reply,
                .on_link_activated = [](std::string href) { open_in_browser(href); }});
```

- **The semantic tree is the document**:
  - headings (their `level` and `anchor` are properties; `value` is the level too), paragraphs, `blockquote`, lists (`ordered`, `start`, `tight`) and items;
  - task checkboxes, named by their item;
  - `code` (its `language` a property) and its *Copy code* button;
  - tables with rows, column headers and cells (`align`); separators;
  - links (`href`, `title`) and images (`source`; named by their alt text);
  - a text's inline formats are `marks` over it (emphasis, strong, code, deletion; a link's or an image's names its child node), counted in code points (spec S-17);
  - a comment (`<!-- … -->` alone in a block) is a hidden node whose `comment` property holds it: nothing shows it, a writing of the document gives it back.

  What a node is beyond its text is in its `properties` (spec S-16), not borrowed from `description` or `value`.
- **The tree read back** (`omui.ui.markdown:read`, D-258): `document_of(nodes)` is the document a snapshot's nodes state, `markdown_of(nodes)` it written as Markdown. For this component's own subtree, parsing that Markdown gives the document it was given: held on two corpora and every page of the repository's `docs/` (36 of 36; a line's end inside a paragraph read as the space the tree has). The same tree written as HTML (`omui::semantic::static_html`) reads back through `from_html` to the same document (32 of 32 documents without embedded markup: markup from a source that is not the application's is written as text, spec S-3).

  `origin` marks every node: text from an LLM is `remote` all the way down.
- **HTML as the source** (`format: html`, D-141): the same component over a subset of HTML read into the same document (`mlibs::markdown::from_html`): the same semantic tree and the same pixels as the Markdown it was made from. Scripts, styles and handlers are not content; an append reads the whole source again; its task boxes are as written (`toggle_task` refuses).
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
