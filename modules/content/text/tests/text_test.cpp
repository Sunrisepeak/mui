// The text engine's own part, with no font file read: the cell measure (a
// terminal's grid), a collection without a shaper, and the editing model.
// Shaping with fonts is the HarfBuzz shaper's (../harfbuzz/tests).
import std;
import mlibs.compose;
import mlibs.text;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace text = mlibs::text;

namespace {

text::ParagraphStyle style(float size = 16) {
    text::ParagraphStyle p;
    p.style.size = size;
    return p;
}

}  // namespace

UTest cases{"text", [] {

    "without a shaper a collection has no faces, says how to install one, and lays text out to nothing"_test = [] {
        expect(mlibs::compose::process().get(text::shaper_slot) == nullptr) << "nothing installed yet in this program";
        text::FontCollection none;
        expect(none.shaper() == nullptr);
        const auto loaded = none.add_defaults();
        expect(!loaded && loaded.error().contains("mlibs.text.shaper") && loaded.error().contains("install()")) << (loaded ? "" : loaded.error());
        text::Paragraph p(none, "Hello 统一", style());
        p.layout(200);
        expect(p.size().width == 0 && p.lines().size() == 1) << std::format("width {}", p.size().width);
    };

    "the cell measure: a grapheme takes its cells, a line is a row, whatever the size; a glyph is its code point"_test = [] {
        text::FontCollection cells{text::cells::shaper()};
        expect(cells.add_defaults().has_value() && cells.face_count() == 1);
        const text::cells::Grid grid = text::cells::grid();
        text::Paragraph p(cells, "ab中é", style(40));
        p.layout(std::numeric_limits<float>::infinity());
        expect(eq(p.size().width, 5 * grid.column)) << "a, b one each, 中 two, e and its mark one";
        expect(eq(p.size().height, grid.row)) << "a line is a row, not the size's";
        expect(eq(p.first_baseline(), grid.baseline));
        const auto& run = p.lines()[0].runs[0];
        expect(eq(run.glyphs.size(), std::size_t{5}));
        expect(run.glyphs[0].id == U'a' && run.glyphs[2].id == U'中' && run.glyphs[3].id == U'e' && run.glyphs[4].id == 0x301);
        expect(eq(run.glyphs[3].position.x, run.glyphs[4].position.x)) << "a mark at its base's place";
        expect(eq(run.glyphs[3].grapheme, run.glyphs[4].grapheme)) << "one grapheme";
        expect(cells.outline(run.font, run.glyphs[0].id).verbs.empty()) << "no outlines in a terminal";
    };

    "the cell measure wraps in whole cells, and two lines are two rows"_test = [] {
        text::FontCollection cells{text::cells::shaper()};
        (void)cells.add_defaults();
        const text::cells::Grid grid = text::cells::grid();
        text::ParagraphStyle wrapped = style();
        wrapped.style.line_height = 1.6f;   // asked for, and the row's anyway
        wrapped.style.letter_spacing = 3;
        text::Paragraph p(cells, "hello world", wrapped);
        p.layout(7 * grid.column);
        expect(eq(p.lines().size(), std::size_t{2}));
        expect(eq(p.size().height, 2 * grid.row));
        expect(eq(p.lines()[1].top, grid.row));
        expect(eq(p.lines()[0].width, 5 * grid.column)) << "\"hello\" and no spacing between its cells";
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

    "installed at the slot, a collection made after measures in cells by the grid given"_test = [] {
        expect(text::cells::install({.column = 10, .row = 20, .baseline = 15}).has_value());
        expect(mlibs::compose::process().provider(text::shaper_slot.info) == "cells");
        text::FontCollection fonts;
        expect(fonts.add_defaults().has_value());
        text::Paragraph p(fonts, "ab", style());
        expect(eq(p.size().width, 20.0f) && eq(p.size().height, 20.0f));
    };
}};
