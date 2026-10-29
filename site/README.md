# openmui.site — openmui's website

The site shows openmui's thesis instead of describing it: **the interface is a semantic tree, and everything else is a rendering of it** (D-84 §6.6). It is an openmui program. On the Web it runs as wasm, and in a window it runs as a native program.

| Package | What it is |
|---|---|
| `openmui.site` (here) | The site as an application (`omui::site::app()`), its lens component, the static page and the pre-renderer (`omui-site`) |

## What is on it

- **Places, not pages.** The home page, the framework's documents (`docs/en`, `docs/zh`: features and practices) rendered by `openmui.ui.markdown`, and a live work: a timeline editor (`openmui.ui.timeline`) with its own undo history (S-12).
- **The lens.** One switch moves the page you are reading between four renderings of the same nodes:
  - **Pixels**: the page as drawn.
  - **Tree**: the semantic tree's wireframe over the pixels. Each node gets its box and role; nodes that an agent or a person can change are outlined in amber.
  - **Agent**: the JSON an agent receives, one node per line.
  - **Test**: the assertions a test would write.
- **The tree's hash.** The footer shows a hash of the home page's semantic tree as your browser computed it (ids, roles, names, values and states; not the boxes, which follow the window), next to the value CI pins in `tests/site_test.cpp`.
- **Both languages.** English and 中文. The node ids are the structure's, not the text's, so the tree keeps its ids when the language changes.
- **Hash routes.** `#/features` or `#/zh/timeline` open a place in a language. They are the program's flags (`--zh --timeline`), as dist-toy's page passes them.

## How it is built

```
mcpp run omui-site -p openmui.site               # in a window
tools/ci/site.sh                                 # for the Web, into target/site/
python3 -m http.server -d target/site            # and look at it
```

`tools/ci/site.sh` packs the site for wasm32-emscripten as dist-toy's static page, without Toy's SDK (`dist_toy({.sdk = false})`). It then runs the native program with `--prerender target/site/index.html`, which writes every place into the page as static HTML. The Markdown is rendered by `openmui.markdown`'s `to_html`, whose output is byte-for-byte what md4c-html writes. The static page:
- sits in a `<main id="omui-static">` that readers without JavaScript and search engines read;
- uses the program's own fonts;
- is hidden once the program runs.

`.github/workflows/pages.yml` publishes `target/site` to GitHub Pages.

## Tests

`tests/site_test.cpp`:
- every place, in both languages, one at a time;
- the lens's four renderings of the same nodes;
- the home tree's hash, pinned, and the same in a narrower window;
- the timeline editor: an agent's edit followed by the tree, then undone;
- the pixels of home, the wireframe, the agent's JSON, the timeline and the Chinese page, pinned on every platform;
- the static HTML and its injection into dist-toy's page.

Changing a document changes the pinned hashes. Look at the pictures (`$TMPDIR/openmui-tests/site/`), then update the pins.
