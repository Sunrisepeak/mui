// openmui.text:paragraph — shaped, wrapped, aligned text.
//
// A paragraph is shaped once (bidi levels by SheenBidi, script runs, font
// fallback per grapheme cluster, HarfBuzz shaping) and laid out for any width
// (UAX #14 line breaks, overflow breaking inside words when a word is wider
// than the line, bidi reordering per line, alignment, justification,
// ellipsis). Positions come from HarfBuzz in font units, so the same text
// lays out to the same floats on every platform.
export module openmui.text:paragraph;

import std;
import openmui.geom;
import openmui.render.vector;
import :font;

export namespace omui::typeset {

using geom::Color;
using geom::Point;
using geom::Rect;
using geom::Size;

struct Feature {
    std::array<char, 4> tag{};  // "tnum", "liga", "ss01"
    std::uint32_t       value = 1;
    [[nodiscard]] static constexpr Feature on(std::string_view t, std::uint32_t v = 1) noexcept {
        Feature f;
        for (std::size_t i = 0; i < 4 && i < t.size(); ++i) f.tag[i] = t[i];
        f.value = v;
        return f;
    }
};

struct TextStyle {
    std::vector<std::string> families{"Inter"};  // tried in order, then the fallback chain
    float                    size = 14;
    float                    weight = 400;
    Slant                    slant = Slant::normal;
    Color                    color = Color::black();
    float                    letter_spacing = 0;  // pixels after each grapheme
    float                    line_height = 0;     // 0: the font's; < 4: a multiple of the size; else pixels
    std::vector<Feature>     features;
    std::string              language;            // BCP 47, e.g. "zh-Hans", "ar"
    bool                     underline = false;
    bool                     strikethrough = false;
    // CJK spacing (CSS Text 4, as browsers' defaults): 1/8 em between
    // ideographs and Latin letters or digits (`text-autospace: normal`); a
    // fullwidth punctuation mark's empty half set solid beside another mark,
    // and an opening mark's at the start of a line after the first
    // (`text-spacing-trim: normal`). Off for text that must keep its columns.
    bool                     autospace = true;
    bool                     trim_punctuation = true;
};

// A style over a byte range; ranges must not overlap.
struct StyledRange {
    std::size_t begin = 0, end = 0;
    TextStyle   style;
};

enum class TextAlign : std::uint8_t { start, end, left, right, center, justify };
enum class TextDirection : std::uint8_t { automatic, ltr, rtl };

struct ParagraphStyle {
    TextStyle     style;                // for bytes no range covers
    TextAlign     align = TextAlign::start;
    TextDirection direction = TextDirection::automatic;
    bool          wrap = true;
    int           max_lines = 0;       // 0: unlimited
    bool          ellipsis = true;     // with max_lines: end the last line with "…"
};

// A glyph run of one line: one font, one style, one direction.
struct GlyphRun {
    FontRef                    font;
    float                      size = 0;
    Color                      color;
    std::vector<render::Glyph> glyphs;  // positions relative to the paragraph origin (baseline)
    std::size_t                begin = 0, end = 0;  // bytes
    bool                       rtl = false;
    float                      x = 0, width = 0;
    float                      baseline = 0;
    bool                       underline = false, strikethrough = false;
    float                      decoration_offset = 0, decoration_thickness = 0;
};

// A grapheme's place on its line, in logical order.
struct GraphemeBox {
    std::size_t begin = 0, end = 0;
    float       x0 = 0, x1 = 0;
    bool        rtl = false;
};

struct Line {
    std::size_t              begin = 0, end = 0;  // bytes, including a trailing newline
    float                    top = 0, height = 0, baseline = 0;
    float                    ascent = 0, descent = 0;
    float                    left = 0, width = 0;  // the visible content (trailing spaces excluded)
    bool                     rtl = false;          // the paragraph direction
    bool                     ellipsized = false;
    std::vector<GlyphRun>    runs;
    std::vector<GraphemeBox> graphemes;
};

// A caret: where the insertion point is drawn.
struct Caret {
    float       x = 0, top = 0, height = 0;
    std::size_t line = 0;
};

class Paragraph {
public:
    Paragraph() = default;
    Paragraph(FontCollection& fonts, std::string text, ParagraphStyle style, std::vector<StyledRange> ranges = {});
    ~Paragraph();
    Paragraph(Paragraph&&) noexcept;
    Paragraph& operator=(Paragraph&&) noexcept;

    // Lays out the lines for a width (infinity: no wrapping).
    void layout(float width);

    [[nodiscard]] const std::string& text() const noexcept;
    [[nodiscard]] std::span<const Line> lines() const noexcept;
    [[nodiscard]] Size size() const noexcept;               // after layout
    [[nodiscard]] float min_intrinsic_width() const;        // the widest unbreakable piece
    [[nodiscard]] float max_intrinsic_width() const;        // the widest line without wrapping
    [[nodiscard]] float first_baseline() const noexcept;
    [[nodiscard]] bool truncated() const noexcept;          // lines were dropped by max_lines

    // Hit testing and carets (byte offsets on grapheme boundaries).
    [[nodiscard]] std::size_t offset_at(Point p) const;
    [[nodiscard]] Caret caret(std::size_t offset) const;
    [[nodiscard]] std::vector<Rect> selection(std::size_t begin, std::size_t end) const;
    [[nodiscard]] std::size_t line_of(std::size_t offset) const;
    // Vertical caret movement: the offset on the line above / below nearest to x.
    [[nodiscard]] std::size_t offset_above(std::size_t offset, float x) const;
    [[nodiscard]] std::size_t offset_below(std::size_t offset, float x) const;

    // Draws the glyph runs (and decorations) with the paragraph's origin at `origin`.
    void paint(render::DisplayList& list, Point origin) const;

    struct Shaped;  // the shaped text; defined by the implementation (openmui.text:shaped)

private:
    std::unique_ptr<Shaped> shaped_;
    std::vector<Line>       lines_;
    Size                    size_{};
    bool                    truncated_ = false;
};

}  // namespace omui::typeset
