// mlibs.text.harfbuzz — the shaper over HarfBuzz (mlibs.text's slot `mlibs.text.shaper`; terminal design §9):
// font files read and described (OpenType tables, variable weights, colour), fonts at a weight and slant, their
// metrics, a paragraph's runs shaped with the whole text as context (script runs, features, language, CJK spacing),
// glyphs as outlines and colour layers. Where pixels are drawn its root installs it — omui::run, the headless host,
// a program or test of its own; a terminal installs the cell measure instead, and links none of this.
//
//     mlibs::text::harfbuzz::install();          // at the root, before fonts are made
//     mlibs::text::FontCollection fonts;         // over it
//     // or, without the process's slot: mlibs::text::FontCollection fonts{mlibs::text::harfbuzz::shaper()};
export module mlibs.text.harfbuzz;

import std;
export import mlibs.text;
import mlibs.compose;

export namespace mlibs::text::harfbuzz {

// The shaper's table, made when first asked for.
[[nodiscard]] const Shaper& shaper();
// Installs it at the process's `shaper_slot`; installed already: nothing changes. Refused: another shaper is there.
std::expected<void, compose::Error> install();

}  // namespace mlibs::text::harfbuzz
