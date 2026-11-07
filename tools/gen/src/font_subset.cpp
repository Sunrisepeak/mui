// A TrueType font cut down to the glyphs its kept characters reach.
module omui.gen.font_subset;

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

std::expected<std::vector<Table>, std::string> delivery_tables(std::span<const std::uint8_t> font) {
    if (font.size() < 12 || (u32(font, 0) != 0x00010000 && u32(font, 0) != 0x74727565))
        return std::unexpected("delivery font is not a standalone TrueType SFNT");
    const std::size_t count = u16(font, 4), directory_end = 12 + count * 16;
    if (!count || directory_end > font.size()) return std::unexpected("truncated or empty font table directory");
    std::size_t power = 1, selector = 0;
    while (power * 2 <= count) power *= 2, ++selector;
    if (u16(font, 6) != power * 16 || u16(font, 8) != selector || u16(font, 10) != count * 16 - power * 16)
        return std::unexpected("invalid SFNT directory search fields");
    std::vector<Table> tables;
    std::vector<std::pair<std::size_t, std::size_t>> extents;
    std::string previous;
    bool has_head = false;
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t record = 12 + i * 16;
        std::string tag(reinterpret_cast<const char*>(font.data() + record), 4);
        if ((!previous.empty() && tag <= previous) || std::ranges::any_of(tag, [](unsigned char c) { return c < 32 || c > 126; }))
            return std::unexpected("font table tags must be unique and sorted printable ASCII");
        previous = tag;
        const std::size_t at = u32(font, record + 8), length = u32(font, record + 12);
        if (at % 4 || at > font.size() || length > font.size() - at || (length && at < directory_end))
            return std::unexpected(std::format("table {} has an invalid offset or length", tag));
        auto data = font.subspan(at, length);
        auto sum = checksum(data);
        if (tag == "head") {
            if (length < 54) return std::unexpected("truncated head table");
            has_head = true;
            sum -= u32(data, 8); // head's directory checksum excludes checkSumAdjustment.
        }
        if (sum != u32(font, record + 4)) return std::unexpected(std::format("table {} has an invalid checksum", tag));
        tables.push_back({std::move(tag), data});
        if (length) extents.emplace_back(at, at + length);
    }
    std::ranges::sort(extents);
    for (std::size_t i = 1; i < extents.size(); ++i)
        if (extents[i - 1].second > extents[i].first) return std::unexpected("font table payloads overlap");
    if (!has_head || checksum(font) != 0xB1B0AFBAu) return std::unexpected("missing head or invalid whole-font checksum");
    return tables;
}

// Preserve substitution outputs without changing glyph ids or layout tables.
// Scan every lookup, including contextual lookup targets, irrespective of the
// enabled features. A rule fires only when all of its input glyphs are kept.
struct SubstitutionRule {
    std::vector<std::uint16_t> inputs, outputs;
};

struct Substitutions {
    std::span<const std::uint8_t> data;
    std::size_t glyphs;
    std::uint16_t lookup_count = 0;
    std::string error;
    std::vector<SubstitutionRule> rules;

