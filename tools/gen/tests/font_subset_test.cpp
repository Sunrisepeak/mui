// omui-gen's font subset: the page's Chinese font cut down to the characters
// a program draws, its glyph ids kept — smaller by far, whole as a file
// (its checksum), the same cut again the same bytes, and a character more a
// glyph more.
import std;
import openmui.gen.font_subset;
import openmui.testing;
import openmui.text;

using namespace omui::testing;
using namespace omui;

namespace {

std::vector<std::uint8_t> read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

std::uint32_t sum(std::span<const std::uint8_t> d) {
    std::uint32_t s = 0;
    for (std::size_t i = 0; i < d.size(); i += 4) {
        std::uint32_t w = 0;
        for (std::size_t k = 0; k < 4; ++k) w = w << 8 | (i + k < d.size() ? d[i + k] : 0);
        s += w;
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gen.font_subset"};
    // The repository's, found from this file (tools/gen/tests/…).
    const auto root = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path().parent_path().parent_path();
    const auto font = read(root / "modules/content/text/fonts/NotoSansSC.ttf");

    s.test("Noto Sans SC cut to two characters and the punctuation: a fraction of it, whole, the same cut the same bytes", [&] {
        check(font.size() > 1000000, "the font is there");
        std::set<char32_t> keep;
        gen::always_kept(keep);
        gen::characters_of("中文", keep);
        gen::FontSubsetReport r;
        auto cut = gen::subset_font(font, keep, &r);
        check(cut.has_value(), cut ? "" : cut.error());
        if (!cut) return;
        check(r.kept > 2 && r.kept < 1500, std::format("{} of {} glyphs kept", r.kept, r.glyphs));
        check(r.bytes_after * 4 < r.bytes_before, std::format("{} → {} bytes", r.bytes_before, r.bytes_after));
        check_eq(sum(*cut), 0xB1B0AFBAu);  // the file's checksum, as head says it must be
        {
            std::ofstream out(scratch_dir("font-subset") / "NotoSansSC.cut.ttf", std::ios::binary);  // to look at (fontTools, a font viewer)
            out.write(reinterpret_cast<const char*>(cut->data()), static_cast<std::streamsize>(cut->size()));
        }
        auto again = gen::subset_font(*cut, keep);
        check(again.has_value() && *again == *cut, "cut again: the same bytes");
        // A character more: its glyph more.
        std::set<char32_t> more = keep;
        more.insert(U'龙');
        auto bigger = gen::subset_font(font, more);
        check(bigger.has_value() && bigger->size() > cut->size(), "龙 adds its outline");
    });

    s.test("the characters of text: UTF-8 read, malformed bytes passed over", [] {
        std::set<char32_t> got;
        gen::characters_of("a中\xff文\xe4", got);
        check(got == std::set<char32_t>{U'a', U'中', U'文'});
    });

    s.test("initial coverage: HarfBuzz sees only the two characters; the later face supplies the rest", [&] {
        auto cut = gen::subset_font(font, {U'中', U'文'});
        check(cut.has_value(), cut ? "" : cut.error());
        if (!cut) return;
        typeset::FontCollection fonts;
        const auto initial = fonts.add_data(*cut), full = fonts.add_data(font);
        check(initial.has_value() && full.has_value());
        if (!initial || !full) return;
        check(fonts.covers(*initial, U'中') && fonts.covers(*initial, U'文'));
        check(!fonts.covers(*initial, U'龙') && !fonts.covers(*initial, U'A'), "the tiny face must not claim emptied glyphs");
        fonts.set_fallback({*initial, *full});
        const std::array<std::string, 1> family{"Noto Sans SC"};
        check(fonts.select(family, U"中文") == *initial);
        check(fonts.select(family, U"龙") == *full, "including an explicitly requested family, full coverage remains reachable");
        const auto a = fonts.metrics(fonts.font(*initial, 500, typeset::Slant::normal), 14);
        const auto b = fonts.metrics(fonts.font(*full, 500, typeset::Slant::normal), 14);
        check(a.ascent == b.ascent && a.descent == b.descent, "the same face keeps its metrics");
        auto twice = gen::subset_font(*cut, {U'中', U'文'});
        check(twice && *twice == *cut, "coverage and bytes are stable on a second cut");
    });

    return s.run(argc, argv);
}
