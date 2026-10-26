# openmui-render

The rendering abstraction (module `openmui.render`, D-57): the display list of contract ⑤ (paths, paints, gradients, strokes, clips, transforms, layers with blend modes and effect parameters, glyph runs, images, external textures, node markers), resources by reference (an image store; glyphs from the text engine through `GlyphSource`), and backends. The CPU backend renders through openmui-raster, byte-for-byte the same on every platform, records pixel provenance, and reports what a CPU cannot draw (effects, external textures — spec R-3). Knows nothing of nodes or views.
