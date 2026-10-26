// The text engine: fallback, shaping, bidi, wrapping, alignment, ellipsis,
// carets and selections, editing; and pixels pinned for every platform.
import std;
import openmui.geom;
import openmui.raster;
import openmui.render;
import openmui.text;
import openmui.image;
import openmui.testing;

using namespace omui::testing;
namespace text = omui::text;
namespace render = omui::render;
namespace raster = omui::raster;
namespace geom = omui::geom;

namespace {

text::FontCollection& fonts() {
    static text::FontCollection f = [] {
        text::FontCollection c;
        auto r = c.add_defaults();
        if (!r) std::println("fonts: {}", r.error());
        return c;
    }();
    return f;
}

text::ParagraphStyle style(float size = 16) {
    text::ParagraphStyle p;
    p.style.size = size;
    return p;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"text"};

    s.test("the default fonts load, with fallback per cluster", [] {
        auto& f = fonts();
        check(f.face_count() == 5);
        auto inter = f.find_family("Inter");
        check(inter.has_value() && f.face(*inter).variable);
        std::array<std::string, 1> fam{"Inter"};
        check(f.select(fam, U"A") == *inter);
        check(f.face(f.select(fam, U"中")).family == "Noto Sans SC");
        check(f.face(f.select(fam, U"ب")).family == "Noto Sans Arabic");
        check(f.face(f.select(fam, U"\U0001F600")).color, "emoji take the colour face");
        check(f.find_family("monospace") == f.find_family("JetBrains Mono"));
    });

    s.test("shaping: advances, ligatures and kerning come from the font", [] {
        text::Paragraph p(fonts(), "Hello", style(20));
        check(p.lines().size() == 1);
        float w = p.size().width;
        check(w > 40 && w < 60, std::format("Hello at 20px is {} wide", w));
        text::Paragraph two(fonts(), "HelloHello", style(20));
        check_near(two.size().width, 2 * w, 0.5);
        check_eq(p.max_intrinsic_width(), w);
    });

    s.test("wrapping at word boundaries, and inside an overlong word", [] {
        text::Paragraph p(fonts(), "The quick brown fox jumps over the lazy dog", style(16));
        p.layout(120);
        check(p.lines().size() > 2);
        for (const auto& line : p.lines()) check(line.width <= 120.5f, std::format("line width {}", line.width));
        // No line starts with a space: spaces hang at the end.
        for (const auto& line : p.lines()) check(p.text()[line.begin] != ' ');
        text::Paragraph long_word(fonts(), "Supercalifragilisticexpialidocious", style(16));
        long_word.layout(60);
        check(long_word.lines().size() > 3, "an overflow break splits a word wider than the line");
        check(p.min_intrinsic_width() < p.max_intrinsic_width());
    });

    s.test("Chinese breaks between characters", [] {
        text::Paragraph p(fonts(), "统一的语义层让像素无障碍和代理看到同一棵树", style(16));
        p.layout(100);
        check(p.lines().size() >= 3);
        check_near(p.min_intrinsic_width(), 16, 2);
    });

    s.test("bidi: an Arabic paragraph is right to left", [] {
        text::Paragraph p(fonts(), "مرحبا بالعالم", style(16));
        check(p.lines().size() == 1);
        check(p.lines()[0].rtl);
        // The first logical character is drawn at the right.
        auto first = p.caret(0), last = p.caret(p.text().size());
        check(first.x > last.x, std::format("caret at 0: {}, at end: {}", first.x, last.x));
        text::Paragraph mixed(fonts(), "abc مرحبا def", style(16));
        check(!mixed.lines()[0].rtl);
        check(mixed.lines()[0].runs.size() >= 3);
    });

    s.test("alignment, justification, ellipsis", [] {
        auto st = style(16);
        st.align = text::TextAlign::center;
        text::Paragraph c(fonts(), "centre", st);
        c.layout(200);
        check_near(c.lines()[0].left, (200 - c.lines()[0].width) / 2, 0.01);
        st.align = text::TextAlign::justify;
        text::Paragraph j(fonts(), "one two three four five six seven eight nine ten", st);
        j.layout(150);
        check(j.lines().size() > 1);
        const auto& first = j.lines()[0];
        float right = 0;
        for (const auto& r : first.runs)
            for (const auto& g : r.glyphs) right = std::max(right, g.position.x);
        check(right > 130, "a justified line reaches the right edge");
        st.align = text::TextAlign::start;
        st.max_lines = 2;
        text::Paragraph e(fonts(), "one two three four five six seven eight nine ten eleven twelve", st);
        e.layout(100);
        check(e.lines().size() == 2 && e.truncated());
        check(e.lines()[1].ellipsized);
        check(e.lines()[1].width <= 100.5f);
    });

