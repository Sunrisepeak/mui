// mlibs.text.subset: the page's Chinese font cut down to the characters
// a program draws, its glyph ids kept — smaller by far, whole as a file
// (its checksum), the same cut again the same bytes, and a character more a
// glyph more.
import std;
import mlibs.text.subset;
import mlibs.testing;
import mcpplibs.mtest;
import mlibs.text;
import mlibs.text.harfbuzz;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace subset = mlibs::text::subset;

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
Bytes with_table(Bytes font, std::string_view tag, std::span<const std::uint8_t> data) {
    const auto rec = table_record(font, tag);
    while (font.size() % 4) font.push_back(0);
    set_dword(font, rec + 8, static_cast<std::uint32_t>(font.size()));
    set_dword(font, rec + 12, static_cast<std::uint32_t>(data.size()));
    font.insert(font.end(), data.begin(), data.end());
    return font;
}
Bytes with_gsub(Bytes font, std::span<const std::uint8_t> gsub) { return with_table(std::move(font), "GSUB", gsub); }
// A GPOS of empty script and feature lists and one lookup of the given type, its subtable's words behind it.
Bytes gpos_table(std::uint16_t type, const Bytes& subtable) {
    Bytes out = words({1, 0, 10, 12, 14, 0, 0, 1, 4, type, 0, 1, 8});
    out.insert(out.end(), subtable.begin(), subtable.end());
    return out;
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

// The repository: the nearest directory above this file with the workspace's `.xlings.json` and `modules/`.
std::filesystem::path repository() {
    auto at = std::filesystem::path(std::source_location::current().file_name()).parent_path().parent_path();
    while (at.has_parent_path() && at != at.parent_path() && !(std::filesystem::exists(at / ".xlings.json") && std::filesystem::exists(at / "modules")))
        at = at.parent_path();
    return at;
}

}  // namespace

