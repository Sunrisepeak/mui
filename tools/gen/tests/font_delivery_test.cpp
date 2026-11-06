// The public packer is the oracle: delivery optimization must occur before
// hashing, without changing a source font, glyph ids or actual text pixels.
import std;
import omui.gen.dist_toy;
import omui.gen.font_subset;
import mlibs.text;
import mlibs.hash;
import mlibs.testing;
import xrender.text;

using namespace omui;
using namespace mlibs::testing;
namespace fs = std::filesystem;

namespace {
using Bytes = std::vector<std::uint8_t>;
std::uint16_t u16(std::span<const std::uint8_t> bytes, std::size_t at) { return static_cast<std::uint16_t>(bytes[at] << 8 | bytes[at + 1]); }
std::uint32_t u32(std::span<const std::uint8_t> bytes, std::size_t at) { return std::uint32_t{u16(bytes, at)} << 16 | u16(bytes, at + 2); }
void put16(Bytes& bytes, std::size_t at, std::uint16_t value) { bytes[at] = static_cast<std::uint8_t>(value >> 8); bytes[at + 1] = static_cast<std::uint8_t>(value); }
void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) bytes[at + i] = static_cast<std::uint8_t>(value >> (24 - i * 8));
}
Bytes read(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), {}};
}
void write(const fs::path& path, std::span<const std::uint8_t> bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
}
std::uint32_t checksum(std::span<const std::uint8_t> bytes) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < bytes.size(); i += 4) {
        std::uint32_t word = 0;
        for (std::size_t k = 0; k < 4; ++k) word = word << 8 | (i + k < bytes.size() ? bytes[i + k] : 0);
        sum += word;
    }
    return sum;
}
struct Record { std::string tag; std::size_t directory = 0, offset = 0, length = 0; std::uint32_t sum = 0; };
std::vector<Record> records(const Bytes& font) {
    check(font.size() >= 12); if (font.size() < 12) return {};
    const auto count = u16(font, 4);
    check(12 + std::size_t{count} * 16 <= font.size());
    if (12 + std::size_t{count} * 16 > font.size()) return {};
    std::vector<Record> out;
    for (std::size_t i = 0; i < count; ++i) {
        const auto at = 12 + i * 16;
        out.push_back({std::string(reinterpret_cast<const char*>(font.data() + at), 4), at, u32(font, at + 8), u32(font, at + 12), u32(font, at + 4)});
    }
    return out;
}
std::span<const std::uint8_t> table(const Bytes& font, std::string_view tag) {
    for (const auto& record : records(font)) if (record.tag == tag) {
        check(record.offset <= font.size() && record.length <= font.size() - record.offset);
        if (record.offset <= font.size() && record.length <= font.size() - record.offset) return std::span(font).subspan(record.offset, record.length);
    }
    return {};
}
void whole_checksum(Bytes& font) {
    for (const auto& record : records(font)) if (record.tag == "head") {
        put32(font, record.offset + 8, 0);
        put32(font, record.directory + 4, checksum(std::span(font).subspan(record.offset, record.length)));
        put32(font, record.offset + 8, 0xB1B0AFBAu - checksum(font));
    }
}
// An independent fixture writer, also able to remove a table. Production
// must preserve physical order; this writer intentionally uses directory order.
Bytes replace_table(const Bytes& font, std::string_view tag, std::optional<Bytes> replacement) {
    const auto input = records(font);
    std::vector<std::pair<std::string, Bytes>> tables;
    for (const auto& record : input) {
        if (record.tag == tag && !replacement) continue;
        const auto data = table(font, record.tag);
        tables.emplace_back(record.tag, record.tag == tag ? *replacement : Bytes(data.begin(), data.end()));
        if (record.tag == "head") put32(tables.back().second, 8, 0);
    }
    Bytes out(font.begin(), font.begin() + 12);
    const auto count = static_cast<std::uint16_t>(tables.size());
    std::uint16_t power = 1, selector = 0;
    while (power * 2 <= count) power *= 2, ++selector;
    put16(out, 4, count); put16(out, 6, static_cast<std::uint16_t>(power * 16)); put16(out, 8, selector);
    put16(out, 10, static_cast<std::uint16_t>(count * 16 - power * 16));
    out.resize(12 + tables.size() * 16);
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const auto& [name, bytes] = tables[i];
        while (out.size() % 4) out.push_back(0);
        const auto at = 12 + i * 16;
        std::memcpy(out.data() + at, name.data(), 4);
        put32(out, at + 4, checksum(bytes)); put32(out, at + 8, static_cast<std::uint32_t>(out.size()));
        put32(out, at + 12, static_cast<std::uint32_t>(bytes.size()));
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    while (out.size() % 4) out.push_back(0);
    whole_checksum(out);
    return out;
}
void valid_directory(const Bytes& font) {
    check(checksum(font) == 0xB1B0AFBAu, "the complete SFNT checksum is valid");
    auto tables = records(font);
    std::string previous;
    for (const auto& record : tables) {
        check(previous.empty() || previous < record.tag, "directory tags are unique and ordered"); previous = record.tag;
        check(record.offset % 4 == 0 && record.offset >= 12 + tables.size() * 16);
        check(record.offset <= font.size() && record.length <= font.size() - record.offset);
        Bytes data(table(font, record.tag).begin(), table(font, record.tag).end());
        if (record.tag == "head") put32(data, 8, 0);
        check(checksum(data) == record.sum, record.tag + " has a valid directory checksum");
    }
    std::ranges::sort(tables, {}, &Record::offset);
    for (std::size_t i = 1; i < tables.size(); ++i)
        check(tables[i - 1].offset + tables[i - 1].length <= tables[i].offset, "table payloads do not overlap");
}
void preserved_tables(const Bytes& before, const Bytes& after) {
    const auto a = records(before), b = records(after);
    check(a.size() == b.size()); if (a.size() != b.size()) return;
    for (std::size_t i = 0; i < a.size(); ++i) {
        check(a[i].tag == b[i].tag);
        if (a[i].tag == "post") continue;
        Bytes original(table(before, a[i].tag).begin(), table(before, a[i].tag).end());
        Bytes delivered(table(after, a[i].tag).begin(), table(after, a[i].tag).end());
        if (a[i].tag == "head") { put32(original, 8, 0); put32(delivered, 8, 0); }
        check(original == delivered, a[i].tag + " remains byte-for-byte identical (head adjustment excepted)");
    }
    auto ap = a, bp = b;
    std::ranges::sort(ap, {}, &Record::offset); std::ranges::sort(bp, {}, &Record::offset);
    for (std::size_t i = 0; i < ap.size(); ++i) check(ap[i].tag == bp[i].tag, "physical table order is retained");
    const auto old_post = table(before, "post"), post = table(after, "post");
    check(post.size() == 32 && u32(post, 0) == 0x00030000u, "the actual packaged post is a 32-byte version 3 table");
    if (post.size() == 32 && old_post.size() >= 32)
        check(std::equal(old_post.begin() + 4, old_post.begin() + 32, post.begin() + 4), "italic, underline, fixed pitch and all other header fields survive");
    valid_directory(after);
}
void text_is_identical(const Bytes& original, const Bytes& delivered, std::string sample) {
    mlibs::text::FontCollection before, after;
    const auto a = before.add_data(original), b = after.add_data(delivered);
    check(a && b); if (!a || !b) return;
    const auto& ai = before.face(*a); const auto& bi = after.face(*b);
    check(ai.family == bi.family && ai.style == bi.style && ai.units_per_em == bi.units_per_em && ai.weight == bi.weight && ai.slant == bi.slant &&
        ai.variable == bi.variable && ai.min_weight == bi.min_weight && ai.max_weight == bi.max_weight && ai.color == bi.color);
    xrender::text::GlyphCache ac(before), bc(after);
    bool actual_glyph = false;
    for (float weight : {400.0f, 600.0f}) {
        const auto am = before.metrics(before.font(*a, weight, mlibs::text::Slant::normal), 28);
        const auto bm = after.metrics(after.font(*b, weight, mlibs::text::Slant::normal), 28);
        check(am.ascent == bm.ascent && am.descent == bm.descent && am.line_gap == bm.line_gap && am.x_height == bm.x_height && am.cap_height == bm.cap_height &&
            am.underline_offset == bm.underline_offset && am.underline_thickness == bm.underline_thickness, "all actual metrics, including post underline values, match");
        mlibs::text::ParagraphStyle style; style.style.families = {ai.family}; style.style.weight = weight; style.style.size = 28; style.style.underline = true;
        mlibs::text::Paragraph ap(before, sample, style), bp(after, sample, style); ap.layout(200); bp.layout(200);
        check(ap.lines().size() == bp.lines().size()); if (ap.lines().size() != bp.lines().size()) continue;
        for (std::size_t line = 0; line < ap.lines().size(); ++line) {
            const auto& al = ap.lines()[line]; const auto& bl = bp.lines()[line];
            check(al.width == bl.width && al.height == bl.height && al.baseline == bl.baseline && al.begin == bl.begin && al.end == bl.end);
            check(al.runs.size() == bl.runs.size()); if (al.runs.size() != bl.runs.size()) continue;
            for (std::size_t run = 0; run < al.runs.size(); ++run) {
                const auto& ar = al.runs[run]; const auto& br = bl.runs[run];
                check(ar.decoration_offset == br.decoration_offset && ar.decoration_thickness == br.decoration_thickness);
                check(ar.glyphs.size() == br.glyphs.size()); if (ar.glyphs.size() != br.glyphs.size()) continue;
                for (std::size_t i = 0; i < ar.glyphs.size(); ++i) {
                    const auto& ag = ar.glyphs[i]; const auto& bg = br.glyphs[i]; actual_glyph |= ag.id != 0;
                    check(ag.id == bg.id && ag.position.x == bg.position.x && ag.position.y == bg.position.y, "HarfBuzz glyph ids and positioned results match");
                    const auto amask = ac.mask(xrender::text::font(ar.font), ag.id, ar.size, {}), bmask = bc.mask(xrender::text::font(br.font), bg.id, br.size, {});
                    check(bool(amask.coverage) == bool(bmask.coverage));
                    if (amask.coverage && bmask.coverage) {
                        const auto ab = amask.coverage->bounds, bb = bmask.coverage->bounds;
                        check(ab.x == bb.x && ab.y == bb.y && ab.width == bb.width && ab.height == bb.height && amask.coverage->values == bmask.coverage->values,
                            "the real rasterized glyph pixels match");
                    }
                }
            }
        }
    }
    check(actual_glyph, "the test actually shaped named source-font glyphs");
}
struct Package {
    fs::path root = scratch_dir("font-delivery") / std::format("case-{}", std::chrono::steady_clock::now().time_since_epoch().count());
    Package() {
        write(root / "stage/bin/game.js", Bytes{'v', 'a', 'r', ' ', 'M', 'o', 'd', 'u', 'l', 'e', ';'});
        write(root / "stage/bin/game.wasm", Bytes{0, 'a', 's', 'm', 1, 0, 0, 0});
    }
    gen::ToyPackage options(std::vector<std::string> fonts = {"Inter"}) const {
        return {.stage = root / "stage", .target = "game", .out = root / "out", .fonts = std::move(fonts), .fonts_dir = root / "fonts", .sdk = false};
    }
    void font(std::string name, const Bytes& bytes) { write(root / "fonts" / (name + ".ttf"), bytes); }
    Bytes sole_font(const gen::ToyReport& report) {
        for (const auto& file : report.files) if (file.path.ends_with(".ttf")) return read(root / "out" / file.path);
        check(false, "the actual package contains a TTF asset"); return {};
    }
};
}

