# xrender.text

Text as the renderer draws it. [mlibs.text](../../../content/text) shapes and lays out text and gives glyphs as geometry (`FontCollection::outline`, `color_layers`); it knows no renderer (convergence plan §5.3). This package is the renderer's side:

- **`GlyphCache`** — the renderer's `GlyphSource` over a `FontCollection`: outlines rasterised by [xrender.raster](../raster) per size and subpixel phase (quarter pixels), colour glyphs (COLR v0) composed from the font's palette into premultiplied images, so text pixels are as deterministic as any other shape.
- **`paint(paragraph, list, origin)`** — a laid-out paragraph's glyph runs (id, position and provenance — byte, grapheme, word, line, D-68) and decorations into a [display list](../vector): one `DrawGlyphs` a run, its glyphs written in place into the list's arena, nothing else allocated.

```cpp
import xrender.text;

xrender::text::GlyphCache glyphs(fonts);
xrender::text::paint(paragraph, list, {10, 10});
cpu.render(list, surface, {nullptr, &glyphs});
```

Its layer is content (tools/lint's path table): it depends on text, which is above graphics. Tests pin the pixels of mixed scripts, emoji and per-glyph styles (the same hashes the tests had inside mlibs.text before the split).