    bool fail(std::string_view message) {
        if (error.empty()) error = std::format("GSUB: {}", message);
        return false;
    }
    bool has(std::size_t at, std::size_t bytes) {
        return (at <= data.size() && bytes <= data.size() - at) || fail("truncated table or invalid offset");
    }
    std::uint16_t word(std::size_t at) { return has(at, 2) ? u16(data, at) : 0; }
    std::size_t relative(std::size_t base, std::size_t delta) {
        if (!delta || !has(base, delta) || !has(base + delta, 2)) { fail("invalid relative offset"); return data.size(); }
        return base + delta;
    }
    bool glyph(std::uint16_t id) { return id < glyphs || fail("glyph id exceeds maxp glyph count"); }
    std::vector<std::uint16_t> coverage(std::size_t at) {
        std::vector<std::uint16_t> out;
        const auto format = word(at), count = word(at + 2);
        if (!error.empty()) return out;
        if (format == 1) {
            if (!has(at + 4, std::size_t{count} * 2)) return out;
            for (std::size_t i = 0; i < count; ++i) {
                const auto id = word(at + 4 + i * 2);
                if (!glyph(id) || (!out.empty() && id <= out.back())) { fail("invalid coverage order"); return {}; }
                out.push_back(id);
            }
        } else if (format == 2) {
            if (!has(at + 4, std::size_t{count} * 6)) return out;
            for (std::size_t i = 0; i < count; ++i) {
                const auto first = word(at + 4 + i * 6), last = word(at + 6 + i * 6), start = word(at + 8 + i * 6);
                if (first > last || start != out.size() || !glyph(last) || (!out.empty() && first <= out.back())) {
                    fail("invalid coverage range"); return {};
                }
                for (std::uint32_t id = first; id <= last; ++id) out.push_back(static_cast<std::uint16_t>(id));
            }
        } else fail("unsupported coverage format");
        return out;
    }
    bool class_definition(std::size_t at) {
        const auto format = word(at);
        if (format == 1) {
            const auto start = word(at + 2), count = word(at + 4);
            return has(at + 6, std::size_t{count} * 2) && (std::size_t{start} + count <= glyphs || fail("invalid class glyph range"));
        }
        if (format == 2) {
            const auto count = word(at + 2);
            if (!has(at + 4, std::size_t{count} * 6)) return false;
            std::uint32_t previous = 0;
            for (std::size_t i = 0; i < count; ++i) {
                const auto first = word(at + 4 + i * 6), last = word(at + 6 + i * 6);
                if (first > last || !glyph(last) || (i && first <= previous)) return fail("invalid class range");
                previous = last;
            }
            return error.empty();
        }
        return fail("unsupported class definition format");
    }
    bool records(std::size_t at, std::size_t count, std::size_t inputs) {
        if (!has(at, count * 4)) return false;
        for (std::size_t i = 0; i < count; ++i)
            if (word(at + i * 4) >= inputs || word(at + i * 4 + 2) >= lookup_count) return fail("invalid contextual lookup record");
        return error.empty();
    }
    // Context tables introduce no output glyphs themselves. Validate their
    // structures; their referenced lookups are scanned independently below.
    bool context(std::uint16_t type, std::size_t at, std::uint16_t format) {
        const bool chain = type == 6;
        if (format == 3) {
            std::size_t cursor = at + 2, inputs = 0, substitutions = 0;
            if (!chain) { inputs = word(cursor); substitutions = word(cursor + 2); cursor += 4; }
            for (int group = 0; group < (chain ? 3 : 1); ++group) {
                const std::size_t count = chain ? word(cursor) : inputs;
                if (chain) cursor += 2;
                if (group == (chain ? 1 : 0)) inputs = count;
                if (!has(cursor, count * 2)) return false;
                for (std::size_t i = 0; i < count; ++i) coverage(relative(at, word(cursor + i * 2)));
                cursor += count * 2;
            }
            if (chain) { substitutions = word(cursor); cursor += 2; }
            if (!inputs) return fail("empty contextual input");
            return error.empty() && records(cursor, substitutions, inputs);
        }
        if (format != 1 && format != 2) return fail("unsupported contextual substitution format");
        const auto covered = coverage(relative(at, word(at + 2)));
        std::size_t count_at = at + 4;
        if (format == 2) {
            const int classes = chain ? 3 : 1;
            for (int i = 0; i < classes; ++i) class_definition(relative(at, word(at + 4 + std::size_t(i) * 2)));
            count_at += std::size_t(classes) * 2;
        }
        const auto count = word(count_at);
        if (format == 1 && count != covered.size()) return fail("contextual set count does not match coverage");
        if (!error.empty() || !has(count_at + 2, std::size_t{count} * 2)) return false;
        for (std::size_t i = 0; i < count; ++i) {
            const auto delta = word(count_at + 2 + i * 2);
            if (!delta) continue;  // no rules for this input/class
            const auto set = relative(at, delta);
            const auto n = word(set);
            if (!has(set + 2, std::size_t{n} * 2)) return false;
            for (std::size_t j = 0; j < n; ++j) {
                const auto rule = relative(set, word(set + 2 + j * 2));
                std::size_t cursor = rule, inputs = 0, substitutions = 0;
                if (!chain) { inputs = word(cursor); substitutions = word(cursor + 2); cursor += 4; }
                for (int group = 0; group < (chain ? 3 : 1); ++group) {
                    const std::size_t count_glyphs = chain ? word(cursor) : inputs;
                    if (chain) cursor += 2;
                    const bool input = group == (chain ? 1 : 0);
                    if (input) inputs = count_glyphs;
                    if (input && !count_glyphs) return fail("empty contextual input");
                    const auto stored = count_glyphs - (input ? 1 : 0);
                    if (!has(cursor, stored * 2)) return false;
                    if (format == 1) for (std::size_t k = 0; k < stored; ++k) if (!glyph(word(cursor + k * 2))) return false;
                    cursor += stored * 2;
                }
                if (chain) { substitutions = word(cursor); cursor += 2; }
                if (!error.empty() || !records(cursor, substitutions, inputs)) return false;
            }
        }
        return error.empty();
    }
    bool subtable(std::uint16_t type, std::size_t at) {
        auto format = word(at);
        if (type == 7) {
            if (format != 1 || !has(at, 8)) return fail("unsupported or truncated extension format");
            type = word(at + 2);
            if (type == 7 || type < 1 || type > 8) return fail("invalid extension lookup type");
            at = relative(at, u32(data, at + 4)); format = word(at);
        }
        if (type == 5 || type == 6) return context(type, at, format);
        if (type < 1 || type > 8) return fail("unsupported substitution lookup type");
        if ((type == 1 && format != 1 && format != 2) || (type != 1 && format != 1)) return fail("unsupported substitution format");
        const auto covered = coverage(relative(at, word(at + 2)));
        if (!error.empty()) return false;
        if (type == 1 && format == 1) {
            const auto delta = word(at + 4);
            for (const auto id : covered) {
                const auto output = static_cast<std::uint16_t>(id + delta);
                if (!glyph(output)) return false;
                rules.push_back({{id}, {output}});
            }
        } else if (type == 8) {
            std::size_t cursor = at + 4;
            for (int group = 0; group < 2; ++group) {
                const auto count = word(cursor); cursor += 2;
                if (!has(cursor, std::size_t{count} * 2)) return false;
                for (std::size_t i = 0; i < count; ++i) coverage(relative(at, word(cursor + i * 2)));
                cursor += std::size_t{count} * 2;
            }
            const auto count = word(cursor); cursor += 2;
            if (count != covered.size() || !has(cursor, std::size_t{count} * 2)) return fail("invalid reverse substitution count");
            for (std::size_t i = 0; i < count; ++i) {
                const auto output = word(cursor + i * 2);
                if (!glyph(output)) return false;
                rules.push_back({{covered[i]}, {output}});
            }
        } else {
            const auto count = word(at + 4);
            if (count != covered.size() || !has(at + 6, std::size_t{count} * 2)) return fail("substitution count does not match coverage");
            for (std::size_t i = 0; i < count; ++i) {
                if (type == 1) {
                    const auto output = word(at + 6 + i * 2);
                    if (!glyph(output)) return false;
                    rules.push_back({{covered[i]}, {output}});
                    continue;
                }
                const auto set = relative(at, word(at + 6 + i * 2));
                const auto n = word(set);
                if (!has(set + 2, std::size_t{n} * 2)) return false;
                if (type == 2 || type == 3) {
                    SubstitutionRule rule{{covered[i]}, {}};
                    for (std::size_t j = 0; j < n; ++j) {
                        const auto id = word(set + 2 + j * 2);
                        if (!glyph(id)) return false;
                        rule.outputs.push_back(id);
                    }
                    rules.push_back(std::move(rule));
                } else if (type == 4) {
                    for (std::size_t j = 0; j < n; ++j) {
                        const auto ligature = relative(set, word(set + 2 + j * 2));
                        const auto output = word(ligature), components = word(ligature + 2);
                        if (!glyph(output) || !components || !has(ligature + 4, (std::size_t{components} - 1) * 2)) return fail("invalid ligature components");
                        SubstitutionRule rule{{covered[i]}, {output}};
                        for (std::size_t k = 1; k < components; ++k) {
                            const auto id = word(ligature + 2 + k * 2);
                            if (!glyph(id)) return false;
                            rule.inputs.push_back(id);
                        }
                        rules.push_back(std::move(rule));
                    }
                }
            }
        }
        return error.empty();
    }
    bool parse() {
        if (!has(0, 10) || word(0) != 1 || word(2) > 1) return fail("unsupported or truncated version");
        if (word(2) == 1 && !has(0, 14)) return false;
        const auto list = relative(0, word(8)); lookup_count = word(list);
        if (!has(list + 2, std::size_t{lookup_count} * 2)) return false;
        for (std::size_t i = 0; i < lookup_count; ++i) {
            const auto lookup = relative(list, word(list + 2 + i * 2));
            const auto type = word(lookup), flags = word(lookup + 2), count = word(lookup + 4);
            if (type < 1 || type > 8 || !has(lookup + 6, std::size_t{count} * 2 + ((flags & 0x10) ? 2 : 0))) return fail("invalid lookup header");
            for (std::size_t j = 0; j < count; ++j)
                if (!subtable(type, relative(lookup, word(lookup + 6 + j * 2)))) return false;
        }
        return error.empty();
    }
};

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

