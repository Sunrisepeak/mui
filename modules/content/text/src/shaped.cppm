// openmui.text:shaped — a paragraph after shaping, before line layout. Plain
// data shared by the shaping unit (HarfBuzz, fribidi) and the layout unit;
// exported to no one.
module openmui.text:shaped;

import std;
import openmui.geom;
import openmui.render;
import openmui.unicode;
import :font;
import :paragraph;

namespace omui::typeset {

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

// Shapes `text` (implemented over HarfBuzz and fribidi in native/shape.cpp).
void shape(Paragraph::Shaped& s);

}  // namespace omui::typeset
