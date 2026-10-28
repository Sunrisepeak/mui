# Dev story — the Markdown wall

How `omui-wall` was made: every Markdown file under a folder laid out on the infinite canvas, a workspace per folder, to read and search across, with streaming windows and a scene script written in three languages.

![The Markdown wall](media/wall.png)

## 1. What we set out to make

A real reading tool: openmui's own design documents are read with it (`omui-wall .agents/docs`). It is also the canvas's proof at scale: a thousand live documents within the frame budget, sharp at every zoom, with agents able to find and act on any window, on screen or not.

## 2. The layers

| Layer | Package | What it is |
|---|---|---|
| Canvas | `openmui.ui.canvas` | a scene of nodes in world space, a camera, tiers (box, texture, live component), textures rasterised on the task pool, the semantic items of what is on screen and `find` across the rest |
| Canvas behaviours | `openmui.ui.canvas.kit` | select, drag, snap, resize, and declarative statecharts (`*.behavior.omui.toml`) |
| Markdown | `openmui.markdown`, `openmui.ui.markdown` | parsing (incremental: a stream parses only its tail), the document component |
| Scripts | `openmui.lang.{js,lua}`, the Python binding | the tour, one scene script in three languages |
| The application | `showcase/markdown-wall` | documents into workspaces, the window element, the HUD, the verbs |

## 3. How each part was built

- **One element per window, three tiers.** Under 40 px it is two instanced boxes; up to 900 px, a texture of the window's own element, painted by an offscreen runtime on the task pool at the mip level the screen asks for; beyond that, or when focused, the live element. The texture and the live window are the same element, so nothing moves when a window goes live.
- **Behaviours are data.** Dragging a workspace with every window on it, and the two views (1 and 2), are statecharts embedded at build time (`behaviors/*.behavior.omui.toml`).
- **Streaming** writes tokens into the nearest windows at a language model's pace. A live window appends and parses only the tail. A texture window is made stale at most four times a second and redrawn on the pool.
- **The tour** (`scripts/tour.{ts,lua,py}`) is one scene script written three times. TypeScript and Lua run inside the program on its script host; the Python one runs as an outside agent over the agent protocol. The test runs all three and requires the same records and the same wall.

## 4. Performance

The canvas's benchmarks hold a frame of panning or zooming 1,000 windows, or 100,000, under 1 ms of CPU (measured about 0.02 ms); textures are sharp again about 1 ms after the camera stops. The HUD shows the last frames against a 120 Hz frame, what was drawn by tier, and the texture jobs.

## 5. Self-review

- **The package name and the directory differed** (`wall` in `markdown-wall`), against D-80. Fixed with the style workshop's: the package is `openmui.showcase.markdown_wall`, and lint's `layout` now covers showcases.
- **The README picture is a still.** The wall's point is the zoom from the whole wall to a live window, which is a full-frame change on every frame. That is a video, not an animated PNG: the same finding as the style workshop's.
- **Three languages, one tour** worked because every language binding builds the same view documents and calls the same verbs. Writing the tour three times found binding gaps that a single language would have hidden, so new script-facing features should be tried in at least two languages.

## 6. Practices that generalise

See [best practices](../../docs/en/best-practices.md): one element for every tier of a node (no reflow when it goes live); heavy painting off the UI thread at the resolution the screen asks for; interaction as declarative behaviours where a statechart says it; script-facing features checked in more than one language.
