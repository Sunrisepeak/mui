// mlibs.text:shaped — a paragraph after shaping, before line layout: plain data a shaper fills (its `shape`, the
// slot's contract, :shaper) and the layout unit reads. A shaper's units are the only writers outside this module.
export module mlibs.text.api:shaped;

import std;
import mlibs.geom;
import mlibs.unicode;
import :font;
import :paragraph;

export namespace mlibs::text {

struct ShapedGlyph {
    std::uint32_t id = 0;
    std::uint32_t cluster = 0;  // byte offset of the cluster's first character
    float         advance = 0;
    float         x_offset = 0, y_offset = 0;
};

struct ShapedRun {
    std::size_t              begin = 0, end = 0;
    std::uint8_t             level = 0;
    FontRef                  font;
    std::size_t              style = 0;  // index into Paragraph::Shaped::styles
    std::vector<ShapedGlyph> glyphs;     // visual order
    Metrics                  metrics;
};

struct Grapheme {
    std::size_t begin = 0, end = 0;
    std::uint32_t word = 0;        // the UAX #29 word segment it is in (spaces and punctuation are segments too)
    float       advance = 0;   // including letter spacing
    std::size_t run = 0;
    bool        space = false;    // white space (hangs at line ends, stretches when justified)
    bool        newline = false;
    float       gap_after = 0;        // autospace after it (in `advance`; not at a line's end)
    float       line_start_trim = 0;  // an opening mark's empty half, set solid when it starts a line after the first
};

struct Paragraph::Shaped {
    FontCollection*            fonts = nullptr;
    std::string                text;
    ParagraphStyle             paragraph;
    std::vector<TextStyle>     styles;  // [0] is the paragraph's, [i + 1] is ranges[i]'s
    std::vector<StyledRange>   ranges;
    std::uint8_t               base_level = 0;
    std::vector<ShapedRun>     runs;    // logical order
    std::vector<Grapheme>      graphemes;
    std::vector<std::uint8_t>  break_before;  // per grapheme: 0 none, 1 allowed, 2 mandatory
    ShapedRun                  ellipsis;      // "…" in the paragraph font
    float                      ellipsis_width = 0;
};

// The style a byte is in: 0 the paragraph's, i + 1 the range i's (a shaper's runs do not cross a style).
[[nodiscard]] std::size_t style_at(const Paragraph::Shaped& shaped, std::size_t byte) noexcept;

}  // namespace mlibs::text

namespace mlibs::text {

// Shapes `text`: bidi levels, the collection's shaper's runs, then words and line-break opportunities (shape.cpp).
void shape(Paragraph::Shaped& s);
// Each byte's bidi level (UBA, rule L1's paragraph part applied) into `level_at`; the paragraph's base level, 0 or 1
// (native/bidi.cpp, over SheenBidi).
std::uint8_t bidi_levels(std::string_view text, TextDirection direction, std::vector<std::uint8_t>& level_at);

}  // namespace mlibs::text
