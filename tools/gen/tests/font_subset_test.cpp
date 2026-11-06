// omui-gen's font subset: the page's Chinese font cut down to the characters
// a program draws, its glyph ids kept — smaller by far, whole as a file
// (its checksum), the same cut again the same bytes, and a character more a
// glyph more.
import std;
import openmui.gen.font_subset;
import mlibs.testing;
import mlibs.text;
import xrender.text;

using namespace mlibs::testing;
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

using Bytes = std::vector<std::uint8_t>;

std::uint16_t word(std::span<const std::uint8_t> bytes, std::size_t at) {
    return static_cast<std::uint16_t>(bytes[at] << 8 | bytes[at + 1]);
}
std::uint32_t dword(std::span<const std::uint8_t> bytes, std::size_t at) {
    return std::uint32_t{word(bytes, at)} << 16 | word(bytes, at + 2);
}
void set_dword(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (24 - i * 8));
}
Bytes words(std::initializer_list<std::uint16_t> values) {
    Bytes out;
    for (auto value : values) { out.push_back(static_cast<std::uint8_t>(value >> 8)); out.push_back(static_cast<std::uint8_t>(value)); }
    return out;
}
std::size_t table_record(const Bytes& font, std::string_view name) {
    for (std::size_t i = 0; i < word(font, 4); ++i) {
        const auto at = 12 + i * 16;
        if (std::string_view(reinterpret_cast<const char*>(font.data() + at), 4) == name) return at;
    }
    return 0;
}
std::span<const std::uint8_t> table(const Bytes& font, std::string_view name) {
    const auto rec = table_record(font, name);
    return std::span(font).subspan(dword(font, rec + 8), dword(font, rec + 12));
}
Bytes outline(const Bytes& font, std::uint16_t glyph) {
    const auto loca = table(font, "loca"), glyf = table(font, "glyf"), head = table(font, "head");
    const bool long_offsets = word(head, 50) == 1;
    const auto offset = [&](std::size_t g) { return long_offsets ? dword(loca, g * 4) : std::uint32_t{word(loca, g * 2)} * 2; };
    const auto begin = offset(glyph), end = offset(std::size_t{glyph} + 1);
    return {glyf.begin() + begin, glyf.begin() + end};
}
Bytes with_gsub(Bytes font, std::span<const std::uint8_t> gsub) {
    const auto rec = table_record(font, "GSUB");
    while (font.size() % 4) font.push_back(0);
    set_dword(font, rec + 8, static_cast<std::uint32_t>(font.size()));
    set_dword(font, rec + 12, static_cast<std::uint32_t>(gsub.size()));
    font.insert(font.end(), gsub.begin(), gsub.end());
    return font;
}
Bytes gsub_table(const std::vector<std::pair<std::uint16_t, Bytes>>& lookups) {
    // Empty Script/Feature lists, then one subtable per lookup. The closure
    // intentionally scans even lookups that no feature currently selects.
    Bytes out = words({1, 0, 10, 12, 14, 0, 0, static_cast<std::uint16_t>(lookups.size())});
    out.resize(out.size() + lookups.size() * 2);
    for (std::size_t i = 0; i < lookups.size(); ++i) {
        const auto offset = static_cast<std::uint16_t>(out.size() - 14);
        out[16 + i * 2] = static_cast<std::uint8_t>(offset >> 8); out[17 + i * 2] = static_cast<std::uint8_t>(offset);
        auto lookup = words({lookups[i].first, 0, 1, 8});
        out.insert(out.end(), lookup.begin(), lookup.end());
        out.insert(out.end(), lookups[i].second.begin(), lookups[i].second.end());
    }
    return out;
}
std::uint16_t glyph_id(mlibs::text::FontCollection& fonts, std::string text) {
    mlibs::text::Paragraph p(fonts, std::move(text), {}); p.layout(512);
    return static_cast<std::uint16_t>(p.lines()[0].runs[0].glyphs[0].id);
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
        mlibs::text::FontCollection fonts;
        const auto initial = fonts.add_data(*cut), full = fonts.add_data(font);
        check(initial.has_value() && full.has_value());
        if (!initial || !full) return;
        check(fonts.covers(*initial, U'中') && fonts.covers(*initial, U'文'));
        check(!fonts.covers(*initial, U'龙') && !fonts.covers(*initial, U'A'), "the tiny face must not claim emptied glyphs");
        fonts.set_fallback({*initial, *full});
        const std::array<std::string, 1> family{"Noto Sans SC"};
        check(fonts.select(family, U"中文") == *initial);
        check(fonts.select(family, U"龙") == *full, "including an explicitly requested family, full coverage remains reachable");
        const auto a = fonts.metrics(fonts.font(*initial, 500, mlibs::text::Slant::normal), 14);
        const auto b = fonts.metrics(fonts.font(*full, 500, mlibs::text::Slant::normal), 14);
        check(a.ascent == b.ascent && a.descent == b.descent, "the same face keeps its metrics");
        auto twice = gen::subset_font(*cut, {U'中', U'文'});
        check(twice && *twice == *cut, "coverage and bytes are stable on a second cut");
    });

    s.test("a later full Inter face completes its explicit family before another family's fallback", [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        auto sparse = gen::subset_font(inter, {U'A', U' '});
        check(sparse.has_value(), sparse ? "" : sparse.error()); if (!sparse) return;
        mlibs::text::FontCollection fonts;
        const auto small = fonts.add_data(*sparse);
        check(small.has_value()); if (!small) return;
        const auto reference = fonts.font(*small, 500, mlibs::text::Slant::normal);
        const auto original_metrics = fonts.metrics(reference, 14);
        const auto other = fonts.add_data(font), full = fonts.add_data(inter);
        check(other.has_value() && full.has_value()); if (!other || !full) return;
        check(fonts.covers(*small, U'A') && !fonts.covers(*small, U'B') && fonts.covers(*full, U'B') && fonts.covers(*other, U'B'));
        // The fallback deliberately offers another family before either Inter
        // face, so merely finding the full font in the chain cannot pass.
        fonts.set_fallback({*other, *small, *full});
        for (const auto& name : {"Inter", "iNtEr", "sans-serif", "SYSTEM-UI", "UI"}) {
            const std::array<std::string, 1> family{name};
            check(fonts.select(family, U"A") == *small, "covered glyphs retain the original sparse FaceId");
            check(fonts.select(family, U"B") == *full, "a later face completes the requested family before fallback");
            check(fonts.select(family, U"AB") == *full, "the entire cluster must be covered by one face");
        }
        const std::array<std::string, 2> inter_first{"Inter", "Noto Sans SC"}, noto_first{"Noto Sans SC", "Inter"};
        check(fonts.select(inter_first, U"B") == *full && fonts.select(noto_first, U"B") == *other, "explicit family order remains authoritative");
        check(fonts.select(inter_first, U"中") == *other, "cross-family fallback still supplies an uncovered script");
        check(fonts.find_family("INTER") == small, "find_family retains the original face identity");
        check(fonts.face_of(reference) == *small && fonts.font(*small, 500, mlibs::text::Slant::normal) == reference, "previously shaped references are not replaced");
        const auto now = fonts.metrics(reference, 14);
        check(now.ascent == original_metrics.ascent && now.descent == original_metrics.descent);
        // The host groups a primary family's tiny and full faces before
        // later fallback families. With no explicit family, that ordering
        // must also select Inter for a non-ASCII glyph both full fonts have.
        check(!fonts.covers(*small, U'é') && fonts.covers(*full, U'é') && fonts.covers(*other, U'é'));
        fonts.set_fallback({*small, *full, *other});
        check(fonts.select({}, U"é") == *full && fonts.select({}, U"A") == *small, "the complete primary family precedes cross-family fallback");
    });


    s.test("initial Inter preserves default substitutions, positions and raster pixels at both UI weights", [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        std::set<char32_t> keep;
        const std::array<std::string, 9> samples{"Office", "Notification", "configuration", "One interface.", "Every perspective.", ":-)", "UI (CPU)", "A-B", "2x3"};
        for (const auto& sample : samples) gen::characters_of(sample, keep);
        auto cut = gen::subset_font(inter, keep);
        check(cut.has_value(), cut ? "" : cut.error()); if (!cut) return;
        mlibs::text::FontCollection full, small;
        check(full.add_data(inter).has_value() && small.add_data(*cut).has_value());
        xrender::text::GlyphCache full_cache(full), small_cache(small);
        bool saw_substitution = false;
        for (const float weight : {400.0f, 600.0f}) for (const auto& sample : samples) {
            mlibs::text::ParagraphStyle style; style.style.size = 28; style.style.weight = weight;
            mlibs::text::Paragraph a(full, sample, style), b(small, sample, style);
            a.layout(512); b.layout(512);
            check(a.lines().size() == b.lines().size());
            if (a.lines().size() != b.lines().size()) continue;
            for (std::size_t line = 0; line < a.lines().size(); ++line) {
                const auto& ar = a.lines()[line].runs; const auto& br = b.lines()[line].runs;
                check(ar.size() == br.size()); if (ar.size() != br.size()) continue;
                for (std::size_t run = 0; run < ar.size(); ++run) {
                    check(ar[run].glyphs.size() == br[run].glyphs.size());
                    if (ar[run].glyphs.size() != br[run].glyphs.size()) continue;
                    for (std::size_t i = 0; i < ar[run].glyphs.size(); ++i) {
                        const auto& ag = ar[run].glyphs[i]; const auto& bg = br[run].glyphs[i];
                        check(ag.id == bg.id && ag.position.x == bg.position.x && ag.position.y == bg.position.y,
                              std::format("{} at weight {} retains shaping", sample, weight));
                        const auto am = full_cache.mask(xrender::text::font(ar[run].font), ag.id, ar[run].size, {});
                        const auto bm = small_cache.mask(xrender::text::font(br[run].font), bg.id, br[run].size, {});
                        check(bool(am.coverage) == bool(bm.coverage));
                        if (am.coverage && bm.coverage) {
                            const auto ab = am.coverage->bounds, bb = bm.coverage->bounds;
                            check(ab.x == bb.x && ab.y == bb.y && ab.width == bb.width && ab.height == bb.height && am.coverage->values == bm.coverage->values,
                                  std::format("{} at weight {} retains actual glyph pixels", sample, weight));
                        }
                    }
                }
            }
            if (sample == ":-)") {
                style.style.features = {mlibs::text::Feature::on("calt", 0)};
                mlibs::text::Paragraph plain(full, sample, style); plain.layout(512);
                if (!a.lines().empty() && !plain.lines().empty()) {
                    const auto& shaped = a.lines()[0].runs[0].glyphs;
                    const auto& unshaped = plain.lines()[0].runs[0].glyphs;
                    for (std::size_t i = 0; i < std::min(shaped.size(), unshaped.size()); ++i)
                        saw_substitution |= shaped[i].id != unshaped[i].id;
                }
            }
        }
        check(saw_substitution, "the fixture exercises a real default calt glyph, not only cmap glyphs");
        auto twice = gen::subset_font(*cut, keep);
        check(twice && *twice == *cut, "substitution/composite closure is byte stable");
    });


    s.test("all GSUB output forms close transitively with composite glyphs without retaining unrelated outlines", [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        mlibs::text::FontCollection fonts; check(fonts.add_data(inter).has_value());
        const auto a = glyph_id(fonts, "A"), b = glyph_id(fonts, "B"), c = glyph_id(fonts, "C"), d = glyph_id(fonts, "D"),
                   e = glyph_id(fonts, "E"), f = glyph_id(fonts, "F"), g = glyph_id(fonts, "G"), h = glyph_id(fonts, "H"),
                   i = glyph_id(fonts, "I"), k = glyph_id(fonts, "K"), y = glyph_id(fonts, "Y"), z = glyph_id(fonts, "Z"), accented = glyph_id(fonts, "é");
        const auto composite = outline(inter, accented);
        check(!composite.empty() && static_cast<std::int16_t>(word(composite, 0)) < 0); if (composite.size() < 14) return;
        const auto component = word(composite, 12);
        const auto single = [](auto in, auto out) { return words({2, 8, 1, out, 1, 1, in}); };
        auto extension = words({1, 1, 0, 8}); const auto inner = single(i, accented);
        extension.insert(extension.end(), inner.begin(), inner.end());
        std::vector<std::pair<std::uint16_t, Bytes>> lookups{
            {1, words({1, 6, static_cast<std::uint16_t>(b - a), 1, 1, a})},
            {1, words({2, 8, 1, c, 2, 1, b, b, 0})},  // coverage format 2
            {2, words({1, 8, 1, 14, 1, 1, c, 2, d, e})},
            {3, words({1, 8, 1, 14, 1, 1, d, 2, f, g})},
            {4, words({1, 8, 1, 14, 1, 1, g, 1, 4, h, 2, e})},
            {8, words({1, 16, 1, 22, 1, 28, 1, i, 1, 1, h, 1, 1, z, 1, 1, z})},
            {7, extension},
            {1, words({1, 6, static_cast<std::uint16_t>(a - k), 1, 1, k})},  // negative delta and cycle
            {1, single(component, k)},  // only reached after the GSUB output's composite expansion
            {4, words({1, 8, 1, 14, 1, 1, a, 1, 4, y, 2, z})}, // missing input must not retain y
        };
        // Reverse lookup order so one forward scan cannot happen to pass.
        std::ranges::reverse(lookups);
        const auto source = with_gsub(inter, gsub_table(lookups));
        gen::FontSubsetReport report;
        const auto cut = gen::subset_font(source, {U'A'}, &report);
        check(cut.has_value(), cut ? "" : cut.error()); if (!cut) return;
        for (auto id : {a, b, c, d, e, f, g, h, i, k, accented, component}) {
            auto expected = outline(inter, id), actual = outline(*cut, id);
            // Subsetting pads glyf records to four bytes; compare all original
            // bytes and allow only zero alignment bytes after them.
            check(actual.size() >= expected.size() && std::equal(expected.begin(), expected.end(), actual.begin()) &&
                  std::ranges::all_of(std::span(actual).subspan(std::min(actual.size(), expected.size())), [](auto byte) { return byte == 0; }),
                  std::format("reachable glyph {} keeps its original outline", id));
        }
        check(outline(*cut, y).empty() && outline(*cut, z).empty(), "a ligature needs every input, and unrelated glyphs stay empty");
        check(report.kept < 30, "conservative lookup scanning is still a small subset");
        const auto twice = gen::subset_font(*cut, {U'A'});
        check(twice && *twice == *cut, "fixed-point closure is byte stable");
    });

    s.test("malformed and unsupported GSUB structures are rejected explicitly", [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        mlibs::text::FontCollection fonts; check(fonts.add_data(inter).has_value());
        const auto a = glyph_id(fonts, "A"), b = glyph_id(fonts, "B");
        const auto valid = gsub_table({{1, words({2, 8, 1, b, 1, 1, a})}});
        for (std::size_t size = 0; size < valid.size(); ++size) {
            const auto result = gen::subset_font(with_gsub(inter, std::span(valid).first(size)), {U'A'});
            check(!result && result.error().starts_with("GSUB:"), std::format("truncated GSUB of {} bytes is rejected", size));
        }
        for (const auto& bad : {
            gsub_table({{9, words({1, 0})}}),
            gsub_table({{1, words({3, 0})}}),
            gsub_table({{1, words({2, 65535, 1, b})}}),
            gsub_table({{1, words({2, 8, 1, 65535, 1, 1, a})}}),
            gsub_table({{1, words({2, 8, 1, b, 3, 1, a})}}),
            gsub_table({{7, words({1, 7, 0, 8, 1, 0})}}),
            gsub_table({{7, words({1, 1, 65535, 65535})}}),
            gsub_table({{5, words({4, 0})}}),
            gsub_table({{6, words({4, 0})}}),
        }) {
            const auto result = gen::subset_font(with_gsub(inter, bad), {U'A'});
            check(!result && result.error().starts_with("GSUB:"), "unsupported formats and invalid offsets/glyphs must fail explicitly");
        }
    });

    return s.run(argc, argv);
}
