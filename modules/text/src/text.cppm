// openmui.text — a text engine.
//
//   :font       font files, variable instances, fallback per cluster
//   :paragraph  shaping (HarfBuzz), bidi (fribidi), line breaking (UAX #14),
//               alignment, ellipsis, hit testing, carets, selections
//   :glyphs     glyph masks and colour glyphs for renderers (openmui.raster)
//   :edit       the editing model of a text field: graphemes, words, IME,
//               undo
//
// Text lays out and rasterises the same on every platform with the default
// fonts (D-48): positions come from HarfBuzz in font units, pixels from
// openmui.raster.
export module openmui.text;

export import :font;
export import :paragraph;
export import :glyphs;
export import :edit;
