# openmui.site — openmui's website

The site shows openmui's thesis instead of describing it: **the interface is a semantic tree, and everything else is a rendering of it** (D-84 §6.6). It is an openmui program. On the Web it runs as wasm, and in a window it runs as a native program.

| Package | What it is |
|---|---|
| `openmui.site` (here) | The site as an application (`omui::site::app()`), its components (the lens, the page's time, the frame meter), its theme, the static page and the pre-renderer (`omui-site`) |

## What is on it

- **The opening** (`src/hero.*`): one thing, rendered four ways. It shows the claim beside the mark (one node branching into a pixel, a focus ring, braces and a check). Below them is a real checklist: three checks, then "Ship 0.1", which waits for them. Beside the checklist is the same node, the one last touched, three more ways, read from the page's semantic tree:
  - as a screen reader says it;
  - as an agent asks for it;
  - as a test asserts it.

  Lines in the lens's colours join them. "Let an agent ship it" performs the same actions a person's clicks reach, as an agent, through the runtime's checks; each one is journaled and undoable. The static page carries the same opening for readers without JavaScript.
- **The home page's screens** (`src/screens.*`), after the opening, one strength each: *your agent works here, as you do* (the prompt, a simulated agent, a live actor log of what agents did on the page); *every moment, drawn again* (the page's time replayed, this page's tree hash beside CI's, the pixel check); *one motion for every change* (a list becomes three cards — one box, two drawings, its morph held to the page's scroll, the scroll its time); *fast on every device* (the device's tier and who draws, another tier chosen as the settings choose it, three switches whose motions fall back where the tier cannot afford them). The hash shown and the device's facts are live readouts the tree's hash leaves out.
- **Places.** The bar names them, and the one shown is `selected` in its semantics (a screen reader says which; an agent reads it):
  - **Home**: the opening above, then the features document (`docs/en`, `docs/zh`, rendered by `openmui.ui.markdown`).
  - **Components** (`src/gallery.*`): every component of `openmui.ui.base`, live, with its contract's words (its role, its actions, what it is) and its code in C++, TypeScript, Python or Lua. A style pack (plain, glass, neon, retro, ink, liquid) styles the gallery alone: it is a theme scope (D-89), and the tree is the same in every pack.
  - **Features** and **Practices**: the framework's documents.
  - **Get started**: example 01 (`examples/01-hello-im`), the shortest openmui program, running beside its own source. A person or an agent types into it and clicks it.
  - **Timeline editor** (`openmui.ui.timeline`): a real one with its own undo history (S-12).

  A place is a page that scrolls, at most 1120 px wide and centred. The window's width falls into one of four bands (under 700, 1100, 1400 px, and wider), and a place is built again only when the band changes. On a phone the bar wraps, putting the places on a line of their own, and a panel takes the place's room while it is open.
- **The site's own design language** (`theme/site.theme.omui.toml`, `.agents/docs/2026-11-01-openmui-site-design-language.md`): warm white (or night), one indigo accent, Inter at a 64 px display size. These are the theme's tokens over the framework's light and dark themes, so every component takes them as it is. The theme sets only tokens the spec defines; a name it adds is an error, and the site would fall back to the default theme (a test holds the site's own background).
- **Settings**, each choice the framework leaves to its users: light or dark (a crossfade), motion full, reduced or off, how things move (each control's own, simple, liquid or ink, D-90's motion styles), the effects tier, and who draws.
- **Who draws, and how fast.** The page is drawn by the machine's GPU where it has one (WebGPU, else WebGL 2; a software adapter counts as none), else by the CPU. The CPU's pixels stay the reference: the check against CI draws its frames with the CPU whoever draws the page. The bar's **frame meter** (`frame_meter`, `src/frame_meter.*`) shows:
  - who draws: CPU, WebGPU or WebGL 2;
  - the frame rate and a frame's median cost, with the last frames as bars against a 60 Hz display's budget;
  - **idle** once nothing moves: the page then draws nothing (D-70), and the meter asks for no frame of its own. It reads the frames the host reports (`Runtime::frame_shown`), and an agent reads the same numbers with its `reading` action.
- **The lens.** One switch moves the page you are reading between four renderings of the same nodes:
  - **Pixels**: the page as drawn.
  - **Tree**: the semantic tree's wireframe over the pixels. Each node gets its box and role; nodes that an agent or a person can change are outlined in amber.
  - **Agent**: the JSON an agent receives, one node per line.
  - **Test**: the assertions a test would write.
- **Your agent, on this page** (agent connect, D-95). The visitor's own agent — Claude Code, Codex, any that runs commands — operates the page by its semantic tree through a bridge on their machine; the page has no chat box, no model and no key of its own.
  - **Copy the prompt** starts the page's agent link (`Runtime::link_agent`, the Web host's): a pairing code, good for ten minutes, in a prompt the person pastes to their agent. It points the agent at the site's `agent.md`.
  - The **actor log** (`actor_log`, openmui.ui.base) says the link's state and lists each agent's actions as they come, newest last. The last one its history holds is taken back from there; hovering one outlines the node it acted on.
  - **Hello, agent** is the self-check `agent.md` gives: the agent presses it, and the count says so.
  - Above everything, a question an agent asks the person: an external action waits for the person's answer, once or for the visit (a grant openas records), or no. The answer buttons are the person's own (S-10): an agent pressing them is refused whatever actor it names.
  - **A simulated agent**, marked so: a scripted one, no model, whose every step is still a real `openmui.tree@0.1` request on the bus that openas decides (S-11) — reading, switching places, editing the timeline and undoing it, opening a link (it asks first). The panel shows each request and its records.
