// Shaping, the part that is the same whatever shapes the glyphs: bidi levels (native/bidi.cpp, SheenBidi), then the
// runs and their glyphs from the collection's shaper (:shaper — HarfBuzz's, or the cell measure's), then each
// grapheme's word segment (UAX #29) and the line-break opportunities before it (UAX #14), from mlibs.unicode.
module mlibs.text.api;

import std;
import mlibs.unicode;
import :shaped;
import :shaper;

namespace mlibs::text {

std::size_t style_at(const Paragraph::Shaped& s, std::size_t byte) noexcept {
    for (std::size_t i = 0; i < s.ranges.size(); ++i)
        if (byte >= s.ranges[i].begin && byte < s.ranges[i].end) return i + 1;
    return 0;
}

namespace {

// A collection without a shaper (none was installed): its graphemes in one run with no glyphs, taking no room —
// text lays out and draws nothing instead of failing; the collection's add_defaults said how to install one.
void shape_nothing(Paragraph::Shaped& s, std::span<const std::uint8_t> level_at) {
    const std::vector<std::size_t> bounds = unicode::grapheme_boundaries(s.text);
    s.runs.clear();
    if (!s.text.empty()) s.runs.push_back({.begin = 0, .end = s.text.size(), .level = s.base_level});
    s.graphemes.clear();
    for (std::size_t i = 0; i + 1 < bounds.size(); ++i) {
        const char32_t first = unicode::decode(s.text, bounds[i]).cp;
        s.graphemes.push_back({.begin = bounds[i], .end = bounds[i + 1], .space = first == ' ' || first == '\t', .newline = first == '\n'});
    }
    (void)level_at;
    s.ellipsis = ShapedRun{.level = s.base_level};
    s.ellipsis_width = 0;
}

}  // namespace

void shape(Paragraph::Shaped& s) {
    const std::string& text = s.text;
    std::vector<std::uint8_t> level_at;
    s.base_level = bidi_levels(text, s.paragraph.direction, level_at);

    if (const Shaper* shaper = s.fonts ? s.fonts->shaper() : nullptr) shaper->shape(s, level_at);
    else shape_nothing(s, level_at);

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
