# omui's site, one page (`omui.site.page`)

omui's site as one page: the mark, one sentence, the Live Design key and the install, in four states of one picture (G5; [the site's design](../../.agents/docs/2026-11-19-site-live-design-design.md) §4).

The page is TypeScript (`app/main.ts`) run by the player, like any omui application. It is made by an agent in Live Design. When it lands, it replaces the C++ site under `../src`. **The art direction's sketch, reviewed (2026-11-20)**: the four states, each drawn offscreen at 1280×800 and 390×844; the five questions below decided.

| State | Desktop | Phone | What it says |
|---|---|---|---|
| At rest | ![At rest](media/sketch-rest-1280.png) | [390](media/sketch-rest-390.png) | The mark, the sentence, a lot of room. The install is the one accented action; Live Design is a quiet key under the mark. |
| Live Design | ![Live Design](media/sketch-design-1280.png) | [390](media/sketch-design-390.png) | The page is back to its sketch (grey) and grows again step by step. The mark's O is the course of the design's steps: its own stroke is drawn in ink as far as the design has come, the steps' marks and the playhead (step 3 of 8) on it. Under the key: the step, its maker and the source it wrote. |
| Waiting | ![Waiting](media/sketch-waiting-1280.png) | [390](media/sketch-waiting-390.png) | The prompt is copied ("Copied"). The O opens, with a fainter, wider O breathing around it. The pairing code is in the copied words only. |
| Connected | ![Connected](media/sketch-connected-1280.png) | [390](media/sketch-connected-390.png) | The O closes and becomes Mui: the O with ears is her outline, filled with her colour, her face drawn in it (head16's, every pixel a vector square). The visitor's agent and its omui appear beside it. The structure the agent changed is outlined and named for that moment. |

## How the sketches are drawn

An agent drives the page through the protocol, with the same operations and pixels as a person's window:

```sh
omui-player --manifest site/page --serve --size 1280x800 --agent-name sitepage
omui agent invoke sitepage page.state '{"state":"waiting"}'
omui agent wait sitepage --settled true
omui agent capture sitepage --out media/sketch-waiting-1280.png
```

`page.state` is the page's verb for its four states. Later, the Live Design key and the bridge (`omui connect`) drive them instead. `tests/page_test.cpp` does the same without a window: it sets each state through the verb, reads what each says in the semantic tree (the mark, the sentence, the keys, the design's course at its step, the agent's chip), and checks that each state is a different picture and that returning to rest gives the first picture again.

## Drawn with

- `path` (omui.ui.base) draws the mark's O (outline A of `../brand/marks.mark.xdef.json`: with ears), its word and Mui's face, scaled from their view boxes. The waiting O is the same path with `trim` 0.9; connected, the same path filled.
- `timeline` (omui.ui.base) lays the design's eight steps along the O (`shape`: the O in a 1×1 box; `line: false`: the O's own stroke is the course, the timeline adds the marks and the playhead).
- `segmented`, `button` and `text` come from the theme's defaults. The colours are the brand's (`#1e1b4b` ink, `#4f46e5` accent) on `#faf9f6` paper.

## Not in this round

| What | Where it comes from |
|---|---|
| Material and light (the mark's halo, the install's surface) | The framework's effects (liquid glass, a theme layer), not page code |
| The motion between states, including the O's breathing | The default recipes (`path`: `default.structure`); the sketches are at rest |
| The real Live Design replay | `generate.replay` (G8), as in example 13 |
| The bridge and pairing | `omui connect` (G9) |
| Chinese, static HTML, budgets | G2's projection; the site's budgets (§6) |

The page carries a copy of head16's rows from `../brand/mascot.sprite.xdef.json`, because this round does not read the brand's file from the script.

## Decided in review (2026-11-20)

1. **The O.** Outline A, the O with ears: connected, it becomes Mui's own outline (filled, her face in it) instead of holding her head.
2. **The sentence.** The README's, held to the claims ledger in G7.
3. **The Live Design key.** Under the mark: the O is its timeline.
4. **Live Design's course.** The O's own stroke is the played part; only the marks and the playhead are on it (`timeline`'s `line: false`).
5. **The palette.** Ink and indigo on paper; a dark variant later, from the theme's tokens, not the page.
