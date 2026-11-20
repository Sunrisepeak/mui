// xrender.text:paint — a laid-out paragraph into a display list.
export module xrender.text:paint;

import std;
import mlibs.geom;
import mlibs.text;
import xrender.vector;

export namespace xrender::text {

// The renderer's reference to a collection's font: the same id.
[[nodiscard]] constexpr FontRef font(mlibs::text::FontRef f) noexcept { return {f.id}; }

// Draws the paragraph's glyph runs (and their decorations) with its origin at
// `origin`: one DrawGlyphs a run, its glyphs written into the list as they are
// (id, position and provenance), nothing allocated beside the list's arena.
void paint(const mlibs::text::Paragraph& paragraph, DisplayList& list, mlibs::geom::Point origin);

}  // namespace xrender::text