    s.test("carets, hit testing, selections", [] {
        text::Paragraph p(fonts(), "ab\ncd", style(20));
        check(p.lines().size() == 2);
        auto c0 = p.caret(0), c3 = p.caret(3);
        check_eq(c0.line, std::size_t{0});
        check_eq(c3.line, std::size_t{1});
        check_near(c3.x, 0, 0.01);
        check_eq(p.offset_at({1000, 5}), std::size_t{2}, std::source_location::current());
        check_eq(p.offset_at({0, p.lines()[1].top + 2}), std::size_t{3});
        auto rects = p.selection(1, 4);
        check(rects.size() == 2);
        check_eq(p.offset_below(1, p.caret(1).x), std::size_t{4});
        text::Paragraph emoji(fonts(), "a\U0001F469‍\U0001F4BBb", style(20));  // a 👩‍💻 b
        auto boxes = emoji.lines()[0].graphemes;
        check(boxes.size() == 3, std::format("{} graphemes", boxes.size()));
        text::Paragraph empty(fonts(), "", style(20));
        check(empty.lines().size() == 1 && empty.size().height > 0, "an empty paragraph has a line for the caret");
    });

    s.test("editing by grapheme and word, IME, undo", [] {
        text::TextEditor ed("hello world");
        ed.move_to(5);
        ed.insert(",");
        check_eq(ed.text(), std::string("hello, world"));
        ed.delete_backward(text::Unit::word);  // back to the start of the previous word
        check_eq(ed.text(), std::string(" world"), std::source_location::current());
        check(ed.undo());
        check_eq(ed.text(), std::string("hello, world"));
        check(ed.undo());
        check_eq(ed.text(), std::string("hello world"));
        check(ed.redo());
        check_eq(ed.text(), std::string("hello, world"));
        text::TextEditor fam("x\U0001F468‍\U0001F469‍\U0001F467");
        fam.delete_backward();
        check_eq(fam.text(), std::string("x"), std::source_location::current());
        text::TextEditor ime("ab");
        ime.move_to(1);
        ime.set_composition("ni");
        check_eq(ime.display_text(), std::string("anib"));
        ime.commit_composition("你");
        check_eq(ime.text(), std::string("a你b"));
        text::TextEditor limited("", {.max_length = 3});
        limited.insert("abcdef");
        check_eq(limited.text(), std::string("abc"));
        check(!limited.insert("x"));
        text::TextEditor single("a");
        single.insert("b\nc");
        check_eq(single.text(), std::string("ab c"));
        text::TextEditor typing("");
        for (char c : std::string("abc")) typing.insert(std::string(1, c));
        check(typing.undo());
        check_eq(typing.text(), std::string(""), std::source_location::current());
    });

    s.test("pixels: mixed scripts and emoji, pinned", [] {
        auto st = style(18);
        st.style.color = geom::Color::hex(0x0f172a);
        std::vector<text::StyledRange> ranges{{6, 13, {}}};
        ranges[0].style.size = 18;
        ranges[0].style.weight = 700;
        ranges[0].style.color = geom::Color::hex(0x4f46e5);
        text::Paragraph p(fonts(), "Hello openmui! 统一的语义层 مرحبا \U0001F680\U0001F3A8", st, ranges);
        p.layout(320);
        render::DisplayList dl;
        p.paint(dl, {10, 10});
        text::GlyphCache glyphs(fonts());
        raster::Surface surf(340, 30 + static_cast<int>(p.size().height));
        render::CpuRenderer cpu;
        auto report = cpu.render(dl, surf, {nullptr, &glyphs}, {.background = geom::Color::white()});
        check_eq(report.missing_glyphs, std::size_t{0});
        auto dir = scratch_dir("text");
        omui::image::View v{reinterpret_cast<const std::uint8_t*>(surf.pixels().data()), surf.width(), surf.height(),
                            surf.width() * 4};
        check(omui::image::save_png(dir / "mixed.png", v).has_value());
        std::println("    {:016x} ({})", surf.hash(), (dir / "mixed.png").generic_string());
        check_eq(std::format("{:016x}", surf.hash()), std::string("09abccfc87e4202b"));
    });

    return s.run(argc, argv);
}
