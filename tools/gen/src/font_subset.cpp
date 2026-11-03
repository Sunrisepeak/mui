// A TrueType font cut down to the glyphs its kept characters reach.
module openmui.gen.font_subset;

import std;

namespace omui::gen {

namespace {

using Bytes = std::vector<std::uint8_t>;

std::uint16_t u16(std::span<const std::uint8_t> b, std::size_t at) { return at + 2 <= b.size() ? static_cast<std::uint16_t>(b[at] << 8 | b[at + 1]) : 0; }
std::uint32_t u32(std::span<const std::uint8_t> b, std::size_t at) {
    return at + 4 <= b.size() ? static_cast<std::uint32_t>(b[at]) << 24 | static_cast<std::uint32_t>(b[at + 1]) << 16 | static_cast<std::uint32_t>(b[at + 2]) << 8 | b[at + 3]
                              : 0;
}
void put16(Bytes& b, std::size_t at, std::uint16_t v) {
    b[at] = static_cast<std::uint8_t>(v >> 8);
    b[at + 1] = static_cast<std::uint8_t>(v);
}
void put32(Bytes& b, std::size_t at, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(v >> (24 - 8 * i));
}
void push16(Bytes& b, std::uint16_t v) { b.push_back(static_cast<std::uint8_t>(v >> 8)), b.push_back(static_cast<std::uint8_t>(v)); }
void push32(Bytes& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(v >> (24 - 8 * i)));
}

struct Table {
    std::string   tag;
    std::span<const std::uint8_t> data;
};

std::uint32_t checksum(std::span<const std::uint8_t> d) {
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < d.size(); i += 4) {
        std::uint32_t w = 0;
        for (std::size_t k = 0; k < 4; ++k) w = w << 8 | (i + k < d.size() ? d[i + k] : 0);
        sum += w;
    }
    return sum;
}

// The glyph ids `cmap` gives the characters (its Unicode subtable: format 12, else 4).
std::expected<std::map<char32_t, std::uint32_t>, std::string> glyphs_of(std::span<const std::uint8_t> cmap, const std::set<char32_t>& keep) {
    const std::uint16_t n = u16(cmap, 2);
    std::size_t best = 0;
    int best_rank = 0;
    for (std::uint16_t i = 0; i < n; ++i) {
        const std::size_t rec = 4 + std::size_t{i} * 8;
        const std::uint16_t platform = u16(cmap, rec), encoding = u16(cmap, rec + 2);
        const std::size_t at = u32(cmap, rec + 4);
        const std::uint16_t format = u16(cmap, at);
        const bool unicode = platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10));
        const int rank = !unicode ? 0 : format == 12 ? 2 : format == 4 ? 1 : 0;
        if (rank > best_rank) best = at, best_rank = rank;
    }
    if (best_rank == 0) return std::unexpected("no Unicode cmap subtable (format 12 or 4)");
    std::map<char32_t, std::uint32_t> out;
    if (best_rank == 2) {
        const std::uint32_t groups = u32(cmap, best + 12);
        for (std::uint32_t g = 0; g < groups; ++g) {
            const std::size_t at = best + 16 + std::size_t{g} * 12;
            const std::uint32_t first = u32(cmap, at), last = u32(cmap, at + 4), glyph = u32(cmap, at + 8);
            for (auto it = keep.lower_bound(static_cast<char32_t>(first)); it != keep.end() && *it <= last; ++it)
                if (const auto id = glyph + (*it - first); id != 0) out.emplace(*it, id);
        }
        return out;
    }
    const std::size_t segs = u16(cmap, best + 6) / 2;
    const std::size_t ends = best + 14, starts = ends + segs * 2 + 2, deltas = starts + segs * 2, ranges = deltas + segs * 2;
    for (std::size_t s = 0; s < segs; ++s) {
        const std::uint16_t end = u16(cmap, ends + s * 2), start = u16(cmap, starts + s * 2), delta = u16(cmap, deltas + s * 2),
                            range = u16(cmap, ranges + s * 2);
        for (auto it = keep.lower_bound(start); it != keep.end() && *it <= end; ++it) {
            std::uint16_t g = 0;
            if (range == 0) g = static_cast<std::uint16_t>(*it + delta);
            else {
                g = u16(cmap, ranges + s * 2 + range + 2 * (*it - start));
                if (g) g = static_cast<std::uint16_t>(g + delta);
            }
            if (g) out.emplace(*it, g);
        }
    }
    return out;
}