- **`agent.md`.** What an agent needs to operate the site, beside the page: how to link (the code, `omui connect` or a bridge written from the protocol it gives), the tools, the places its navigation offers, the self-check, and the rules. The program writes it (`omui-site --agent-guide target/site`), as every openmui application does, with its machine-readable twin `.well-known/openmui-agent.json` and the page's `<link rel="openmui-agent">`. Prebuilt `omui` for Linux, macOS and Windows comes from the omui workflow, with SHA256SUMS.
- **Links.** A document's link to another document moves to that place. Any other link is opened by the host: the system's browser on the desktop, a new tab on the Web. This goes through `Runtime::open_url`, which accepts only http, https and mailto.
- **The page's time.** The footer is a strip of the page's last 20 seconds, with what happened marked where it did: a place reached, a language, a lens. It runs on a second at a time, on frames the page draws anyway, so an idle page stays idle and the strip is drawn the same whenever it is drawn again.
  - Drag the playhead back, or let an agent set it (the strip is a slider on the bus), and the page is drawn as it was: a place's page arriving, the letters of a language switch gathering, a lens fading in.
  - Play it forwards or backwards. **Live** returns to the present.
  - Animations are functions of time, so every moment can be drawn again. The runtime re-evaluates what it holds (D-60), and components that animate on their own are told the time (`retimed`).
  - Only the drawing goes back; the tree stays as it is now.
- **Checked against CI, frame by frame.** **Check against CI** in the footer opens a panel that replays a fixed script offscreen in your browser: go to Features, switch to Chinese, turn the lens to Tree, 5.9 seconds in all (`src/reference.*`).
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
- **Routes.** Where the site is within itself is its route (`Runtime::set_route`): `/features`, `/zh/timeline`. On the Web the address's hash shows it, each move a step of the browser's history, and Back and Forward move the site. A link to `#/zh/timeline` opens that place in that language.

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

`.github/workflows/pages.yml` publishes `target/site` to GitHub Pages. `tools/ci/drive-page.mjs` loads the page in a real browser (headless Chromium, in real time) and can drive it through `window.omui`. That is how the packed site was checked: the same tree hash as CI at three window sizes, the routes, and places reached by a page agent.

## Delivery

`tools/ci/site-browser.sh` measures the built page in a real browser (`tools/ci/site-perf.mjs`), and the Pages workflow holds every build to `delivery.budget.omui.json`. It serves `target/site` as Pages does (gzip) and makes a cold visit at 1280×800 over a 20 Mbit/s, 40 ms connection. It then goes to the features and back through `window.omui`. Measured 2026-11-02 (headless Chromium 156):

| | |
|---|---|
| on the way to the first frame | 4.4 MB, all of it: the program (its wasm 2.4 MB compressed, its script 89 KB) and the fonts (2.0 MB) |
| first frame | 2.1 s, nearly all of it the transfer at 20 Mbit/s |
| `window.omui` answers | 2.1 s |
| frames while it moves | p50 16.7 ms, p95 16.7 ms, max 50 ms; 1 frame over 33 ms in 303 |
| memory at the end | JS heap 10 MB, wasm memory 28 MB |

The fonts are most of what is left: Noto Sans SC is there for any Chinese a visitor types.

The same script then reads the page as assistive technology does (`tools/ci/site-a11y.mjs`), from Chromium's own accessibility tree, which is what a screen reader on the page is given. It checks that:
- the places are a navigation landmark and the place shown is the main one;
- the claim is a level-1 heading, and every button and link has a name;
- the page states its language and allows zoom;
- the static page is hidden once the program runs;
- the first Tab is the skip link, seen while it has focus, and Enter takes both the page's focus and the program's to the main landmark.

## Tests

`tests/site_test.cpp`:
- every place, in both languages, one live at a time;
- the language switch: letters on their way, the tree already switched, at rest the very pixels of the page opened in Chinese;
- links: to another place, or out through the host (never a script);
- the agent panel: real requests, a person's answer once and for the visit, the S-10 refusal, a revoked grant asking again; your own agent's prompt with its code, its actions in the log and taken back from there, the self-check it presses;
- the lens's four renderings of the same nodes;
- the home tree's hash, pinned, and the same in a narrower window;
- the timeline editor: an agent's edit followed by the tree, then undone;
- the page's time: an agent dragging it back into a language switch (the view exactly as it was), live again, played backwards to the strip's start and forwards to live;
- the components: each live, its code in four languages, a style pack styling the gallery alone, the tree the same in every pack;
- example 01 typed into and clicked; the settings, each choice's name drawn and read by it and each changing what it says;
- on a phone: the bar wrapped, a panel in the place's place, the agent's tour putting it away;
- the pixels of home, the components, the wireframe, the agent's JSON, the timeline, getting started, the agent asking, the settings, the Chinese page and the page's time dragged back into a language switch, pinned on every platform (each picture's own hash printed, so a platform that differs shows which);
- the reference run: every sampled frame pinned (`reference/ci.frames.omui.json`; when they differ the run's own table is written to `$TMPDIR/openmui-tests/site-reference/`, with three of its frames beside it), a frame played to again the same; the panel in the page: an agent picks frame 9, and the hash drawn here is a fresh replay's and CI's;
- the static HTML and its injection into dist-toy's page.

The tests run natively on every CI row and on wasm32 under node in the Web row, with the same pins.

Changing a document changes the pinned hashes. Look at the pictures (`$TMPDIR/openmui-tests/site/`), then update the pins.
