// openmui.text — a text engine.
//
//   :font       font files, variable instances, fallback per cluster
//   :paragraph  shaping, bidi (SheenBidi), line breaking (UAX #14),
//               alignment, ellipsis, hit testing, carets, selections
//   :edit       the editing model of a text field: graphemes, words, IME,
//               undo
//   :shaper     the slot a shaper is installed at (HarfBuzz in its own
//               package, mlibs.text.harfbuzz; the cell measure of a terminal
//               here), and :shaped, the paragraph a shaper fills
//
// Text lays out the same on every platform with the default fonts (D-48):
// positions come from the shaper in font units. Glyphs leave as geometry
// (FontCollection::outline, color_layers); mrender.text rasterises and draws
// them, so text knows no renderer.
export module mlibs.text.api;

export import :font;
export import :paragraph;
export import :edit;
export import :shaped;
export import :shaper;
