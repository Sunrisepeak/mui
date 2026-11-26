# mrender.cells

A display list as a terminal's character cells (module `mrender.cells`, terminal design §3 ①, D-249): another reader of [contract ⑤](../display) beside the CPU and GPU renderers of [mrender.vector](../vector). A frame is a value — `columns × rows` cells, each a grapheme, a foreground and a background colour, style bits and the node that drew it — so a terminal's picture is a text baseline a test compares (`Frame::text`) and two frames compare cell by cell. `ansi` writes a frame to a terminal, or only the cells that changed since the last, inside synchronized output (DEC mode 2026), in 24-bit colour or the 256 or 16 colours.

How each command lands: a fill takes the cells whose centres it covers (an opaque one covers the text under it, a translucent layer mixes into it); a rounded rectangle's stroke is a box of box-drawing characters (brackets where it is too low for one); a fill thinner than half a cell is a rule, an underline or strikethrough, a caret or a dot; paths fill the cells whose centres they cover and strokes mark the cells they pass; glyphs land in the cells their pen position names, a cluster's glyphs as one grapheme and a wide grapheme in two cells; clips and translations go to whole cells. Shadows, images and effects are not drawn.

A glyph's id is read as its code point: the glyphs of mlibs.text's cell measure (`mlibs::text::cells`), which lays text out in the same grid (`Grid`: a column, a row and the baseline in the list's units — the host gives both the same numbers). A list laid out with HarfBuzz's glyphs is not a terminal's.

**Why a package of its own**: a terminal program reads its lists with this and nothing else — it depends on the list's types ([mrender.display](../display)), mlibs.geom and mlibs.unicode, so no rasteriser, vector renderer or shaper comes with it (terminal design §4).

**Label**: preview — new with the terminal host; its API (the cell, the options) may still move. Verified headless (frames as values); real terminals as the host's README says.

The images a terminal's graphics protocols could show (kitty, iTerm2, sixel; terminal design §3 ②) are not in 0.1: a node marked for pixels is drawn as the cells under it.