// Unicode format 12, shared by the Unicode and Windows UCS-4 records.
// Glyph ids stay original; characters outside the subset are available to fallback fonts.
Bytes cmap_for(const std::map<char32_t, std::uint32_t>& mapping) {
    struct Group { std::uint32_t first, last, glyph; };
    std::vector<Group> groups;
    for (const auto& [cp, glyph] : mapping) {
        const auto c = static_cast<std::uint32_t>(cp);
        if (!groups.empty() && c == groups.back().last + 1 && glyph == groups.back().glyph + c - groups.back().first)
            groups.back().last = c;
        else groups.push_back({c, c, glyph});
    }
    Bytes out;
    push16(out, 0); push16(out, 2);
    push16(out, 0); push16(out, 4); push32(out, 20);
    push16(out, 3); push16(out, 10); push32(out, 20);
    push16(out, 12); push16(out, 0);
    push32(out, static_cast<std::uint32_t>(16 + groups.size() * 12));
    push32(out, 0); push32(out, static_cast<std::uint32_t>(groups.size()));
    for (const auto& g : groups) { push32(out, g.first); push32(out, g.last); push32(out, g.glyph); }
    return out;
}

}  // namespace

void characters_of(std::string_view t, std::set<char32_t>& into) {
    for (std::size_t i = 0; i < t.size();) {
        const auto c = static_cast<unsigned char>(t[i]);
        const int n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
        if (n == 0 || i + static_cast<std::size_t>(n) > t.size()) {
            ++i;
            continue;
        }
        char32_t cp = n == 1 ? c : c & (0xff >> (n + 1));
        bool ok = true;
        for (int k = 1; k < n; ++k) {
            const auto x = static_cast<unsigned char>(t[i + static_cast<std::size_t>(k)]);
            if ((x & 0xc0) != 0x80) ok = false;
            cp = cp << 6 | (x & 0x3f);
        }
        if (ok) into.insert(cp);
        i += ok ? static_cast<std::size_t>(n) : 1;
    }
}

void always_kept(std::set<char32_t>& into) {
    for (char32_t c = 0x20; c < 0x250; ++c) into.insert(c);     // ASCII, Latin-1, Latin Extended
    for (char32_t c = 0x2000; c < 0x2070; ++c) into.insert(c);  // general punctuation
    for (char32_t c = 0x2190; c < 0x2200; ++c) into.insert(c);  // arrows
    for (char32_t c = 0x3000; c < 0x3040; ++c) into.insert(c);  // CJK symbols and punctuation
    for (char32_t c = 0xff00; c < 0xfff0; ++c) into.insert(c);  // full-width forms
}

