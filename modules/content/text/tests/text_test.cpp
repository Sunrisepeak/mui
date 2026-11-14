// The text engine: fallback, shaping, bidi, wrapping, alignment, ellipsis,
// carets and selections, editing; glyphs as geometry. Their pixels are
// xrender.text's (modules/projects/xrender/text/tests).
import std;
import mlibs.geom;
import mlibs.text;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace text = mlibs::text;
namespace geom = mlibs::geom;

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

UTest cases{"text", [] {

    "a paragraph's glyphs as placed outlines: which glyph, where from, its contours in ems"_test = [] {
        text::Paragraph p(fonts(), "A一 B", style(40));
        p.layout(std::numeric_limits<float>::infinity());
        const auto glyphs = text::outlines(p, fonts());
        expect(eq(glyphs.size(), std::size_t{4}));
        auto contours = [](const text::GlyphOutline& o) {
            return std::ranges::count(o.verbs, geom::PathVerb::move);
        };
        expect(eq(contours(glyphs[0].outline), std::ptrdiff_t{2}));   // A: the outline and its counter
        expect(eq(contours(glyphs[1].outline), std::ptrdiff_t{1}));   // 一: one stroke
        expect(glyphs[2].outline.verbs.empty()) << "a space has no outline";
        expect(eq(contours(glyphs[3].outline), std::ptrdiff_t{3}));   // B: the outline and two counters
        expect(glyphs[0].cluster == 0 && glyphs[1].cluster == 1 && glyphs[3].cluster == 5) << "clusters are byte offsets of the source";
        expect(glyphs[0].size == 40 && glyphs[1].origin.x > glyphs[0].origin.x && glyphs[3].origin.x > glyphs[1].origin.x) << "placed left to right at the size";
        expect(geom::inspect_path(glyphs[0].outline.view()).has_value()) << "a valid mlibs.geom path";
    };

    "the default fonts load, with fallback per cluster"_test = [] {
        auto& f = fonts();
        expect(f.face_count() == 5);
        auto inter = f.find_family("Inter");
        expect(inter.has_value() && f.face(*inter).variable);
        std::array<std::string, 1> fam{"Inter"};
        expect(f.select(fam, U"A") == *inter);
        expect(f.face(f.select(fam, U"中")).family == "Noto Sans SC");
        expect(f.face(f.select(fam, U"ب")).family == "Noto Sans Arabic");
        expect(f.face(f.select(fam, U"\U0001F600")).color) << "emoji take the colour face";
        expect(f.find_family("monospace") == f.find_family("JetBrains Mono"));
    };

    "a collection's identity is never reused, as its address may be: caches keyed by it cannot meet a successor"_test = [] {
        std::uint64_t first = 0;
        {
            mlibs::text::FontCollection a;
            first = a.id();
            expect(first != 0 && mlibs::text::FontCollection::alive(first));
        }
        expect(!mlibs::text::FontCollection::alive(first)) << "gone with it";
        std::set<std::uint64_t> ids{first};
        for (int i = 0; i < 64; ++i) {
            mlibs::text::FontCollection b;  // often where the last one was
            expect(ids.insert(b.id()).second) << "a new id each time";
        }
        mlibs::text::FontCollection c;
        mlibs::text::FontCollection moved = std::move(c);
        expect(mlibs::text::FontCollection::alive(moved.id())) << "a move keeps the identity";
    };

    "shaping: advances, ligatures and kerning come from the font"_test = [] {
        text::Paragraph p(fonts(), "Hello", style(20));
        expect(p.lines().size() == 1);
        float w = p.size().width;
        expect(w > 40 && w < 60) << std::format("Hello at 20px is {} wide", w);
        text::Paragraph two(fonts(), "HelloHello", style(20));
        expect(near(two.size().width, 2 * w, 0.5));
        expect(eq(p.max_intrinsic_width(), w));
    };

    "Chinese typography (CSS Text 4): autospace, punctuation set solid in pairs and at line starts, no 。 starting a line"_test = [] {
        auto width = [](std::string_view t, bool autospace = true, bool trim = true) {
            text::ParagraphStyle st = style(20);
            st.style.autospace = autospace;
            st.style.trim_punctuation = trim;
            text::Paragraph p(fonts(), std::string(t), st);
            return p.max_intrinsic_width();
        };
        // 1/8 em between ideographs and Latin letters or digits, on either side.
        expect(std::abs(width("中文Inter") - (width("中文") + width("Inter") + 20.0f / 8)) < 0.01f) << "ideograph, then letters";
        expect(std::abs(width("版本2") - (width("版本") + width("2") + 20.0f / 8)) < 0.01f) << "ideographs, then a digit";
        expect(std::abs(width("A中") - (width("A") + width("中") + 20.0f / 8)) < 0.01f) << "a letter, then an ideograph";
        expect(std::abs(width("中文Inter", false) - (width("中文") + width("Inter"))) < 0.01f) << "autospace off";
        expect(std::abs(width("中文，Inter") - (width("中文，") + width("Inter"))) < 0.01f) << "punctuation is neither";
        // A closing mark before another closing mark gives up its empty half: 。」 is 1.5 em, not 2.
        expect(std::abs(width("。」") - 30.0f) < 0.05f) << std::format("。」 is {}", width("。」"));
        expect(std::abs(width("。」", true, false) - 40.0f) < 0.05f) << "trimming off";
        // An opening mark after another opening mark: 「（ is 1.5 em.
        expect(std::abs(width("「（") - 30.0f) < 0.05f) << std::format("「（ is {}", width("「（"));
        // One mark alone keeps its full width.
        expect(std::abs(width("（") - 20.0f) < 0.05f);
        // A line after the first that starts with an opening mark sets it solid at the edge.
        text::Paragraph quote(fonts(), "一二三四五六七八九十（甲乙丙丁戊己）", style(20));
        quote.layout(200);  // ten ideographs fill the first line exactly
        expect(quote.lines().size() >= 2) << "it wraps";
        if (quote.lines().size() >= 2) {
            const auto& second = quote.lines()[1];
            expect(quote.text().substr(second.begin, 3) == "（") << "the second line starts with the opening mark";
            expect(!second.runs.empty() && !second.runs.front().glyphs.empty() && second.runs.front().glyphs.front().position.x < -9.0f) << "its empty half lies before the line's edge";
        }
        // No closing mark starts a line (UAX #14): 。 stays with the ideograph before it.
        text::Paragraph sentence(fonts(), "一二三四五六七八九十。甲乙丙丁戊", style(20));
        sentence.layout(200);
        for (const auto& line : sentence.lines()) expect(sentence.text().substr(line.begin, 3) != "。") << "no line starts with 。";
    };

    "wrapping at word boundaries, and inside an overlong word"_test = [] {
        text::Paragraph p(fonts(), "The quick brown fox jumps over the lazy dog", style(16));
        p.layout(120);
        expect(p.lines().size() > 2);
        for (const auto& line : p.lines()) expect(line.width <= 120.5f) << std::format("line width {}", line.width);
        // No line starts with a space: spaces hang at the end.
        for (const auto& line : p.lines()) expect(p.text()[line.begin] != ' ');
        text::Paragraph long_word(fonts(), "Supercalifragilisticexpialidocious", style(16));
        long_word.layout(60);
        expect(long_word.lines().size() > 3) << "an overflow break splits a word wider than the line";
        expect(p.min_intrinsic_width() < p.max_intrinsic_width());
    };

    "Chinese breaks between characters"_test = [] {
        text::Paragraph p(fonts(), "统一的语义层让像素无障碍和代理看到同一棵树", style(16));
        p.layout(100);
        expect(p.lines().size() >= 3);
        expect(near(p.min_intrinsic_width(), 16, 2));
    };

    "bidi: an Arabic paragraph is right to left"_test = [] {
        text::Paragraph p(fonts(), "مرحبا بالعالم", style(16));
        expect(p.lines().size() == 1);
        expect(p.lines()[0].rtl);
        // The first logical character is drawn at the right.
        auto first = p.caret(0), last = p.caret(p.text().size());
        expect(first.x > last.x) << std::format("caret at 0: {}, at end: {}", first.x, last.x);
        text::Paragraph mixed(fonts(), "abc مرحبا def", style(16));
        expect(!mixed.lines()[0].rtl);
        expect(mixed.lines()[0].runs.size() >= 3);
        // UBA P1: a line feed ends a paragraph, and the next one takes its
        // direction from its own first strong character — so the `!` that
        // opens an Arabic paragraph belongs to it and is drawn at its right.
        text::Paragraph two(fonts(), "abc\n!مرحبا", style(16));
        expect(two.lines().size() == 2);
        const std::size_t bang = 4;
        expect(two.caret(bang).x > two.caret(bang + 1).x) << std::format("`!` at {} drawn right of the Arabic at {}", two.caret(bang).x, two.caret(bang + 1).x);
        expect(two.caret(1).x > two.caret(0).x) << "the first paragraph stays left to right";
    };

    "alignment, justification, ellipsis"_test = [] {
        auto st = style(16);
        st.align = text::TextAlign::center;
        text::Paragraph c(fonts(), "centre", st);
        c.layout(200);
        expect(near(c.lines()[0].left, (200 - c.lines()[0].width) / 2, 0.01));
        st.align = text::TextAlign::justify;
        text::Paragraph j(fonts(), "one two three four five six seven eight nine ten", st);
        j.layout(150);
        expect(j.lines().size() > 1);
        const auto& first = j.lines()[0];
        float right = 0;
        for (const auto& r : first.runs)
            for (const auto& g : r.glyphs) right = std::max(right, g.position.x);
        expect(right > 130) << "a justified line reaches the right edge";
        st.align = text::TextAlign::start;
        st.max_lines = 2;
        text::Paragraph e(fonts(), "one two three four five six seven eight nine ten eleven twelve", st);
        e.layout(100);
        expect(e.lines().size() == 2 && e.truncated());
        expect(e.lines()[1].ellipsized);
        expect(e.lines()[1].width <= 100.5f);
    };

    "carets, hit testing, selections"_test = [] {
        text::Paragraph p(fonts(), "ab\ncd", style(20));
        expect(p.lines().size() == 2);
        auto c0 = p.caret(0), c3 = p.caret(3);
        expect(eq(c0.line, std::size_t{0}));
        expect(eq(c3.line, std::size_t{1}));
        expect(near(c3.x, 0, 0.01));
        expect(eq(p.offset_at({1000, 5}), std::size_t{2}), std::source_location::current());
        expect(eq(p.offset_at({0, p.lines()[1].top + 2}), std::size_t{3}));
        auto rects = p.selection(1, 4);
        expect(rects.size() == 2);
        expect(eq(p.offset_below(1, p.caret(1).x), std::size_t{4}));
        text::Paragraph emoji(fonts(), "a\U0001F469‍\U0001F4BBb", style(20));  // a 👩‍💻 b
        auto boxes = emoji.lines()[0].graphemes;
        expect(boxes.size() == 3) << std::format("{} graphemes", boxes.size());
        text::Paragraph empty(fonts(), "", style(20));
        expect(empty.lines().size() == 1 && empty.size().height > 0) << "an empty paragraph has a line for the caret";
    };

    "editing by grapheme and word, IME, undo"_test = [] {
        text::TextEditor ed("hello world");
        ed.move_to(5);
        ed.insert(",");
        expect(eq(ed.text(), std::string("hello, world")));
        ed.delete_backward(text::Unit::word);  // back to the start of the previous word
        expect(eq(ed.text(), std::string(" world")), std::source_location::current());
        expect(ed.undo());
        expect(eq(ed.text(), std::string("hello, world")));
        expect(ed.undo());
        expect(eq(ed.text(), std::string("hello world")));
        expect(ed.redo());
        expect(eq(ed.text(), std::string("hello, world")));
        text::TextEditor fam("x\U0001F468‍\U0001F469‍\U0001F467");
        fam.delete_backward();
        expect(eq(fam.text(), std::string("x")), std::source_location::current());
        text::TextEditor ime("ab");
        ime.move_to(1);
        ime.set_composition("ni");
        expect(eq(ime.display_text(), std::string("anib")));
        ime.commit_composition("你");
        expect(eq(ime.text(), std::string("a你b")));
        text::TextEditor limited("", {.max_length = 3});
        limited.insert("abcdef");
        expect(eq(limited.text(), std::string("abc")));
        expect(!limited.insert("x"));
        text::TextEditor single("a");
        single.insert("b\nc");
        expect(eq(single.text(), std::string("ab c")));
        text::TextEditor typing("");
        for (char c : std::string("abc")) typing.insert(std::string(1, c));
        expect(typing.undo());
        expect(eq(typing.text(), std::string("")), std::source_location::current());
    };

    "a length-limited editor can replace its selection with empty text and undo it"_test = [] {
        for (const std::string original : {std::string("abc"), std::string("你好吗"), std::string("a👩‍💻b")}) {
            text::TextEditor editor(original, {.max_length = 3}); editor.select_all();
            expect(editor.insert("")); expect(eq(editor.text(), std::string()));
            expect(editor.undo()); expect(eq(editor.text(), original));
            expect(editor.redo()); expect(eq(editor.text(), std::string()));
            expect(editor.insert("")); expect(eq(editor.text(), std::string()));
        }
    };

    "every glyph knows where it came from: byte, grapheme, word, line (D-68)"_test = [] {
        const std::string t = "Hello, world\nnext line";
        text::Paragraph p(fonts(), t, style());
        p.layout(1000);
        std::vector<text::Glyph> all;
        for (const auto& l : p.lines())
            for (const auto& r : l.runs) all.insert(all.end(), r.glyphs.begin(), r.glyphs.end());
        auto at = [&](std::size_t byte) {
            for (const auto& g : all)
                if (g.cluster == byte) return g;
            return text::Glyph{};
        };
        expect(eq(at(0).grapheme, std::uint32_t{0}));
        expect(at(7).grapheme == std::uint32_t{7}) << "one grapheme per ASCII byte";
        expect(at(7).word == at(11).word && at(0).word != at(7).word) << "'world' is one word, 'Hello' another";
        expect(at(0).line == 0 && at(13).line == 1 && at(t.size() - 1).line == 1) << "lines";
        expect(at(13).word > at(11).word);
    };

    "a collection without fonts shapes and lays out nothing instead of failing"_test = [] {
        text::FontCollection none;
        expect(none.add_defaults("/no/fonts/here").has_value() == false);
        text::Paragraph p(none, "Hello 统一 مرحبا \U0001F680", style());
        p.layout(200);
        expect(p.size().width == 0) << std::format("width {}", p.size().width);
    };

    "glyphs leave as geometry: outlines in ems, colour glyphs as palette layers"_test = [] {
        text::Paragraph p(fonts(), "H \U0001F680", style());
        p.layout(1000);
        std::vector<std::pair<text::FontRef, std::uint32_t>> glyphs;
        for (const auto& l : p.lines())
            for (const auto& r : l.runs)
                for (const auto& g : r.glyphs) glyphs.push_back({r.font, g.id});
        expect(eq(glyphs.size(), std::size_t{3}));
        const auto h = fonts().outline(glyphs[0].first, glyphs[0].second);
        expect(!h.verbs.empty() && h.verbs.front() == geom::PathVerb::move) << "H has an outline that starts a contour";
        float top = 0, bottom = -1;
        for (const auto& pt : h.points) top = std::min(top, pt.y), bottom = std::max(bottom, pt.y);
        expect(top < -0.6f && top > -0.9f && bottom > -0.01f && bottom < 0.01f) << std::format("cap height in ems, y down: {} to {}", top, bottom);
        expect(fonts().outline(glyphs[1].first, glyphs[1].second).verbs.empty()) << "a space has no outline";
        expect(fonts().color_layers(glyphs[0].first, glyphs[0].second).empty()) << "H is not a colour glyph";
        const auto rocket = fonts().color_layers(glyphs[2].first, glyphs[2].second);
        expect(rocket.size() > 1) << std::format("the rocket is a colour glyph of {} layers", rocket.size());
        expect(std::ranges::all_of(rocket, [&](const text::ColorLayer& l) { return !fonts().outline(glyphs[2].first, l.glyph).verbs.empty(); })) << "each layer is a glyph with an outline";
    };

    "fonts load from a directory whose name is not ASCII (Windows: not through the code page)"_test = [] {
        const auto dir = scratch_dir("text") / std::filesystem::path(u8"中文 字体 目录");
        std::filesystem::create_directories(dir);
        for (const char* f : {"Inter.ttf", "NotoSansSC.ttf", "NotoSansArabic.ttf", "TwemojiMozilla.ttf", "JetBrainsMono.ttf"})
            std::filesystem::copy_file(text::FontCollection::default_directory() / f, dir / f, std::filesystem::copy_options::overwrite_existing);
        text::FontCollection c;
        auto r = c.add_defaults(dir);
        expect(r.has_value() && c.face_count() == 5) << (r ? "" : r.error());
        auto missing = c.add_file(dir / std::filesystem::path(u8"不存在.ttf"));
        expect(!missing && missing.error().contains("不存在.ttf")) << (missing ? "" : missing.error());
    };
}};
