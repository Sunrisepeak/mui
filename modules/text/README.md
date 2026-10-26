# openmui-text

A text engine (module `openmui.text`) over HarfBuzz and fribidi (mcpp-index) and openmui-unicode, openmui-raster and openmui-render.

- **Fonts**: files and data, variable instances by weight (and italic), metrics; the default set in `fonts/` (D-48); fallback per grapheme cluster through the family list and a fallback chain, emoji presentation to the colour face.
- **Paragraphs**: bidi levels (fribidi), script runs, shaping (HarfBuzz) with features and language, UAX #14 line breaking with hanging spaces and overflow breaks inside overlong words, UBA L2 reordering per line, start / end / center / justify alignment, `max_lines` with an ellipsis, styled ranges (size, weight, colour, letter spacing, line height, underline, strikethrough), intrinsic widths for layout.
- **Queries**: hit testing, carets, selection rectangles, vertical caret movement — bidi-aware, by grapheme cluster.
- **Glyphs**: `GlyphCache` implements `render::GlyphSource`: outlines through HarfBuzz's draw API rasterised by openmui-raster at quarter-pixel phases, COLR colour glyphs from the palette.
- **Editing**: `TextEditor` — selection on grapheme boundaries, word movement and deletion (UAX #29), IME composition, undo / redo with typing coalescing, length and single-line constraints.

Text renders the same pixels on GCC, LLVM and the Windows target with the default fonts: the tests pin a mixed Latin / Chinese / Arabic / emoji paragraph.