static UTest cases{"text.subset", [] {
    const auto root = repository();
    const auto font = read(root / "modules/content/text/fonts/han/NotoSansSC.ttf");

    "Noto Sans SC cut to two characters and the punctuation: a fraction of it, whole, the same cut the same bytes"_test = [&] {
        expect(font.size() > 1000000) << "the font is there";
        std::set<char32_t> keep;
        subset::always_kept(keep);
        subset::characters_of("中文", keep);
        subset::FontSubsetReport r;
        auto cut = subset::subset_font(font, keep, &r);
        expect(cut.has_value()) << (cut ? "" : cut.error());
        if (!cut) return;
        expect(r.kept > 2 && r.kept < 1500) << std::format("{} of {} glyphs kept", r.kept, r.glyphs);
        expect(r.bytes_after * 4 < r.bytes_before) << std::format("{} → {} bytes", r.bytes_before, r.bytes_after);
        expect(eq(sum(*cut), 0xB1B0AFBAu));  // the file's checksum, as head says it must be
        {
            std::ofstream out(scratch_dir("font-subset") / "NotoSansSC.cut.ttf", std::ios::binary);  // to look at (fontTools, a font viewer)
            out.write(reinterpret_cast<const char*>(cut->data()), static_cast<std::streamsize>(cut->size()));
        }
        auto again = subset::subset_font(*cut, keep);
        expect(again.has_value() && *again == *cut) << "cut again: the same bytes";
        // A character more: its glyph more.
        std::set<char32_t> more = keep;
        more.insert(U'龙');
        auto bigger = subset::subset_font(font, more);
        expect(bigger.has_value() && bigger->size() > cut->size()) << "龙 adds its outline";
    };

    "the characters of text: UTF-8 read, malformed bytes passed over"_test = [] {
        std::set<char32_t> got;
        subset::characters_of("a中\xff文\xe4", got);
        expect(got == std::set<char32_t>{U'a', U'中', U'文'});
    };

    "initial coverage: HarfBuzz sees only the two characters; the later face supplies the rest"_test = [&] {
        auto cut = subset::subset_font(font, {U'中', U'文'});
        expect(cut.has_value()) << (cut ? "" : cut.error());
        if (!cut) return;
        mlibs::text::FontCollection fonts{mlibs::text::harfbuzz::shaper()};
        const auto initial = fonts.add_data(*cut), full = fonts.add_data(font);
        expect(initial.has_value() && full.has_value());
        if (!initial || !full) return;
        expect(fonts.covers(*initial, U'中') && fonts.covers(*initial, U'文'));
        expect(!fonts.covers(*initial, U'龙') && !fonts.covers(*initial, U'A')) << "the tiny face must not claim emptied glyphs";
        fonts.set_fallback({*initial, *full});
        const std::array<std::string, 1> family{"Noto Sans SC"};
        expect(fonts.select(family, U"中文") == *initial);
        expect(fonts.select(family, U"龙") == *full) << "including an explicitly requested family, full coverage remains reachable";
        const auto a = fonts.metrics(fonts.font(*initial, 500, mlibs::text::Slant::normal), 14);
        const auto b = fonts.metrics(fonts.font(*full, 500, mlibs::text::Slant::normal), 14);
        expect(a.ascent == b.ascent && a.descent == b.descent) << "the same face keeps its metrics";
        auto twice = subset::subset_font(*cut, {U'中', U'文'});
        expect(twice && *twice == *cut) << "coverage and bytes are stable on a second cut";
    };

    "a later full Inter face completes its explicit family before another family's fallback"_test = [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        auto sparse = subset::subset_font(inter, {U'A', U' '});
        expect(sparse.has_value()) << (sparse ? "" : sparse.error()); if (!sparse) return;
        mlibs::text::FontCollection fonts{mlibs::text::harfbuzz::shaper()};
        const auto small = fonts.add_data(*sparse);
        expect(small.has_value()); if (!small) return;
        const auto reference = fonts.font(*small, 500, mlibs::text::Slant::normal);
        const auto original_metrics = fonts.metrics(reference, 14);
        const auto other = fonts.add_data(font), full = fonts.add_data(inter);
        expect(other.has_value() && full.has_value()); if (!other || !full) return;
        expect(fonts.covers(*small, U'A') && !fonts.covers(*small, U'B') && fonts.covers(*full, U'B') && fonts.covers(*other, U'B'));
        // The fallback deliberately offers another family before either Inter
        // face, so merely finding the full font in the chain cannot pass.
        fonts.set_fallback({*other, *small, *full});
        for (const auto& name : {"Inter", "iNtEr", "sans-serif", "SYSTEM-UI", "UI"}) {
            const std::array<std::string, 1> family{name};
            expect(fonts.select(family, U"A") == *small) << "covered glyphs retain the original sparse FaceId";
            expect(fonts.select(family, U"B") == *full) << "a later face completes the requested family before fallback";
            expect(fonts.select(family, U"AB") == *full) << "the entire cluster must be covered by one face";
        }
        const std::array<std::string, 2> inter_first{"Inter", "Noto Sans SC"}, noto_first{"Noto Sans SC", "Inter"};
        expect(fonts.select(inter_first, U"B") == *full && fonts.select(noto_first, U"B") == *other) << "explicit family order remains authoritative";
        expect(fonts.select(inter_first, U"中") == *other) << "cross-family fallback still supplies an uncovered script";
        expect(fonts.find_family("INTER") == small) << "find_family retains the original face identity";
        expect(fonts.face_of(reference) == *small && fonts.font(*small, 500, mlibs::text::Slant::normal) == reference) << "previously shaped references are not replaced";
        const auto now = fonts.metrics(reference, 14);
        expect(now.ascent == original_metrics.ascent && now.descent == original_metrics.descent);
        // The host groups a primary family's tiny and full faces before
        // later fallback families. With no explicit family, that ordering
        // must also select Inter for a non-ASCII glyph both full fonts have.
        expect(!fonts.covers(*small, U'é') && fonts.covers(*full, U'é') && fonts.covers(*other, U'é'));
        fonts.set_fallback({*small, *full, *other});
        expect(fonts.select({}, U"é") == *full && fonts.select({}, U"A") == *small) << "the complete primary family precedes cross-family fallback";
    };


    "initial Inter preserves default substitutions and positions at both UI weights (its pixels: omui.gen's font_delivery_test, where a renderer is)"_test = [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        std::set<char32_t> keep;
        // The last ones are kerned (class pairs and glyph pairs: AV, To, Ty, Wa, P., LT, r., 1 and 7), in their pairs.
        const std::array<std::string, 16> samples{"Office", "Notification", "configuration", "One interface.", "Every perspective.", ":-)", "UI (CPU)", "A-B", "2x3",
                                                  "AVATAR Wave", "Today Type", "Pa. LT r. y,", "17 71 \"To\"", "Yo Ye P,", "Vo Va We", "fi fl ff"};
        for (const auto& sample : samples) subset::characters_of(sample, keep);
        auto cut = subset::subset_font(inter, keep);
        expect(cut.has_value()) << (cut ? "" : cut.error()); if (!cut) return;
        mlibs::text::FontCollection full{mlibs::text::harfbuzz::shaper()}, small{mlibs::text::harfbuzz::shaper()};
        expect(full.add_data(inter).has_value() && small.add_data(*cut).has_value());
        bool saw_substitution = false;
        for (const float weight : {400.0f, 600.0f}) for (const auto& sample : samples) {
            mlibs::text::ParagraphStyle style; style.style.size = 28; style.style.weight = weight;
            mlibs::text::Paragraph a(full, sample, style), b(small, sample, style);
            a.layout(512); b.layout(512);
            expect(a.lines().size() == b.lines().size());
            if (a.lines().size() != b.lines().size()) continue;
            for (std::size_t line = 0; line < a.lines().size(); ++line) {
                const auto& ar = a.lines()[line].runs; const auto& br = b.lines()[line].runs;
                expect(ar.size() == br.size()); if (ar.size() != br.size()) continue;
                for (std::size_t run = 0; run < ar.size(); ++run) {
                    expect(ar[run].glyphs.size() == br[run].glyphs.size());
                    if (ar[run].glyphs.size() != br[run].glyphs.size()) continue;
                    for (std::size_t i = 0; i < ar[run].glyphs.size(); ++i) {
                        const auto& ag = ar[run].glyphs[i]; const auto& bg = br[run].glyphs[i];
                        expect(ag.id == bg.id && ag.position.x == bg.position.x && ag.position.y == bg.position.y) << std::format("{} at weight {} retains shaping", sample, weight);
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
        expect(saw_substitution) << "the fixture exercises a real default calt glyph, not only cmap glyphs";
        // Kerning is cut with the glyphs: the pairs no kept glyph is in, and the classes none is in, are gone.
        const auto gpos_full = table(inter, "GPOS"), gpos_cut = table(*cut, "GPOS");
        expect(gpos_cut.size() * 3 < gpos_full.size()) << std::format("GPOS {} -> {} bytes", gpos_full.size(), gpos_cut.size());
        auto twice = subset::subset_font(*cut, keep);
        expect(twice && *twice == *cut) << "substitution/composite closure is byte stable";
    };


    "a face cut for the shaping features keeps what those make and no other feature's glyphs; the other features do nothing"_test = [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        std::set<char32_t> keep;
        const std::array<std::string, 6> samples{"Office", "Notification 2026", "One interface.", "(UI) A-B", "0123456789", "Today: 17 of 1,024 -> ok"};
        for (const auto& sample : samples) subset::characters_of(sample, keep);
        subset::FontSubsetReport all_report, shaping_report;
        auto everything = subset::subset_font(inter, keep, &all_report);
        auto cut = subset::subset_font(inter, keep, &shaping_report, subset::shaping_features());
        expect(everything.has_value() && cut.has_value()) << (cut ? "" : cut.error()); if (!everything || !cut) return;
        expect(shaping_report.kept * 2 < all_report.kept && cut->size() < everything->size())
            << std::format("{} of {} glyphs kept for the shaping features, {} of them for every feature", shaping_report.kept, shaping_report.glyphs, all_report.kept);
        mlibs::text::FontCollection full{mlibs::text::harfbuzz::shaper()}, small{mlibs::text::harfbuzz::shaper()};
        expect(full.add_data(inter).has_value() && small.add_data(*cut).has_value());
        using Placed = std::vector<std::array<float, 3>>;  // glyph, x, y
        const auto shaped = [](mlibs::text::FontCollection& fonts, const std::string& sample, std::vector<mlibs::text::Feature> features) {
            mlibs::text::ParagraphStyle style; style.style.size = 28; style.style.weight = 600; style.style.features = std::move(features);
            mlibs::text::Paragraph paragraph(fonts, sample, style); paragraph.layout(1024);
            Placed out;
            for (const auto& line : paragraph.lines()) for (const auto& run : line.runs) for (const auto& glyph : run.glyphs)
                out.push_back({static_cast<float>(glyph.id), glyph.position.x, glyph.position.y});
            return out;
        };
        for (const auto& sample : samples) {
            // What the shaper does by itself, and tabular figures: the same as the whole face.
            expect(shaped(small, sample, {}) == shaped(full, sample, {})) << std::format("{}: the default features shape as the whole face", sample);
            expect(shaped(small, sample, {mlibs::text::Feature::on("tnum")}) == shaped(full, sample, {mlibs::text::Feature::on("tnum")}))
                << std::format("{}: tabular figures shape as the whole face", sample);
            // Alternates the cut does not hold: no feature of theirs is left, so the glyphs are the plain ones, never an emptied outline.
            for (const char* other : {"ss01", "cv11", "zero", "case", "dlig"})
                expect(shaped(small, sample, {mlibs::text::Feature::on(other)}) == shaped(small, sample, {})) << std::format("{}: {} does nothing on the cut", sample, other);
        }
        // The metrics of glyphs nothing draws are emptied, in place: hmtx (4 bytes each) holds as many entries as before, of
        // which only the kept glyphs' (and the last, which every glyph after it has) are not zero.
        const auto metrics = table(*cut, "hmtx");
        const auto whole_metrics = table(inter, "hmtx");
        const auto used = [&](std::span<const std::uint8_t> bytes) {
            std::size_t n = 0;
            for (std::size_t at = 0; at + 4 <= bytes.size(); at += 4) n += std::ranges::any_of(bytes.subspan(at, 4), [](auto byte) { return byte != 0; });
            return n;
        };
        expect(metrics.size() == whole_metrics.size() && used(metrics) <= shaping_report.kept + 1 && used(metrics) * 4 < used(whole_metrics))
            << std::format("{} advance entries in use of {}, {} glyphs kept", used(metrics), used(whole_metrics), shaping_report.kept);
        const auto twice = subset::subset_font(*cut, keep, nullptr, subset::shaping_features());
        expect(twice && *twice == *cut) << "cut again: the same bytes";
    };

    "GPOS is cut with the glyphs: a pair set keeps the pairs of kept glyphs; what is not understood stays as it was"_test = [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        mlibs::text::FontCollection fonts{mlibs::text::harfbuzz::shaper()}; expect(fonts.add_data(inter).has_value());
        const auto a = glyph_id(fonts, "A"), b = glyph_id(fonts, "B"), v = glyph_id(fonts, "V");
        // PairPos format 1 after A: V by -50, B by -20 (its coverage after the pair set).
        const auto pairs = gpos_table(2, words({1, 22, 4, 0, 1, 12, 2, v, static_cast<std::uint16_t>(-50), b, static_cast<std::uint16_t>(-20), 1, 1, a}));
        const auto cut = subset::subset_font(with_table(inter, "GPOS", pairs), {U'A', U'V'});
        expect(cut.has_value()) << (cut ? "" : cut.error()); if (!cut) return;
        const auto gpos = table(*cut, "GPOS");
        const auto lookups = std::size_t{word(gpos, 8)}, lookup = lookups + word(gpos, lookups + 2), subtable = lookup + word(gpos, lookup + 6);
        expect(word(gpos, lookup) == 2 && word(gpos, subtable) == 1 && word(gpos, subtable + 4) == 4) << "a pair lookup, format 1, its values' format kept";
        const auto set = subtable + word(gpos, subtable + 10);
        expect(word(gpos, subtable + 8) == 1 && word(gpos, set) == 1 && word(gpos, set + 2) == v && static_cast<std::int16_t>(word(gpos, set + 4)) == -50)
            << "the pair with B is gone, the pair with V is as it was";
        const auto covering = subtable + word(gpos, subtable + 2);
        expect(word(gpos, covering) == 1 && word(gpos, covering + 2) == 1 && word(gpos, covering + 4) == a) << "A is covered";
        const auto twice = subset::subset_font(*cut, {U'A', U'V'});
        expect(twice && *twice == *cut) << "cut again: the same bytes";

        // No kept glyph in a lookup: nothing of it stays.
        const auto gone = subset::subset_font(with_table(inter, "GPOS", pairs), {U'B', U'V'});
        expect(gone.has_value()) << (gone ? "" : gone.error()); if (!gone) return;
        const auto empty = table(*gone, "GPOS");
        const auto none = std::size_t{word(empty, 8)} + word(empty, std::size_t{word(empty, 8)} + 2);
        expect(word(empty, none + 4) == 0) << "a lookup no kept glyph is the first of has no subtable";

        // Contextual positioning is not written again: where a kept glyph can reach it the table stays whole, and so does
        // a table that is cut short — never worse than before.
        const auto contextual = gpos_table(7, words({1, 6, 0, 0, a, 0}));
        const auto kept_whole = subset::subset_font(with_table(inter, "GPOS", contextual), {U'A'});
        expect(kept_whole.has_value() && std::ranges::equal(table(*kept_whole, "GPOS"), contextual)) << "contextual positioning: the table as it was";
        for (std::size_t size = 0; size < pairs.size(); ++size) {
            const auto short_cut = subset::subset_font(with_table(inter, "GPOS", std::span(pairs).first(size)), {U'A', U'V'});
            expect(short_cut.has_value()) << std::format("a GPOS of {} bytes is read without failing", size);
        }
    };

    "all GSUB output forms close transitively with composite glyphs without retaining unrelated outlines"_test = [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        mlibs::text::FontCollection fonts{mlibs::text::harfbuzz::shaper()}; expect(fonts.add_data(inter).has_value());
        const auto a = glyph_id(fonts, "A"), b = glyph_id(fonts, "B"), c = glyph_id(fonts, "C"), d = glyph_id(fonts, "D"),
                   e = glyph_id(fonts, "E"), f = glyph_id(fonts, "F"), g = glyph_id(fonts, "G"), h = glyph_id(fonts, "H"),
                   i = glyph_id(fonts, "I"), k = glyph_id(fonts, "K"), y = glyph_id(fonts, "Y"), z = glyph_id(fonts, "Z"), accented = glyph_id(fonts, "é");
        const auto composite = outline(inter, accented);
        expect(!composite.empty() && static_cast<std::int16_t>(word(composite, 0)) < 0); if (composite.size() < 14) return;
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
        subset::FontSubsetReport report;
        const auto cut = subset::subset_font(source, {U'A'}, &report);
        expect(cut.has_value()) << (cut ? "" : cut.error()); if (!cut) return;
        for (auto id : {a, b, c, d, e, f, g, h, i, k, accented, component}) {
            auto expected = outline(inter, id), actual = outline(*cut, id);
            // Subsetting pads glyf records to four bytes; compare all original
            // bytes and allow only zero alignment bytes after them.
            expect(actual.size() >= expected.size() && std::equal(expected.begin(), expected.end(), actual.begin()) &&
                  std::ranges::all_of(std::span(actual).subspan(std::min(actual.size(), expected.size())), [](auto byte) { return byte == 0; })) << std::format("reachable glyph {} keeps its original outline", id);
        }
        expect(outline(*cut, y).empty() && outline(*cut, z).empty()) << "a ligature needs every input, and unrelated glyphs stay empty";
        expect(report.kept < 30) << "conservative lookup scanning is still a small subset";
        const auto twice = subset::subset_font(*cut, {U'A'});
        expect(twice && *twice == *cut) << "fixed-point closure is byte stable";
    };

    "malformed and unsupported GSUB structures are rejected explicitly"_test = [&] {
        const auto inter = read(root / "modules/content/text/fonts/Inter.ttf");
        mlibs::text::FontCollection fonts{mlibs::text::harfbuzz::shaper()}; expect(fonts.add_data(inter).has_value());
        const auto a = glyph_id(fonts, "A"), b = glyph_id(fonts, "B");
        const auto valid = gsub_table({{1, words({2, 8, 1, b, 1, 1, a})}});
        for (std::size_t size = 0; size < valid.size(); ++size) {
            const auto result = subset::subset_font(with_gsub(inter, std::span(valid).first(size)), {U'A'});
            expect(!result && result.error().starts_with("GSUB:")) << std::format("truncated GSUB of {} bytes is rejected", size);
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
            const auto result = subset::subset_font(with_gsub(inter, bad), {U'A'});
            expect(!result && result.error().starts_with("GSUB:")) << "unsupported formats and invalid offsets/glyphs must fail explicitly";
        }
    };
}};
