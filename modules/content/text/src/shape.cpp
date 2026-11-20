// Shaping, the part that is the same whatever shapes the glyphs: bidi levels (native/bidi.cpp, SheenBidi), then the
// runs and their glyphs from the shaper (native/shape.cpp: fonts and HarfBuzz), then each grapheme's word segment
// (UAX #29) and the line-break opportunities before it (UAX #14), from mlibs.unicode.
module mlibs.text.api;

import std;
import mlibs.unicode;
import :shaped;

namespace mlibs::text {

void shape(Paragraph::Shaped& s) {
    const std::string& text = s.text;
    std::vector<std::uint8_t> level_at;
    s.base_level = bidi_levels(text, s.paragraph.direction, level_at);

    shape_runs(s, level_at);

    // ── each grapheme's word segment (UAX #29) of the whole text ──
    {
        const std::vector<std::size_t> words = unicode::word_boundaries(text);
        std::uint32_t w = 0;
        std::size_t next = 1;
        for (Grapheme& g : s.graphemes) {
            while (next < words.size() && words[next] <= g.begin) {
                ++next;
                ++w;
            }
            g.word = w;
        }
    }

    // ── break opportunities before each grapheme ──
    s.break_before.assign(s.graphemes.size(), 0);
    for (const unicode::LineBreak& b : unicode::line_breaks(text)) {
        if (b.offset >= text.size()) continue;
        auto it = std::ranges::lower_bound(s.graphemes, b.offset, {}, &Grapheme::begin);
        if (it != s.graphemes.end() && it->begin == b.offset)
            s.break_before[static_cast<std::size_t>(it - s.graphemes.begin())] = b.mandatory ? 2 : 1;
    }
}

}  // namespace mlibs::text
