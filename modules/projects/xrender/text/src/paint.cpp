module xrender.text;

import std;
import mlibs.geom;
import mlibs.text;
import xrender.vector;

namespace xrender::text {

void paint(const mlibs::text::Paragraph& paragraph, DisplayList& list, mlibs::geom::Point origin) {
    for (const mlibs::text::Line& l : paragraph.lines()) {
        for (const mlibs::text::GlyphRun& r : l.runs) {
            if (r.glyphs.empty()) continue;
            list.draw_glyphs(font(r.font), r.size, origin, r.glyphs.size(), [&](std::span<Glyph> out) {
                for (std::size_t i = 0; i < out.size(); ++i) {
                    const mlibs::text::Glyph& g = r.glyphs[i];
                    out[i] = {g.id, g.position, g.cluster, g.grapheme, g.word, g.line};
                }
            }, Paint::solid(r.color));
            if (r.underline)
                list.fill_rect({origin.x + r.x, origin.y + r.baseline + r.decoration_offset, r.width, r.decoration_thickness},
                               Paint::solid(r.color));
            if (r.strikethrough)
                list.fill_rect({origin.x + r.x, origin.y + r.baseline - r.size * 0.3f, r.width, r.decoration_thickness},
                               Paint::solid(r.color));
        }
    }
}

}  // namespace xrender::text