std::expected<Bytes, std::string> compact_font_post(std::span<const std::uint8_t> font, FontDeliveryReport* report) {
    // OpenType post/otff: https://learn.microsoft.com/typography/opentype/spec/post
    // Only delivery data changes. Keep physical table order and all unrelated
    // payload/padding bytes, instead of rebuilding glyph or layout tables.
    auto parsed = delivery_tables(font);
    if (!parsed) return std::unexpected(parsed.error());
    auto find = [&](std::string_view tag) -> std::span<const std::uint8_t> {
        for (const auto& table : *parsed) if (table.tag == tag) return table.data;
        return {};
    };
    auto unchanged = [&]() -> std::expected<Bytes, std::string> {
        if (report) *report = {font.size(), font.size(), false};
        return Bytes(font.begin(), font.end());
    };
    const auto post = find("post");
    if (post.empty()) {
        if (std::ranges::any_of(*parsed, [](const auto& table) { return table.tag == "post"; }))
            return std::unexpected("empty post table");
        return unchanged();
    }
    if (post.size() < 32) return std::unexpected("truncated post header");
    const auto version = u32(post, 0);
    if (version == 0x00030000u) {
        if (post.size() != 32) return std::unexpected("post 3 table must end after its 32-byte header");
        return unchanged();
    }
    const auto maxp = find("maxp");
    if (maxp.size() < 6 || !u16(maxp, 4)) return std::unexpected("post names need valid maxp glyph metadata");
    const auto glyphs = u16(maxp, 4);
    if (version == 0x00010000u) {
        if (post.size() != 32 || glyphs != 258) return std::unexpected("invalid post 1 Macintosh glyph metadata");
    } else if (version == 0x00020000u || version == 0x00025000u) {
        if (post.size() < 34 || u16(post, 32) != glyphs) return std::unexpected("post glyph count does not match maxp");
        const std::size_t index_bytes = std::size_t{glyphs} * (version == 0x00020000u ? 2 : 1);
        if (index_bytes > post.size() - 34) return std::unexpected("truncated post glyph-name indices");
        if (version == 0x00020000u) {
            std::uint16_t greatest = 257;
            for (std::size_t i = 0; i < glyphs; ++i) greatest = std::max(greatest, u16(post, 34 + i * 2));
            std::size_t strings = 0, cursor = 34 + index_bytes;
            while (cursor < post.size()) {
                const std::size_t length = post[cursor++];
                if (length > post.size() - cursor) return std::unexpected("truncated post glyph-name string");
                // Names are opaque Pascal strings here. Existing delivery
                // fonts include non-AGL names; none is used to identify glyphs.
                cursor += length;
                ++strings;
            }
            if (strings < std::size_t{greatest} - 257) return std::unexpected("post glyph-name index has no string");
        } else {
            if (post.size() != 34 + index_bytes) return std::unexpected("invalid post 2.5 glyph-name offsets");
            for (std::size_t i = 0; i < glyphs; ++i) {
                const int offset = post[34 + i] < 128 ? post[34 + i] : int(post[34 + i]) - 256;
                const int index = static_cast<int>(i) + offset;
                if (index < 0 || index > 257) return std::unexpected("post 2.5 name exceeds Macintosh glyph set");
            }
        }
    } else return std::unexpected("unsupported post table version");
    const auto signature = find("DSIG");
    if (!signature.empty() && (signature.size() < 8 || u16(signature, 4) != 0))
        return std::unexpected("cannot change a digitally signed font's post table");

    const auto post_at = static_cast<std::size_t>(post.data() - font.data());
    const auto erase_begin = post_at + 32;
    auto erase_end = post_at + post.size();
    erase_end += std::min((4 - post.size() % 4) % 4, font.size() - erase_end);
    const auto removed = erase_end - erase_begin;
    Bytes out(font.begin(), font.end());
    out.erase(out.begin() + static_cast<std::ptrdiff_t>(erase_begin), out.begin() + static_cast<std::ptrdiff_t>(erase_end));
    put32(out, post_at, 0x00030000u);
    std::size_t head_at = 0;
    for (std::size_t i = 0; i < parsed->size(); ++i) {
        const auto record = 12 + i * 16;
        const auto& table = (*parsed)[i];
        std::size_t at = u32(font, record + 8);
        if (at >= erase_end) at -= removed;
        else if (at >= erase_begin && table.data.empty()) at = erase_begin;
        put32(out, record + 8, static_cast<std::uint32_t>(at));
        if (table.tag == "post") put32(out, record + 12, 32);
        if (table.tag == "head") head_at = at;
    }
    put32(out, head_at + 8, 0);
    for (std::size_t i = 0; i < parsed->size(); ++i) {
        const auto record = 12 + i * 16;
        put32(out, record + 4, checksum(std::span(out).subspan(u32(out, record + 8), u32(out, record + 12))));
    }
    put32(out, head_at + 8, 0xB1B0AFBAu - checksum(out));
    if (report) *report = {font.size(), out.size(), true};
    return out;
}

