# Example 08 — a Markdown viewer

`md-view` opens a Markdown file, or a folder's `index.md` / `README.md`, and is meant for reading documentation:

- **Contents beside the document.** The headings are a table of contents. The entry for the heading in view is marked as you read (the document's `heading_visible`), and choosing an entry scrolls to it (`scroll_to_anchor`).
- **Documents link to documents.** A relative link to a `.md` file opens it here, at its `#anchor` if it has one, and **Back** returns.
- **The web stays outside until asked.** A link to a web address is a link node whose activation reaches outside the application (effect `external`). An agent must state the answer (`yes`). A person's own click is that answer (spec S-2a), and it opens the system's browser.
- **Live.** The file's stamp is read twice a second, so a saved edit shows at once.
- Images resolve relative to the document. The look is the theme's (`--dark`).

```sh
mcpp run -p openmui.example.markdown-viewer -- docs/            # a folder
mcpp run -p openmui.example.markdown-viewer -- README.md --dark
mcpp run -p openmui.example.markdown-viewer -- README.md --serve   # then: omui agent snapshot …
```

As a showcase (D-63) it is the Markdown component at work: `openmui.ui.markdown` over `openmui.markdown`, with CommonMark and GitHub's extensions, a semantic tree an agent reads, and pixels pinned on every platform.

The tests (`tests/viewer_test.cpp`) run on the documents in `tests/data/docs`:
- the contents, and scrolling to a heading from them;
- links between documents and Back, including a link with an anchor;
- a web link: the agent's answer, then a person's click;
- live reload after a save;
- the pixels.
