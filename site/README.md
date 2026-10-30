# openmui.site — openmui's website

The site shows openmui's thesis instead of describing it: **the interface is a semantic tree, and everything else is a rendering of it** (D-84 §6.6). It is an openmui program. On the Web it runs as wasm, and in a window it runs as a native program.

| Package | What it is |
|---|---|
| `openmui.site` (here) | The site as an application (`omui::site::app()`), its lens component, the static page and the pre-renderer (`omui-site`) |

## What is on it

- **Places, not pages.** The places are:
  - the home page;
  - the framework's documents (`docs/en`, `docs/zh`: features and practices), rendered by `openmui.ui.markdown`;
  - live works:
    - a timeline editor (`openmui.ui.timeline`) with its own undo history (S-12);
    - the document wall, S1 small: the site's documents in both languages and the benchmark's set, on a canvas of their own (select, drag, snap, minimap, group drag, named views);
    - example 01 (`examples/01-hello-im`), the shortest openmui program, running in a frame beside its own source. A person or an agent types into it and clicks it.
    - the fighting game's move list, a real frame-data station:
      - for each fighter, every move's startup, active and recovery frames, its advantage on hit and on block, and its damage;
      - every number is measured by the game itself (`gongfu.training::frame_data`);
      - the chosen move plays on the game's own training stage, with its boxes and frame meter. Drag its frames one at a time, on hit or on block, with the move's details in words beside it;
      - the static page carries every fighter's table for readers without JavaScript.

  Each place is a window on one infinite canvas (`openmui.ui.canvas`, S1's approach; `src/atlas.*`). A window is the size of the view, so a place is read at zoom 1, like a page.
  - **Overview** flies the camera out to show every place whole.
  - Choosing a place, from the side, from a link or by tapping its window, flies the camera out and in again.
  - Far away, a window is a texture of its page, drawn offscreen at the resolution the screen asks for. Near enough to read, it is live.
  - The camera's positions are the scene's named views, so an agent moves it as a person does (`camera_preset`).
  - The windows follow the view's size, through the same stage of the frame that watches the tree.
- **The lens.** One switch moves the page you are reading between four renderings of the same nodes:
  - **Pixels**: the page as drawn.
  - **Tree**: the semantic tree's wireframe over the pixels. Each node gets its box and role; nodes that an agent or a person can change are outlined in amber.
  - **Agent**: the JSON an agent receives, one node per line.
  - **Test**: the assertions a test would write.
- **An agent on this page.** The agent is scripted, since no model runs in the page, but every step it takes is a real `openmui.tree@0.1` request on the bus. Claude Code sends the same requests through `omui mcp`, and a page's script through `window.omui`. Because of that, openas decides each step (S-11):
  - Reading, switching places and editing the timeline (then undoing it) run.
  - Opening a link is external, so the agent asks the person first.
  - The person answers in the panel: once, for this visit (a grant openas records), or no.
  - The answer buttons are the person's own (S-10). When the agent tries to press them, it is refused whatever actor it names.
  - The panel shows each request and its records.
- **Links.** A document's link to another document moves to that place. Any other link is opened by the host: the system's browser on the desktop, a new tab on the Web. This goes through `Runtime::open_url`, which accepts only http, https and mailto.
- **The page's time.** The footer is a strip of the page's last 20 seconds, with what happened marked where it did: a place reached, a language, a lens. It runs on a second at a time, on frames the page draws anyway, so an idle page stays idle and the strip is drawn the same whenever it is drawn again.
  - Drag the playhead back, or let an agent set it (the strip is a slider on the bus), and the page is drawn as it was: the camera mid-flight, the letters of a language switch gathering, a lens fading in.
  - Play it forwards or backwards. **Live** returns to the present.
  - Animations are functions of time, so every moment can be drawn again. The runtime re-evaluates what it holds (D-60), and components that animate on their own are told the time (`retimed`): the canvas keeps its camera's past for this.
  - Only the drawing goes back; the tree stays as it is now.
- **Checked against CI, frame by frame.** **Check against CI** in the footer opens a panel that replays a fixed script offscreen in your browser: fly to Features, switch to Chinese, turn the lens to Tree, 5.9 seconds in all (`src/reference.*`).
  - It plays on a host of its own, at CI's size (1280×800), with only the program's own fonts (Inter, Noto Sans SC, JetBrains Mono), rastered in one band, every frame complete (`complete_frames`).
  - Ten frames a second are sampled; each frame's RGBA bytes are hashed (FNV-1a).
  - Drag to a frame and the panel draws it, with its hash here, CI's hash for it and whether they are the same.
  - CI's table is a data file, `reference/ci.frames.omui.json`, embedded in the program. `site_test` makes every frame and checks it against the table.
  - The replay runs in a stage of the frame after paint, not inside the slider's handler: there it would run inside the page's batch, its effects held back, and draw a stale frame.
- **The tree's hash.** The footer shows a hash of the home page's semantic tree as your browser computed it, next to the value CI pins in `tests/site_test.cpp`.
  - It covers ids, roles, names, values and lasting states.
  - It leaves out what follows the window: boxes, how far a view scrolls, and what is off screen. It also leaves out the pointer's hover and press, and motion under way.
  - So any window size gives the same hash.
  - A stage of the frame's pipeline reads the place again whenever its tree changes.
- **Both languages.** English and 中文. The node ids are the structure's, not the text's, so the tree keeps its ids when the language changes. On a switch, the labels' letters gather into the other language and the document's lines rise in, in reading order. This is glyph motion (D-68) applied to whole components, which do nothing for it. The tree is the other language's from the first frame, and at rest the pixels are those of the page opened in that language.
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

`.github/workflows/pages.yml` publishes `target/site` to GitHub Pages. `tools/ci/drive-page.mjs` loads the page in a real browser (headless Chromium, in real time) and can drive it through `window.omui`. That is how the packed site was checked: the same tree hash as CI at three window sizes, the overview's textures, the hash routes, and a flight started by a page agent.

## Tests

`tests/site_test.cpp`:
- every place, in both languages, one live at a time;
- the language switch: letters on their way, the tree already switched, at rest the very pixels of the page opened in Chinese;
- links: to another place, or out through the host (never a script);
- the agent panel: real requests, a person's answer once and for the visit, the S-10 refusal, a revoked grant asking again;
- the lens's four renderings of the same nodes;
- the home tree's hash, pinned, and the same in a narrower window;
- the timeline editor: an agent's edit followed by the tree, then undone;
- the page's time: an agent dragging it back to before a flight (the view exactly as it was), live again, played backwards to the strip's start and forwards to live;
- the works: example 01 typed into and clicked, the wall's canvas live with the site's documents; the move list's rows equal to what the game measures, a move picked by an agent, on block, shown at its first active frame;
- the pixels of home, the overview, the wireframe, the agent's JSON, the timeline, the wall, example 01, the move list, the agent asking, the Chinese page and the page's time dragged back into a language switch, pinned on every platform (each picture's own hash printed, so a platform that differs shows which);
- the reference run: every sampled frame pinned (`reference/ci.frames.omui.json`; when they differ the run's own table is written to `$TMPDIR/openmui-tests/site-reference/`, with three of its frames beside it), a frame played to again the same; the panel in the page: an agent picks frame 9, and the hash drawn here is a fresh replay's and CI's;
- the static HTML and its injection into dist-toy's page.

The tests run natively on every CI row and on wasm32 under node in the Web row, with the same pins.

Changing a document changes the pinned hashes. Look at the pictures (`$TMPDIR/openmui-tests/site/`), then update the pins.
