# mlibs.text.harfbuzz

The shaper over HarfBuzz (module `mlibs.text.harfbuzz`; HarfBuzz as a C++ module, `mcpplibs.mwrap.harfbuzz`, built without FreeType): the provider of [mlibs.text](..)'s slot `mlibs.text.shaper` where pixels are drawn. It reads and describes font files (OpenType tables, the weight axis, colour), makes fonts at a weight and slant with their metrics, shapes a paragraph's runs with the whole text as context (script runs, features, language, CJK punctuation trimming and autospace), and gives glyphs as outlines and COLR colour layers.

**Why a package of its own**: it keeps HarfBuzz — a third-party C library — out of programs that measure text otherwise. A terminal installs mlibs.text's cell measure and does not depend on this, so its link has no HarfBuzz (terminal design §9; compose's rule: a provider is a package of its own when it has a dependency to keep apart).

**Who installs it**: a program's root, before fonts are made — `omui::run` (omui.sdk's feature `shaping`, in every world), the headless host (when the program installed none), or a program or test of its own (`mlibs::text::harfbuzz::install()`, or a collection over it without the process's slot: `FontCollection fonts{mlibs::text::harfbuzz::shaper()}`). Installed twice, nothing changes; where the cell measure is installed it is refused (the slot takes one). It depends on [`mlibs.compose`](../../../base/compose), a preview (`lower` in the manifest).

**Label**: stable — the capability is mlibs.text's shaping as it was, moved with its tests (`tests/text_test.cpp`, the cases with fonts) and pixels unchanged (D-189: the label is the capability's).

Inter (the default UI font) is still deployed by mlibs.text, whose `fonts/` holds it: a deploy cannot name a file outside its package (mcpp takes no `..`). Moving it here waits on Inter becoming a pack of its own, as the other scripts' fonts are (`mlibs.text.fonts.<pack>`, D-214); until then a terminal's distribution carries the file too.