std::expected<Bytes, std::string> subset_font(std::span<const std::uint8_t> font, const std::set<char32_t>& keep, FontSubsetReport* report) {
    if (font.size() < 12 || (u32(font, 0) != 0x00010000 && u32(font, 0) != 0x74727565)) return std::unexpected("not a TrueType font (glyf outlines)");
    const std::uint16_t count = u16(font, 4);
    if (12 + std::size_t{count} * 16 > font.size()) return std::unexpected("truncated font table directory");
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

    if (!glyphs || head.size() < 54 || maxp.size() < 6 || loca.size() < (glyphs + 1) * (long_loca ? 4 : 2))
        return std::unexpected("truncated glyph metadata");
    for (std::size_t g = 0; g < glyphs; ++g)
        if (offset(g) > offset(g + 1) || offset(g + 1) > glyf.size()) return std::unexpected("invalid glyph offsets");
    Substitutions substitutions{by.contains("GSUB") ? by["GSUB"] : std::span<const std::uint8_t>{}, glyphs};
    if (by.contains("GSUB") && !substitutions.parse()) return std::unexpected(substitutions.error);
    auto mapped = glyphs_of(by["cmap"], keep);
    if (!mapped) return std::unexpected(mapped.error());
    std::vector<bool> kept(glyphs, false);
    std::vector<std::uint32_t> todo{0};
    for (const auto& [cp, g] : *mapped) {
        if (g >= glyphs) return std::unexpected("cmap glyph exceeds maxp glyph count");
        todo.push_back(g);
    }
    const Bytes new_cmap = cmap_for(*mapped);
    // Alternate substitution and composite expansion to a fixed point. A GSUB
    // output can itself be a composite or an input to another lookup.
    for (;;) {
        while (!todo.empty()) {
            const std::uint32_t g = todo.back(); todo.pop_back();
            if (kept[g]) continue;
            kept[g] = true;
            const std::size_t at = offset(g), end = offset(g + 1);
            if (end == at) continue;
            if (end - at < 10) return std::unexpected("truncated glyph outline");
            if (static_cast<std::int16_t>(u16(glyf, at)) >= 0) continue;
            std::size_t p = at + 10;
            for (;;) {
                if (p > end || end - p < 4) return std::unexpected("truncated composite component");
                const std::uint16_t flags = u16(glyf, p), component = u16(glyf, p + 2);
                if (component >= glyphs) return std::unexpected("invalid composite glyph id");
                todo.push_back(component);
                p += 4 + ((flags & 0x0001) ? 4 : 2);
                if (flags & 0x0008) p += 2;
                else if (flags & 0x0040) p += 4;
                else if (flags & 0x0080) p += 8;
                if (p > end) return std::unexpected("truncated composite transform");
                if (!(flags & 0x0020)) break;
            }
        }
        for (const auto& rule : substitutions.rules)
            if (std::ranges::all_of(rule.inputs, [&](auto id) { return kept[id]; }))
                for (const auto id : rule.outputs) if (!kept[id]) todo.push_back(id);
        if (todo.empty()) break;
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
