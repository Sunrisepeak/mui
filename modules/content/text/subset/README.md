# mlibs.text.subset

A TrueType face cut down to the characters a product shows, as bytes in and bytes out (module `mlibs.text.subset`, the standard library alone).

**Label**: stable — the capability is omui-gen's font subsetter as it was, moved with its tests and its bytes unchanged (D-189: the label is the capability's).

**Why a package of its own**: its users are a build-stage tool (`omui-gen`'s Web packer) and a site's builder (`oweb`), neither of which should carry the text engine to cut a file. It reads and writes SFNT tables and nothing else: no shaper, no font collection, no renderer.

## What it does

| Function | What it gives |
|---|---|
| `subset_font(font, keep, report, features, move_axes)` | the face with every glyph no character of `keep` reaches emptied — its outline in `glyf`, its variations in `gvar` — and not removed: glyph ids, metrics and GSUB stay as they are, and GPOS is written again for the kept glyphs (below), so shaping the kept text gives the same glyphs at the same places. The Unicode cmap holds only the characters asked for, so another face can give the rest (a fallback chain is not told a glyph is there when its outline is gone). A composite glyph keeps its components. TrueType outlines (`glyf`, `loca`; variable faces' `gvar`) |
| `shaping_features()` | the layout features omui's text turns on: the shaper's own for a line of text (the common and horizontal ones, the direction's forms, a variable font's substitutions) and tabular figures (`tnum`). Given to `subset_font` as `features`, the cut keeps the glyphs of those features' lookups (and of the lookups those call) and no other feature's — Inter's `ss01`, `cv01…`, `zero`, `case`, `dlig`, `aalt` make hundreds of alternates a page's text never draws — and every other feature is left without lookups, so a style that asks for one gets the plain glyphs, never an emptied outline. The first frame's face (`omui-gen dist-toy --font-initial`, a site's page faces) is cut so: Inter's 141 KB (gzip) became 90 KB. A face fetched whole has every feature; an application that wants one on the first frame's face names it in the list it passes (no style of omui's has such a property yet). No features given: every feature, as before |
| `shaping_axes()` | the variation axes omui's text moves: the weight (`wght`); no style sets another. Given to `subset_font` as `move_axes`, a variable face's other axes (Inter's `opsz`) are held at their defaults: the tuples of its glyphs' variation data (`gvar`) whose region has a peak on such an axis count for nothing there, unless the region reaches across the default, and are dropped; a glyph at any weight keeps its outline point for point (the tests compare it with the whole face's at 100–900). Inter's `gvar` for a page's glyphs is about half of what it was. No axes given: every axis stays |
| `compact_font_post(font, report)` | the face without its stored PostScript glyph names (`post` 2 → 3): for consumers that address glyphs by id. Every other table's payload and the tables' order are kept; checksums are recalculated; a signed face, an unsupported `post` version or a broken directory is an error, not a guess |
| `font_blocks(font)` | what the face covers, coarsely: the 256-code-point blocks its cmap maps any character of, merged into ranges — what a page needs to know whether a face it deferred could draw a character |
| `characters_of(text, into)`, `always_kept(into)` | the characters of UTF-8 text; what a cut keeps whatever the text (ASCII, Latin-1, general and CJK punctuation, the full-width forms) |

The same input cut twice is the same bytes; a character more is a glyph more (`tests/subset_test.cpp`: Noto Sans SC and Inter cut, read back by `mlibs.text` with HarfBuzz — coverage, fallback to the full face, metrics, the same glyphs at the same places; the kept text's pixels are compared where a renderer is, `omui.gen`'s `font_delivery_test`).

## Entry points by level

- L0: `subset_font(bytes, {U'中', U'文'})`.
- L1: the characters from text (`characters_of`), with what is always kept (`always_kept`), and a report of glyphs and bytes before and after.
- L2: cut, then `compact_font_post` for delivery; `font_blocks` of the result for a page's fallback table (as `omui-gen dist-toy` does).
- L3, L4: none — it has no state to prepare and no extension point.

## Agent surface

A library of pure functions over bytes: nothing to observe or control at run time. Its results are facts an agent reads where they are used — `omui-gen dist-toy`'s report of each delivered face (bytes before and after, glyphs kept) and a built site's delivery manifest (`site.delivery.xdef.json`: each face's bytes as stored and as sent). Errors say which table is wrong and why.

## Limits

TrueType outlines only (CFF faces are refused); glyphs are emptied, not renumbered, so the tables indexed by glyph id keep their size (GSUB, `hmtx`, GDEF and HVAR of a large face remain); no WOFF2 container.

GPOS is the one layout table written again (2026-11-27, Web-program size design §14): single and pair adjustments (PairPos formats 1 and 2, with their variation device tables) and mark-to-base and mark-to-mark attachment keep what a kept glyph can reach — a pair set loses the pairs of glyphs not kept, a class matrix the classes none is in, a lookup no kept glyph can take goes — and the script and feature lists are copied as they are. Inter's cut for a page's text sent 49 KB of GPOS gzip-compressed whole; cut, 18.7 KB for every feature's glyphs and 9.6 KB for the shaping features' (below). Where a kept glyph can reach cursive, ligature-mark or contextual positioning, or the table has feature variations or a structure the reader does not understand, or an offset would not fit in 16 bits, the table stays as it was: a cut is never worse than the one before it, and cutting a cut again gives the same bytes.