int main(int argc, char** argv) {
    Suite s{"gen.font_delivery"};
    const auto directory = mlibs::text::FontCollection::default_directory();
    const auto inter = read(directory / "Inter.ttf"), mono = read(directory / "JetBrainsMono.ttf");
    s.test("real Inter full and initial plus Mono preserve metadata shaping and pixels through the actual packer", [&] {
        check(!inter.empty() && !mono.empty(), "real default TTF fixtures are available"); if (inter.empty() || mono.empty()) return;
        check(u32(table(inter, "post"), 0) == 0x00020000u && u32(table(mono, "post"), 0) == 0x00020000u);
        Package package; package.font("Inter", inter); package.font("JetBrainsMono", mono);
        auto options = package.options({"Inter", "JetBrainsMono"});
        const std::string sample = "Office Notification configuration omui UI :-)";
        options.font_initial = {{"Inter", sample}};
        std::set<char32_t> keep; gen::characters_of(sample, keep);
        const auto initial = gen::subset_font(inter, keep); check(initial.has_value()); if (!initial) return;
        const auto result = gen::package_for_toy(options); check(result.has_value(), result ? "" : result.error()); if (!result) return;
        int inter_full = 0, inter_initial = 0, monospaced = 0;
        for (const auto& file : result->files) if (file.path.ends_with(".ttf")) {
            const auto bytes = read(options.out / file.path);
            check(file.path == std::format("assets/{:012x}.ttf", mlibs::hash::fnv1a(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size())) & 0xffffffffffffull),
                "the content hash names the actual derived payload, not the source font");
            mlibs::text::FontCollection faces; const auto face = faces.add_data(bytes); check(face.has_value()); if (!face) continue;
            const auto& family = faces.face(*face).family;
            const bool is_mono = family == "JetBrains Mono", is_full = faces.covers(*face, U'Z');
            const auto& source = is_mono ? mono : is_full ? inter : *initial;
            is_mono ? ++monospaced : is_full ? ++inter_full : ++inter_initial;
            preserved_tables(source, bytes);
            text_is_identical(source, bytes, is_mono ? "const auto x = fi + 42; -> !==" : is_full ? sample + " é Å Ω →" : sample);
            check(bytes.size() < source.size(), "names are actually removed from each derived post2 face");
            std::println("font delivery: {} {}: {} -> {} raw bytes", family, is_full ? "full" : "initial", source.size(), bytes.size());
        }
        check(inter_full == 1 && inter_initial == 1 && monospaced == 1);
        check(read(package.root / "fonts/Inter.ttf") == inter && read(package.root / "fonts/JetBrainsMono.ttf") == mono, "source inputs are never rewritten");
        check(read(directory / "Inter.ttf") == inter && read(directory / "JetBrainsMono.ttf") == mono);
    });
    s.test("already-post3 and missing-post delivery inputs are byte-idempotent", [&] {
        check(!inter.empty()); if (inter.empty()) return;
        const auto original = table(inter, "post"); Bytes post(original.begin(), original.begin() + 32); put32(post, 0, 0x00030000u);
        for (const auto& font : {replace_table(inter, "post", post), replace_table(inter, "post", std::nullopt)}) {
            Package package; package.font("Inter", font);
            const auto result = gen::package_for_toy(package.options()); check(result.has_value(), result ? "" : result.error()); if (!result) continue;
            const auto packed = package.sole_font(*result); check(packed == font); valid_directory(packed);
            package.font("Inter", packed);
            const auto again = gen::package_for_toy(package.options()); check(again && package.sole_font(*again) == packed);
        }
    });
    s.test("malformed directory post and checksum inputs are explicitly refused by delivery", [&] {
        check(!inter.empty()); if (inter.empty()) return;
        const auto source_post = table(inter, "post");
        std::vector<Bytes> malformed;
        malformed.push_back(replace_table(inter, "post", Bytes(31)));
        malformed.push_back(replace_table(inter, "post", Bytes(source_post.begin(), source_post.begin() + 34)));
        auto wrong_count = Bytes(source_post.begin(), source_post.end()); put16(wrong_count, 32, 1);
        malformed.push_back(replace_table(inter, "post", wrong_count));
        auto unknown = Bytes(source_post.begin(), source_post.begin() + 32); put32(unknown, 0, 0x00040000u);
        malformed.push_back(replace_table(inter, "post", unknown));
        auto outside = inter;
        for (const auto& record : records(outside)) if (record.tag == "post") put32(outside, record.directory + 12, 0xffffffffu);
        malformed.push_back(std::move(outside));
        auto bad_sum = inter;
        for (const auto& record : records(bad_sum)) if (record.tag == "name") bad_sum[record.offset] ^= 1;
        malformed.push_back(std::move(bad_sum));
        auto duplicate = inter;
        for (const auto& record : records(duplicate)) if (record.tag == "post") std::memcpy(duplicate.data() + record.directory, "name", 4);
        whole_checksum(duplicate); malformed.push_back(std::move(duplicate));
        const auto empty_signature = table(mono, "DSIG");
        Bytes signature(empty_signature.begin(), empty_signature.end()); put16(signature, 4, 1);
        malformed.push_back(replace_table(mono, "DSIG", signature));
        for (const auto& font : malformed) {
            Package package; package.font("Inter", font);
            const auto result = gen::package_for_toy(package.options());
            check(!result, "invalid font data cannot be silently repackaged as an optimized delivery font");
            if (!result) check(result.error().contains("font Inter"), result.error());
        }
    });
    return s.run(argc, argv);
}
