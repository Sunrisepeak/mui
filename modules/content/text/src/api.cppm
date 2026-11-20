// openmui.text — a text engine.
//
//   :font       font files, variable instances, fallback per cluster
//   :paragraph  shaping (HarfBuzz), bidi (SheenBidi), line breaking (UAX #14),
//               alignment, ellipsis, hit testing, carets, selections
//   :edit       the editing model of a text field: graphemes, words, IME,
//               undo
//
// Text lays out the same on every platform with the default fonts (D-48):
// positions come from HarfBuzz in font units. Glyphs leave as geometry
// (FontCollection::outline, color_layers); xrender.text rasterises and draws
// them, so text knows no renderer.
export module mlibs.text.api;

export import :font;
export import :paragraph;
export import :edit;
