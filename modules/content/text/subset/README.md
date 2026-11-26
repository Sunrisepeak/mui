# mlibs.text.subset

A TrueType face cut down to the characters a product shows, as bytes in and bytes out (module `mlibs.text.subset`, the standard library alone).

**Label**: stable — the capability is omui-gen's font subsetter as it was, moved with its tests and its bytes unchanged (D-189: the label is the capability's).

**Why a package of its own**: its users are a build-stage tool (`omui-gen`'s Web packer) and a site's builder (`oweb`), neither of which should carry the text engine to cut a file. It reads and writes SFNT tables and nothing else: no shaper, no font collection, no renderer.

## What it does

| Function | What it gives |
|---|---|
| `subset_font(font, keep, report)` | the face with every glyph no character of `keep` reaches emptied — its outline in `glyf`, its variations in `gvar` — and not removed: glyph ids, metrics and the layout tables (GSUB, GPOS) stay as they are, so shaping the kept text gives the same glyphs at the same places. The Unicode cmap holds only the characters asked for, so another face can give the rest (a fallback chain is not told a glyph is there when its outline is gone). A composite glyph keeps its components. TrueType outlines (`glyf`, `loca`; variable faces' `gvar`) |
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

TrueType outlines only (CFF faces are refused); glyphs are emptied, not renumbered, so the tables indexed by glyph id keep their size (the layout tables of a large face remain); no WOFF2 container.
