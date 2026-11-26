// A display list read into a terminal's cells: rectangles, boxes, rules, glyphs as graphemes (wide ones two cells),
// clips and translations in whole cells, layer opacity mixed in; the frame's text baseline; ANSI output that writes
// only what changed, inside synchronized output.
import std;
import mrender.cells;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace cells = mrender::cells;
using mrender::Color;
using mrender::Paint;
using mrender::Rect;

namespace {

// Glyphs as the cell measure lays them out: a glyph per code point, a grapheme's glyphs one cluster at its pen
// position, a column a narrow grapheme's advance.
void text(mrender::DisplayList& list, std::u32string_view graphemes, float x, float baseline, Color colour, const cells::Grid& grid = {}) {
    std::vector<mrender::Glyph> glyphs;
    float pen = 0;
    std::uint32_t cluster = 0;
    for (const char32_t point : graphemes) {
        glyphs.push_back({.id = static_cast<std::uint32_t>(point), .position = {pen, 0}, .cluster = cluster});
        const bool wide = point >= 0x4E00 && point <= 0x9FFF;
        pen += (wide ? 2 : 1) * grid.column;
        ++cluster;
    }
    list.draw_glyphs({1}, 14, {x, baseline}, glyphs.size(), [&](std::span<mrender::Glyph> out) { std::ranges::copy(glyphs, out.begin()); },
                     Paint::solid(colour));
}

}  // namespace

UTest cases{"cells", [] {

    "glyphs land in the cells their pen names; a wide grapheme takes two; the text is the baseline"_test = [] {
        mrender::DisplayList list;
        text(list, U"Hi 中文", 8, 12 + 16, Color::black());
        const cells::Frame frame = cells::render(list, 10, 3);
        expect(eq(frame.text(), std::string("\n Hi 中文\n"))) << frame.text();
        expect(frame.at(4, 1).text == "中" && frame.at(5, 1).text.empty() && frame.at(6, 1).text == "文");
    };

    "a fill takes the cells whose centres it covers, and an opaque one covers the text under it"_test = [] {
        mrender::DisplayList list;
        text(list, U"under", 0, 12, Color::black());
        list.fill_rect({0, 0, 24, 16}, Paint::solid(Color::hex(0x2563eb)));
        const cells::Frame frame = cells::render(list, 8, 2);
        expect(eq(frame.text(), std::string("   er\n"))) << frame.text();
        expect(frame.at(0, 0).background == Color::hex(0x2563eb) && frame.at(3, 0).background == Color::white());
    };

    "a translucent layer mixes its colour into the cells under it, and leaves their text"_test = [] {
        mrender::DisplayList list;
        text(list, U"kept", 0, 12, Color::black());
        list.push_layer(0.5f);
        list.fill_rect({0, 0, 32, 16}, Paint::solid(Color::black()));
        list.pop_layer();
        const cells::Frame frame = cells::render(list, 4, 1);
        expect(eq(frame.text(), std::string("kept\n")));
        expect(std::abs(frame.at(0, 0).background.r - 0.5f) < 0.01f) << "half black over white";
    };

    "a rounded rectangle's stroke is a box; too low for one, brackets; a thin fill is a rule"_test = [] {
        mrender::DisplayList list;
        list.stroke_round_rect({0, 0, 48, 48}, {8, 8, 8, 8}, 1, Paint::solid(Color::black()));
        list.stroke_round_rect({56, 0, 32, 16}, {}, 1, Paint::solid(Color::black()));
        list.fill_rect({0, 56, 40, 2}, Paint::solid(Color::black()));
        const cells::Frame frame = cells::render(list, 12, 4);
        expect(eq(frame.text(), std::string("╭────╮ [  ]\n│    │\n╰────╯\n─────\n"))) << frame.text();
    };

    "clips and translations are taken to whole cells"_test = [] {
        mrender::DisplayList list;
        list.push_transform(mrender::Affine::translate(16, 16));
        list.push_clip({0, 0, 16, 16});
        text(list, U"clipped", 0, 12, Color::black());
        list.pop_clip();
        list.pop_transform();
        const cells::Frame frame = cells::render(list, 8, 2);
        expect(eq(frame.text(), std::string("\n  cl\n"))) << frame.text();
    };

    "two frames compare as values; ANSI writes only what changed, inside synchronized output"_test = [] {
        mrender::DisplayList first, second;
        text(first, U"count 1", 0, 12, Color::black());
        text(second, U"count 2", 0, 12, Color::black());
        const cells::Frame a = cells::render(first, 10, 2), b = cells::render(second, 10, 2);
        expect(a != b && a == cells::render(first, 10, 2));
        const std::string whole = cells::ansi(a), changed = cells::ansi(a, b);
        expect(whole.starts_with("\x1b[?2026h") && whole.ends_with("\x1b[?2026l"));
        expect(changed.contains("\x1b[1;7H") && changed.contains("2") && !changed.contains("count")) << "one cell, where it is";
        expect(cells::ansi(b, b) == "\x1b[?2026h\x1b[0m\x1b[?2026l") << "nothing changed: nothing but the brackets";
        expect(cells::ansi(a, cells::Colors::palette16).contains("\x1b[0;30;107m")) << "black on bright white from the 16 colours";
    };
}};