std::expected<Bytes, std::string> subset_font(std::span<const std::uint8_t> font, const std::set<char32_t>& keep, FontSubsetReport* report) {
    if (font.size() < 12 || (u32(font, 0) != 0x00010000 && u32(font, 0) != 0x74727565)) return std::unexpected("not a TrueType font (glyf outlines)");
    const std::uint16_t count = u16(font, 4);
    std::vector<Table> tables;
    std::map<std::string, std::span<const std::uint8_t>, std::less<>> by;
    for (std::uint16_t i = 0; i < count; ++i) {
        const std::size_t rec = 12 + std::size_t{i} * 16;
        std::string tag(reinterpret_cast<const char*>(font.data() + rec), 4);
        const std::size_t at = u32(font, rec + 8), len = u32(font, rec + 12);
        if (at + len > font.size()) return std::unexpected(std::format("table {} lies outside the file", tag));
        tables.push_back({tag, font.subspan(at, len)});
        by[tag] = tables.back().data;
    }
    for (std::string_view need : {"head", "maxp", "loca", "glyf", "cmap"})
        if (!by.contains(need)) return std::unexpected(std::format("no `{}` table (TrueType outlines are needed)", need));
    const auto head = by["head"], maxp = by["maxp"], loca = by["loca"], glyf = by["glyf"];
    const std::size_t glyphs = u16(maxp, 4);
    const bool long_loca = u16(head, 50) == 1;
    auto offset = [&](std::size_t g) -> std::size_t { return long_loca ? u32(loca, g * 4) : std::size_t{u16(loca, g * 2)} * 2; };

    // The glyphs kept: .notdef, what the characters map to, and the components of composites, closed over.
    auto mapped = glyphs_of(by["cmap"], keep);
    if (!mapped) return std::unexpected(mapped.error());
    std::vector<bool> kept(glyphs, false);
    std::vector<std::uint32_t> todo{0};
    for (const auto& [cp, g] : *mapped) todo.push_back(g);
    const Bytes new_cmap = cmap_for(*mapped);
    while (!todo.empty()) {
        const std::uint32_t g = todo.back();
        todo.pop_back();
        if (g >= glyphs || kept[g]) continue;
        kept[g] = true;
        const std::size_t at = offset(g), end = offset(g + 1);
        if (end <= at || static_cast<std::int16_t>(u16(glyf, at)) >= 0) continue;
        // A composite: its components.
        std::size_t p = at + 10;
        for (;;) {
            const std::uint16_t flags = u16(glyf, p);
            todo.push_back(u16(glyf, p + 2));
            p += 4 + ((flags & 0x0001) ? 4 : 2);
            if (flags & 0x0008) p += 2;
            else if (flags & 0x0040) p += 4;
            else if (flags & 0x0080) p += 8;
            if (!(flags & 0x0020) || p >= end) break;
        }
    }

    // glyf and loca again: the kept glyphs as they were, the others empty (long offsets).
    Bytes new_glyf, new_loca;
    for (std::size_t g = 0; g < glyphs; ++g) {
        push32(new_loca, static_cast<std::uint32_t>(new_glyf.size()));
        if (!kept[g]) continue;
        const std::size_t at = offset(g), end = offset(g + 1);
        if (end > at) new_glyf.insert(new_glyf.end(), glyf.begin() + static_cast<std::ptrdiff_t>(at), glyf.begin() + static_cast<std::ptrdiff_t>(end));
        while (new_glyf.size() % 4) new_glyf.push_back(0);
    }
    push32(new_loca, static_cast<std::uint32_t>(new_glyf.size()));
    Bytes new_head(head.begin(), head.end());
    put16(new_head, 50, 1);
    put32(new_head, 8, 0);  // checkSumAdjustment: set once the file is whole

    // gvar again, when the font varies: the kept glyphs' variation data, the others none (long offsets).
    Bytes new_gvar;
    if (by.contains("gvar")) {
        const auto gv = by["gvar"];
        const std::uint16_t axes = u16(gv, 4), shared = u16(gv, 6), gcount = u16(gv, 12), flags = u16(gv, 14);
        const std::size_t shared_at = u32(gv, 8), data_at = u32(gv, 16);
        auto goff = [&](std::size_t g) -> std::size_t { return (flags & 1) ? u32(gv, 20 + g * 4) : std::size_t{u16(gv, 20 + g * 2)} * 2; };
        const std::size_t shared_len = std::size_t{shared} * axes * 2;
        Bytes data;
        std::vector<std::uint32_t> offs;
        for (std::size_t g = 0; g < gcount; ++g) {
            offs.push_back(static_cast<std::uint32_t>(data.size()));
            if (g < glyphs && !kept[g]) continue;
            const std::size_t a = data_at + goff(g), b = data_at + goff(g + 1);
            if (b > a && b <= gv.size()) data.insert(data.end(), gv.begin() + static_cast<std::ptrdiff_t>(a), gv.begin() + static_cast<std::ptrdiff_t>(b));
        }
        offs.push_back(static_cast<std::uint32_t>(data.size()));
        const std::size_t header = 20 + offs.size() * 4;
        new_gvar.assign(gv.begin(), gv.begin() + 20);
        put16(new_gvar, 14, static_cast<std::uint16_t>(flags | 1));
        put32(new_gvar, 8, static_cast<std::uint32_t>(header));
        put32(new_gvar, 16, static_cast<std::uint32_t>(header + shared_len));
        for (const std::uint32_t o : offs) push32(new_gvar, o);
        if (shared_at + shared_len <= gv.size())
            new_gvar.insert(new_gvar.end(), gv.begin() + static_cast<std::ptrdiff_t>(shared_at), gv.begin() + static_cast<std::ptrdiff_t>(shared_at + shared_len));
        new_gvar.insert(new_gvar.end(), data.begin(), data.end());
    }

    // The file again, its tables in their order, each on four bytes.
    std::vector<std::pair<std::string, std::span<const std::uint8_t>>> out_tables;
    for (const Table& t : tables) {
        if (t.tag == "glyf") out_tables.push_back({t.tag, new_glyf});
        else if (t.tag == "loca") out_tables.push_back({t.tag, new_loca});
        else if (t.tag == "head") out_tables.push_back({t.tag, new_head});
        else if (t.tag == "cmap") out_tables.push_back({t.tag, new_cmap});
        else if (t.tag == "gvar") out_tables.push_back({t.tag, new_gvar});
        else out_tables.push_back({t.tag, t.data});
    }
    Bytes out(font.begin(), font.begin() + 12);
    out.resize(12 + out_tables.size() * 16, 0);
    std::size_t head_at = 0;
    for (std::size_t i = 0; i < out_tables.size(); ++i) {
        const auto& [tag, data] = out_tables[i];
        while (out.size() % 4) out.push_back(0);
        const std::size_t at = out.size();
        if (tag == "head") head_at = at;
        out.insert(out.end(), data.begin(), data.end());
        const std::size_t rec = 12 + i * 16;
        std::memcpy(out.data() + rec, tag.data(), 4);
        put32(out, rec + 4, checksum(data));
        put32(out, rec + 8, static_cast<std::uint32_t>(at));
        put32(out, rec + 12, static_cast<std::uint32_t>(data.size()));
    }
    while (out.size() % 4) out.push_back(0);
    put32(out, head_at + 8, 0xB1B0AFBAu - checksum(out));
    if (report) {
        report->glyphs = glyphs;
        report->kept = static_cast<std::size_t>(std::ranges::count(kept, true));
        report->bytes_before = font.size();
        report->bytes_after = out.size();
    }
    return out;
}

}  // namespace omui::gen
