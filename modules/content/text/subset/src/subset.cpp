// A TrueType font cut down to the glyphs its kept characters reach.
module mlibs.text.subset;

import std;

namespace mlibs::text::subset {

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
    std::size_t lookup = 0;  // the lookup it is in
};

struct Substitutions {
    std::span<const std::uint8_t> data;
    std::size_t glyphs;
    std::uint16_t lookup_count = 0;
    std::string error;
    std::vector<SubstitutionRule> rules;
    std::vector<std::vector<std::uint16_t>> called;  // by lookup: the lookups its contextual rules call
    std::size_t current = 0;                         // the lookup being read
    bool tracking = false;                           // while the lookups are read: which lookup reaches each structure
    std::map<std::size_t, std::set<std::size_t>> reached_by;  // a structure's start, the lookups that reach it

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
        if (tracking) reached_by[base + delta].insert(current);
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
        for (std::size_t i = 0; i < count; ++i) {
            if (word(at + i * 4) >= inputs || word(at + i * 4 + 2) >= lookup_count) return fail("invalid contextual lookup record");
            called[current].push_back(word(at + i * 4 + 2));
        }
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
        called.assign(lookup_count, {});
        tracking = true;
        for (std::size_t i = 0; i < lookup_count; ++i) {
            current = i;
            const std::size_t first_rule = rules.size();
            const auto lookup = relative(list, word(list + 2 + i * 2));
            const auto type = word(lookup), flags = word(lookup + 2), count = word(lookup + 4);
            if (type < 1 || type > 8 || !has(lookup + 6, std::size_t{count} * 2 + ((flags & 0x10) ? 2 : 0))) return fail("invalid lookup header");
            for (std::size_t j = 0; j < count; ++j)
                if (!subtable(type, relative(lookup, word(lookup + 6 + j * 2)))) return false;
            for (std::size_t k = first_rule; k < rules.size(); ++k) rules[k].lookup = i;
        }
        tracking = false;
        return error.empty();
    }

    // The lookups the layout features given reach — theirs, and what those call — or none when every lookup is to be
    // taken (no feature given, or feature variations, which add lookups to a feature where a condition holds).
    std::optional<std::vector<bool>> reached(std::span<const std::string_view> features) {
        if (features.empty() || (word(2) == 1 && u32(data, 10) != 0)) return std::nullopt;
        std::vector<bool> reach(lookup_count, false);
        std::vector<std::size_t> todo;
        const auto list = relative(0, word(6)), count = std::size_t{word(list)};
        for (std::size_t i = 0; i < count && error.empty(); ++i) {
            if (!has(list + 2 + i * 6, 6)) return std::nullopt;
            const std::string_view tag(reinterpret_cast<const char*>(data.data() + list + 2 + i * 6), 4);
            if (std::ranges::find(features, tag) == features.end()) continue;
            const auto feature = relative(list, word(list + 2 + i * 6 + 4)), lookups = std::size_t{word(feature + 2)};
            if (!has(feature + 4, lookups * 2)) return std::nullopt;
            for (std::size_t k = 0; k < lookups; ++k) todo.push_back(word(feature + 4 + k * 2));
        }
        if (!error.empty()) return std::nullopt;
        while (!todo.empty()) {
            const std::size_t lookup = todo.back();
            todo.pop_back();
            if (lookup >= lookup_count || reach[lookup]) continue;
            reach[lookup] = true;
            for (const std::uint16_t next : called[lookup]) todo.push_back(next);
        }
        return reach;
    }

    // The table with the features not given left without lookups (their lookup counts 0, their indices zeroed — in place:
    // nothing moves), so a cut that does not hold their glyphs applies none of them. A feature table two records share is
    // left as it is while one of them is kept.
    Bytes without_other_features(std::span<const std::string_view> features, const std::vector<bool>& reach) {
        Bytes out(data.begin(), data.end());
        const auto list = relative(0, word(6)), count = std::size_t{word(list)};
        std::set<std::size_t> kept_tables;
        for (std::size_t i = 0; i < count && error.empty(); ++i) {
            const std::string_view tag(reinterpret_cast<const char*>(data.data() + list + 2 + i * 6), 4);
            if (std::ranges::find(features, tag) != features.end()) kept_tables.insert(relative(list, word(list + 2 + i * 6 + 4)));
        }
        for (std::size_t i = 0; i < count && error.empty(); ++i) {
            const std::string_view tag(reinterpret_cast<const char*>(data.data() + list + 2 + i * 6), 4);
            const auto feature = relative(list, word(list + 2 + i * 6 + 4));
            if (std::ranges::find(features, tag) != features.end() || kept_tables.contains(feature)) continue;
            const std::size_t lookups = word(feature + 2);
            if (!has(feature + 4, lookups * 2)) return Bytes(data.begin(), data.end());
            put16(out, feature + 2, 0);
            std::fill_n(out.begin() + static_cast<std::ptrdiff_t>(feature + 4), lookups * 2, std::uint8_t{0});
        }
        if (!error.empty()) return Bytes(data.begin(), data.end());
        // The lookups no kept feature reaches (the others' lookups, and what only those call): each without subtables
        // (a count of 0, no mark filtering set) and everything only they reach zeroed. A structure two lookups share stays
        // while one of them is reached. The extent of a structure is up to the next one that starts: tables of the layout
        // header, the script list and the feature list are such starts too, and are never zeroed.
        const std::size_t lookup_list = relative(0, word(8));
        std::map<std::size_t, bool> zeroed;  // start -> whether its bytes go
        for (const auto& [start, lookups] : reached_by)
            zeroed[start] = std::ranges::none_of(lookups, [&](std::size_t lookup) { return reach[lookup]; });
        for (std::size_t lookup = 0; lookup < lookup_count; ++lookup) {  // lookup tables are not emptied whole: their header stays
            const std::size_t at = lookup_list + word(lookup_list + 2 + lookup * 2);
            zeroed[at] = false;
        }
        for (const std::size_t fixed : {std::size_t{word(4)}, std::size_t{word(6)}, lookup_list}) zeroed[fixed] = false;
        if (!error.empty()) return Bytes(data.begin(), data.end());
        for (auto it = zeroed.begin(); it != zeroed.end(); ++it) {
            const auto next = std::next(it);
            if (it->second) std::fill(out.begin() + static_cast<std::ptrdiff_t>(it->first), out.begin() + static_cast<std::ptrdiff_t>(next == zeroed.end() ? out.size() : next->first), std::uint8_t{0});
        }
        for (std::size_t lookup = 0; lookup < lookup_count; ++lookup) {
            if (reach[lookup]) continue;
            const std::size_t at = lookup_list + word(lookup_list + 2 + lookup * 2), count = word(at + 4), flags = word(at + 2);
            put16(out, at + 2, static_cast<std::uint16_t>(flags & ~0x10u));
            put16(out, at + 4, 0);
            std::fill_n(out.begin() + static_cast<std::ptrdiff_t>(at + 6), count * 2 + ((flags & 0x10) ? 2 : 0), std::uint8_t{0});
        }
        return out;
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

// ── GPOS: the pair adjustments of the glyphs a cut keeps ─────────────────────────────────────────────────────────
// A cut keeps every glyph id, so GPOS — kerning above all: 154 KB of Inter's, 49 KB sent — would stay whole for the few
// hundred glyphs the cut can draw. Its pair lookups (PairPos formats 1 and 2) are written again for the kept glyphs
// alone (a pair set loses the pairs whose glyphs are not kept, a class matrix the classes no kept glyph is in), and a
// subtable of another kind is left out when no kept glyph can reach it. What it does not understand — a lookup of
// another kind a kept glyph can reach, contextual positioning, feature variations, an offset that does not fit in
// 16 bits — leaves the table as it was, so a cut is never worse than the one before it.
class GposPruner {
public:
    GposPruner(std::span<const std::uint8_t> table, const std::vector<bool>& kept) : table_(table), kept_(kept) {}

    // The table written again, or nothing when it is to stay as it is.
    std::optional<Bytes> run() {
        // 1.0, or 1.1 without feature variations (which would point into the lookups): a header of 10 bytes, or 14
        const std::size_t header = word(2) == 1 ? 14 : 10;
        if (word(0) != 1 || word(2) > 1 || (word(2) == 1 && dword(10) != 0)) return std::nullopt;
        const std::size_t scripts = word(4), features = word(6), lookups = word(8);
        const std::size_t scripts_end = script_list_end(scripts), features_end = feature_list_end(features);
        if (!ok_ || scripts_end <= scripts || features_end <= features) return std::nullopt;
        const std::size_t lookup_count = word(lookups);
        if (!has(lookups + 2, lookup_count * 2)) return std::nullopt;
        std::vector<Lookup> built;
        for (std::size_t i = 0; i < lookup_count && ok_; ++i) built.push_back(read_lookup(lookups + word(lookups + 2 + i * 2)));
        if (!ok_) return std::nullopt;
        Bytes list = lookup_list(built, false);
        if (list.empty()) list = lookup_list(built, true);  // the subtables do not reach in 16 bits: every lookup an extension
        if (list.empty()) return std::nullopt;
        const std::size_t scripts_size = scripts_end - scripts, features_size = features_end - features;
        if (header + scripts_size + features_size > 0xFFFF) return std::nullopt;
        Bytes out;
        push16(out, 1), push16(out, word(2));
        push16(out, static_cast<std::uint16_t>(header));
        push16(out, static_cast<std::uint16_t>(header + scripts_size));
        push16(out, static_cast<std::uint16_t>(header + scripts_size + features_size));
        if (header == 14) push32(out, 0);
        out.insert(out.end(), table_.begin() + static_cast<std::ptrdiff_t>(scripts), table_.begin() + static_cast<std::ptrdiff_t>(scripts_end));
        out.insert(out.end(), table_.begin() + static_cast<std::ptrdiff_t>(features), table_.begin() + static_cast<std::ptrdiff_t>(features_end));
        out.insert(out.end(), list.begin(), list.end());
        return out;
    }

private:
    std::span<const std::uint8_t> table_;
    const std::vector<bool>& kept_;
    bool ok_ = true;
    void fail() { ok_ = false; }

    // Device tables a value record points to, copied once whatever the number of records pointing at them.
    struct Devices {
        Bytes bytes;
        std::map<Bytes, std::size_t> at;
    };
    struct DevicePointer {
        std::size_t where;      // the offset's place in the subtable
        std::size_t device_at;  // the device table's place in the pool
        std::size_t parent;     // the table the offset is from: the subtable, or a pair set (format 1)
    };

    bool has(std::size_t at, std::size_t bytes) const { return at <= table_.size() && bytes <= table_.size() - at; }
    std::uint16_t word(std::size_t at) {
        if (!has(at, 2)) return fail(), 0;
        return u16(table_, at);
    }
    std::uint32_t dword(std::size_t at) {
        if (!has(at, 4)) return fail(), 0;
        return u32(table_, at);
    }
    bool is_kept(std::size_t glyph) const { return glyph < kept_.size() && kept_[glyph]; }

    // The scripts' and the features' tables, copied as they are: how far each reaches from its start.
    std::size_t language_system_end(std::size_t at) { return at + 6 + std::size_t{word(at + 4)} * 2; }
    std::size_t script_list_end(std::size_t at) {
        const std::size_t count = word(at);
        std::size_t end = at + 2 + count * 6;
        for (std::size_t i = 0; i < count && ok_; ++i) {
            const std::size_t script = at + word(at + 2 + i * 6 + 4), systems = word(script + 2), fallback = word(script);
            end = std::max(end, script + 4 + systems * 6);
            if (fallback) end = std::max(end, language_system_end(script + fallback));
            for (std::size_t k = 0; k < systems && ok_; ++k) end = std::max(end, language_system_end(script + word(script + 4 + k * 6 + 4)));
        }
        return ok_ && has(at, end - at) ? end : 0;
    }
    std::size_t feature_list_end(std::size_t at) {
        const std::size_t count = word(at);
        std::size_t end = at + 2 + count * 6;
        for (std::size_t i = 0; i < count && ok_; ++i) {
            const std::size_t record = at + 2 + i * 6, feature = at + word(record + 4);
            end = std::max(end, feature + 4 + std::size_t{word(feature + 2)} * 2);
            if (const std::size_t parameters = word(feature)) {  // the few kinds of feature parameters there are
                const std::string_view tag(reinterpret_cast<const char*>(table_.data() + record), 4);
                const std::size_t params = feature + parameters;
                if (tag == "size") end = std::max(end, params + 10);
                else if (tag.starts_with("ss")) end = std::max(end, params + 4);
                else if (tag.starts_with("cv")) end = std::max(end, params + 14 + std::size_t{word(params + 12)} * 3);
                else fail();
            }
        }
        return ok_ && has(at, end - at) ? end : 0;
    }

    std::vector<std::uint16_t> coverage(std::size_t at) {
        std::vector<std::uint16_t> out;
        const auto format = word(at), count = word(at + 2);
        if (!ok_) return out;
        if (format == 1 && has(at + 4, std::size_t{count} * 2)) {
            for (std::size_t i = 0; i < count; ++i) out.push_back(word(at + 4 + i * 2));
        } else if (format == 2 && has(at + 4, std::size_t{count} * 6)) {
            for (std::size_t i = 0; i < count; ++i) {
                const auto first = word(at + 4 + i * 6), last = word(at + 6 + i * 6);
                if (first > last || word(at + 8 + i * 6) != out.size()) { fail(); return out; }
                for (std::uint32_t glyph = first; glyph <= last; ++glyph) out.push_back(static_cast<std::uint16_t>(glyph));
            }
        } else fail();
        return out;
    }
    static Bytes coverage_table(const std::vector<std::uint16_t>& glyphs) {
        std::vector<std::array<std::uint16_t, 3>> ranges;  // first, last, the index of first
        for (std::size_t i = 0; i < glyphs.size(); ++i) {
            if (!ranges.empty() && glyphs[i] == ranges.back()[1] + 1) ranges.back()[1] = glyphs[i];
            else ranges.push_back({glyphs[i], glyphs[i], static_cast<std::uint16_t>(i)});
        }
        Bytes out;
        if (glyphs.size() * 2 <= ranges.size() * 6) {
            push16(out, 1), push16(out, static_cast<std::uint16_t>(glyphs.size()));
            for (const auto glyph : glyphs) push16(out, glyph);
        } else {
            push16(out, 2), push16(out, static_cast<std::uint16_t>(ranges.size()));
            for (const auto& range : ranges) push16(out, range[0]), push16(out, range[1]), push16(out, range[2]);
        }
        return out;
    }
    // The glyphs that have a class but 0 (what is not in a class definition is in class 0).
    std::map<std::uint16_t, std::uint16_t> class_definition(std::size_t at) {
        std::map<std::uint16_t, std::uint16_t> out;
        const auto format = word(at);
        if (format == 1) {
            const std::size_t start = word(at + 2), count = word(at + 4);
            if (!has(at + 6, count * 2)) return fail(), out;
            for (std::size_t i = 0; i < count; ++i)
                if (const auto glyph_class = word(at + 6 + i * 2)) out[static_cast<std::uint16_t>(start + i)] = glyph_class;
        } else if (format == 2) {
            const std::size_t count = word(at + 2);
            if (!has(at + 4, count * 6)) return fail(), out;
            for (std::size_t i = 0; i < count; ++i) {
                const auto first = word(at + 4 + i * 6), last = word(at + 6 + i * 6), glyph_class = word(at + 8 + i * 6);
                for (std::uint32_t glyph = first; glyph <= last && glyph_class; ++glyph) out[static_cast<std::uint16_t>(glyph)] = glyph_class;
            }
        } else fail();
        return out;
    }
    static Bytes class_definition_table(const std::map<std::uint16_t, std::uint16_t>& classes) {
        std::vector<std::array<std::uint16_t, 3>> ranges;  // first, last, class
        for (const auto& [glyph, glyph_class] : classes) {
            if (!ranges.empty() && glyph == ranges.back()[1] + 1 && glyph_class == ranges.back()[2]) ranges.back()[1] = glyph;
            else ranges.push_back({glyph, glyph, glyph_class});
        }
        Bytes out;
        const std::size_t span = classes.empty() ? 0 : std::size_t{classes.rbegin()->first} - classes.begin()->first + 1;
        if (!classes.empty() && 6 + span * 2 < 4 + ranges.size() * 6) {
            push16(out, 1), push16(out, classes.begin()->first), push16(out, static_cast<std::uint16_t>(span));
            std::vector<std::uint16_t> array(span, 0);
            for (const auto& [glyph, glyph_class] : classes) array[glyph - classes.begin()->first] = glyph_class;
            for (const auto glyph_class : array) push16(out, glyph_class);
        } else {
            push16(out, 2), push16(out, static_cast<std::uint16_t>(ranges.size()));
            for (const auto& range : ranges) push16(out, range[0]), push16(out, range[1]), push16(out, range[2]);
        }
        return out;
    }

    // A value record: its numbers as they are, and the device tables it points to given offsets in the pool.
    std::size_t value_size(std::uint16_t format) {
        if (format & 0xFF00) fail();
        return std::size_t(std::popcount(static_cast<unsigned>(format & 0xFF))) * 2;
    }
    std::size_t device_size(std::size_t at) {
        const std::size_t first = word(at), last = word(at + 2), format = word(at + 4);
        if (format == 0x8000) return 6;
        if (format < 1 || format > 3 || last < first) return fail(), 0;
        return 6 + (((last - first + 1) * (std::size_t{1} << format) + 15) / 16) * 2;
    }
    // `from` is the table the source's offsets are from (the PairPos subtable, or a pair set), `parent` where its copy is in `out`.
    void copy_value(std::size_t at, std::uint16_t format, std::size_t from, std::size_t parent, Bytes& out, Devices& devices,
                    std::vector<DevicePointer>& pointers) {
        for (std::uint16_t bit = 1; bit <= 0x80 && ok_; bit = static_cast<std::uint16_t>(bit << 1)) {
            if (!(format & bit)) continue;
            const std::uint16_t value = word(at);
            at += 2;
            if (bit < 0x10 || value == 0) { push16(out, value); continue; }
            const std::size_t device = from + value, size = device_size(device);
            if (!ok_ || !has(device, size)) { fail(); return; }
            Bytes copy(table_.begin() + static_cast<std::ptrdiff_t>(device), table_.begin() + static_cast<std::ptrdiff_t>(device + size));
            const auto [found, added] = devices.at.emplace(copy, devices.bytes.size());
            if (added) devices.bytes.insert(devices.bytes.end(), copy.begin(), copy.end());
            pointers.push_back({out.size(), found->second, parent});
            push16(out, 0);
        }
    }
    // The pool after the subtable's own bytes, the pointers to it filled in.
    void append_devices(Bytes& out, const Devices& devices, const std::vector<DevicePointer>& pointers) {
        const std::size_t base = out.size();
        out.insert(out.end(), devices.bytes.begin(), devices.bytes.end());
        for (const DevicePointer& pointer : pointers) {
            if (base + pointer.device_at - pointer.parent > 0xFFFF) { fail(); return; }
            put16(out, pointer.where, static_cast<std::uint16_t>(base + pointer.device_at - pointer.parent));
        }
    }

    // PairPos format 1: a first glyph's set of second glyphs and their values.
    Bytes pair_glyphs(std::size_t at) {
        const std::uint16_t format1 = word(at + 4), format2 = word(at + 6);
        const std::size_t count = word(at + 8), size1 = value_size(format1), size2 = value_size(format2), record = 2 + size1 + size2;
        const auto firsts = coverage(at + word(at + 2));
        if (!ok_ || firsts.size() != count || !has(at + 10, count * 2)) return fail(), Bytes{};
        struct Pair {
            std::uint16_t second;
            std::size_t values;
        };
        std::vector<std::uint16_t> covered;
        std::vector<std::vector<Pair>> sets;
        std::vector<std::size_t> set_starts;  // where each set is in the source: its value records' device offsets are from it
        for (std::size_t i = 0; i < count && ok_; ++i) {
            if (!is_kept(firsts[i])) continue;
            const std::size_t set = at + word(at + 10 + i * 2), pairs = word(set);
            if (!has(set + 2, pairs * record)) return fail(), Bytes{};
            std::vector<Pair> kept_pairs;
            for (std::size_t k = 0; k < pairs; ++k) {
                const std::size_t pair = set + 2 + k * record;
                if (is_kept(word(pair))) kept_pairs.push_back({word(pair), pair + 2});
            }
            if (kept_pairs.empty()) continue;
            covered.push_back(firsts[i]);
            sets.push_back(std::move(kept_pairs));
            set_starts.push_back(set);
        }
        if (!ok_ || covered.empty()) return {};
        Bytes out;
        push16(out, 1), push16(out, 0), push16(out, format1), push16(out, format2), push16(out, static_cast<std::uint16_t>(sets.size()));
        out.resize(10 + sets.size() * 2);
        const Bytes covering = coverage_table(covered);
        put16(out, 2, static_cast<std::uint16_t>(out.size()));
        out.insert(out.end(), covering.begin(), covering.end());
        Devices devices;
        std::vector<DevicePointer> pointers;
        for (std::size_t i = 0; i < sets.size() && ok_; ++i) {
            if (out.size() > 0xFFFF) { fail(); break; }
            const std::size_t set_start = out.size();
            put16(out, 10 + i * 2, static_cast<std::uint16_t>(set_start));
            push16(out, static_cast<std::uint16_t>(sets[i].size()));
            for (const Pair& pair : sets[i]) {
                push16(out, pair.second);
                copy_value(pair.values, format1, set_starts[i], set_start, out, devices, pointers);
                copy_value(pair.values + size1, format2, set_starts[i], set_start, out, devices, pointers);
            }
        }
        if (ok_) append_devices(out, devices, pointers);
        return ok_ ? out : Bytes{};
    }

    // PairPos format 2: the classes of the first and second glyphs and a matrix of values between them.
    Bytes pair_classes(std::size_t at) {
        const std::uint16_t format1 = word(at + 4), format2 = word(at + 6);
        const std::size_t first_classes = word(at + 12), second_classes = word(at + 14);
        const std::size_t size1 = value_size(format1), size2 = value_size(format2), cell = size1 + size2;
        const auto firsts = coverage(at + word(at + 2));
        const auto classes1 = class_definition(at + word(at + 8)), classes2 = class_definition(at + word(at + 10));
        if (!ok_ || !has(at + 16, first_classes * second_classes * cell)) return fail(), Bytes{};
        const auto class_of = [](const std::map<std::uint16_t, std::uint16_t>& classes, std::uint16_t glyph) -> std::size_t {
            const auto found = classes.find(glyph);
            return found == classes.end() ? 0 : found->second;
        };
        std::vector<std::uint16_t> covered;
        std::set<std::size_t> rows{0}, columns{0};  // class 0 is what a glyph without a class is in: it stays
        for (const std::uint16_t glyph : firsts) {
            if (!is_kept(glyph)) continue;
            covered.push_back(glyph);
            rows.insert(class_of(classes1, glyph));
        }
        for (const auto& [glyph, glyph_class] : classes2)
            if (is_kept(glyph)) columns.insert(glyph_class);
        if (covered.empty()) return {};
        if (*rows.rbegin() >= first_classes || *columns.rbegin() >= second_classes) return fail(), Bytes{};
        std::map<std::size_t, std::uint16_t> new_row, new_column;
        for (const std::size_t row : rows) new_row.emplace(row, static_cast<std::uint16_t>(new_row.size()));
        for (const std::size_t column : columns) new_column.emplace(column, static_cast<std::uint16_t>(new_column.size()));
        std::map<std::uint16_t, std::uint16_t> kept_classes1, kept_classes2;
        for (const std::uint16_t glyph : covered)
            if (const std::size_t row = class_of(classes1, glyph)) kept_classes1[glyph] = new_row[row];
        for (const auto& [glyph, glyph_class] : classes2)
            if (is_kept(glyph)) kept_classes2[glyph] = new_column[glyph_class];
        Bytes out;
        push16(out, 2);
        for (int i = 0; i < 5; ++i) push16(out, 0);  // offsets, below
        out.resize(16);
        put16(out, 4, format1), put16(out, 6, format2);
        put16(out, 12, static_cast<std::uint16_t>(rows.size())), put16(out, 14, static_cast<std::uint16_t>(columns.size()));
        Devices devices;
        std::vector<DevicePointer> pointers;
        for (const std::size_t row : rows)
            for (const std::size_t column : columns) {
                const std::size_t values = at + 16 + (row * second_classes + column) * cell;
                copy_value(values, format1, at, 0, out, devices, pointers);
                copy_value(values + size1, format2, at, 0, out, devices, pointers);
            }
        const Bytes covering = coverage_table(covered), first_definition = class_definition_table(kept_classes1),
                    second_definition = class_definition_table(kept_classes2);
        if (out.size() + covering.size() + first_definition.size() + second_definition.size() > 0xFFFF) return fail(), Bytes{};
        put16(out, 2, static_cast<std::uint16_t>(out.size()));
        out.insert(out.end(), covering.begin(), covering.end());
        put16(out, 8, static_cast<std::uint16_t>(out.size()));
        out.insert(out.end(), first_definition.begin(), first_definition.end());
        put16(out, 10, static_cast<std::uint16_t>(out.size()));
        out.insert(out.end(), second_definition.begin(), second_definition.end());
        append_devices(out, devices, pointers);
        return ok_ ? out : Bytes{};
    }

    // SinglePos: one value for each covered glyph (format 1: the same for all; format 2: one each).
    Bytes single_values(std::size_t at) {
        const auto format = word(at), value_format = word(at + 4);
        const auto glyphs = coverage(at + word(at + 2));
        const std::size_t size = value_size(value_format);
        std::vector<std::uint16_t> covered;
        std::vector<std::size_t> values;  // where each kept glyph's value is
        if (format == 1 && has(at + 6, size)) {
            for (const std::uint16_t glyph : glyphs)
                if (is_kept(glyph)) covered.push_back(glyph), values.push_back(at + 6);
        } else if (format == 2 && word(at + 6) == glyphs.size() && has(at + 8, glyphs.size() * size)) {
            for (std::size_t i = 0; i < glyphs.size(); ++i)
                if (is_kept(glyphs[i])) covered.push_back(glyphs[i]), values.push_back(at + 8 + i * size);
        } else fail();
        if (!ok_ || covered.empty()) return {};
        Bytes out;
        push16(out, format), push16(out, 0), push16(out, value_format);
        if (format == 2) push16(out, static_cast<std::uint16_t>(covered.size()));
        Devices devices;
        std::vector<DevicePointer> pointers;
        if (format == 1) copy_value(values.front(), value_format, at, 0, out, devices, pointers);
        else for (const std::size_t value : values) copy_value(value, value_format, at, 0, out, devices, pointers);
        const Bytes covering = coverage_table(covered);
        if (out.size() + covering.size() > 0xFFFF) return fail(), Bytes{};
        put16(out, 2, static_cast<std::uint16_t>(out.size()));
        out.insert(out.end(), covering.begin(), covering.end());
        append_devices(out, devices, pointers);
        return ok_ ? out : Bytes{};
    }

    // Mark attachment (MarkBasePos, MarkMarkPos): marks and the bases (or marks) they sit on, each with an anchor per mark
    // class. Cursive and ligature-mark attachment are not written again: where a kept glyph can take them, the table
    // stays as it was.
    std::size_t anchor_copy(std::size_t from, Devices& pool) {
        const std::uint16_t format = word(from);
        if (format < 1 || format > 3) return fail(), 0;
        const std::size_t size = format == 1 ? 6 : format == 2 ? 8 : 10;
        if (!has(from, size)) return fail(), 0;
        Bytes copy(table_.begin() + static_cast<std::ptrdiff_t>(from), table_.begin() + static_cast<std::ptrdiff_t>(from + size));
        if (format == 3)  // its device tables after it
            for (const std::size_t slot : {std::size_t{6}, std::size_t{8}}) {
                const std::size_t device = word(from + slot);
                if (!device) continue;
                const std::size_t length = device_size(from + device);
                if (!ok_ || !has(from + device, length)) return fail(), 0;
                put16(copy, slot, static_cast<std::uint16_t>(copy.size()));
                copy.insert(copy.end(), table_.begin() + static_cast<std::ptrdiff_t>(from + device),
                            table_.begin() + static_cast<std::ptrdiff_t>(from + device + length));
            }
        const auto [found, added] = pool.at.emplace(copy, pool.bytes.size());
        if (added) pool.bytes.insert(pool.bytes.end(), copy.begin(), copy.end());
        return found->second;
    }
    Bytes attachment(std::uint16_t type, std::size_t at) {
        const auto any_kept = [&](std::size_t coverage_at) {
            const auto glyphs = coverage(coverage_at);
            return std::ranges::any_of(glyphs, [&](std::uint16_t glyph) { return is_kept(glyph); });
        };
        if (type == 3) return any_kept(at + word(at + 2)) ? (fail(), Bytes{}) : Bytes{};
        if (type < 4 || type > 6 || word(at) != 1) return fail(), Bytes{};
        const auto first_glyphs = coverage(at + word(at + 2)), second_glyphs = coverage(at + word(at + 4));
        const std::size_t classes = word(at + 6), first_array = at + word(at + 8), second_array = at + word(at + 10);
        std::vector<std::size_t> firsts, seconds;  // what is kept: by index in its coverage
        std::vector<std::uint16_t> covered_firsts, covered_seconds;
        for (std::size_t i = 0; i < first_glyphs.size(); ++i)
            if (is_kept(first_glyphs[i])) firsts.push_back(i), covered_firsts.push_back(first_glyphs[i]);
        for (std::size_t i = 0; i < second_glyphs.size(); ++i)
            if (is_kept(second_glyphs[i])) seconds.push_back(i), covered_seconds.push_back(second_glyphs[i]);
        if (!ok_ || firsts.empty() || seconds.empty()) return {};
        if (type == 5) return fail(), Bytes{};
        if (word(first_array) != first_glyphs.size() || word(second_array) != second_glyphs.size() || !has(first_array + 2, first_glyphs.size() * 4) ||
            !has(second_array + 2, second_glyphs.size() * classes * 2))
            return fail(), Bytes{};
        Bytes out;
        push16(out, 1), push16(out, 0), push16(out, 0), push16(out, static_cast<std::uint16_t>(classes)), push16(out, 0), push16(out, 0);
        struct AnchorPointer {
            std::size_t where, array, anchor;  // the offset's place, the array it is from, the anchor in the pool
        };
        std::vector<AnchorPointer> pointers;
        Devices anchors;
        put16(out, 8, static_cast<std::uint16_t>(out.size()));
        const std::size_t first_start = out.size();
        push16(out, static_cast<std::uint16_t>(firsts.size()));
        for (const std::size_t i : firsts) {
            push16(out, word(first_array + 2 + i * 4));
            pointers.push_back({out.size(), first_start, anchor_copy(first_array + word(first_array + 2 + i * 4 + 2), anchors)});
            push16(out, 0);
        }
        put16(out, 10, static_cast<std::uint16_t>(out.size()));
        const std::size_t second_start = out.size();
        push16(out, static_cast<std::uint16_t>(seconds.size()));
        for (const std::size_t i : seconds)
            for (std::size_t k = 0; k < classes && ok_; ++k) {
                const std::size_t anchor = word(second_array + 2 + (i * classes + k) * 2);
                if (anchor) pointers.push_back({out.size(), second_start, anchor_copy(second_array + anchor, anchors)});
                push16(out, 0);
            }
        const Bytes first_covering = coverage_table(covered_firsts), second_covering = coverage_table(covered_seconds);
        put16(out, 2, static_cast<std::uint16_t>(out.size()));
        out.insert(out.end(), first_covering.begin(), first_covering.end());
        put16(out, 4, static_cast<std::uint16_t>(out.size()));
        out.insert(out.end(), second_covering.begin(), second_covering.end());
        const std::size_t pool = out.size();
        out.insert(out.end(), anchors.bytes.begin(), anchors.bytes.end());
        for (const AnchorPointer& pointer : pointers) {
            if (pool + pointer.anchor - pointer.array > 0xFFFF) return fail(), Bytes{};
            put16(out, pointer.where, static_cast<std::uint16_t>(pool + pointer.anchor - pointer.array));
        }
        return ok_ && pool <= 0xFFFF ? out : (fail(), Bytes{});
    }

    // A lookup as the cut needs it: its flags and mark filtering set as they were, its subtables written again.
    struct Lookup {
        std::uint16_t type = 0;       // as it was (9: an extension)
        std::uint16_t real_type = 0;  // of its subtables
        std::uint16_t flags = 0;
        std::optional<std::uint16_t> mark_set;
        std::vector<Bytes> subtables;
    };
    Lookup read_lookup(std::size_t at) {
        Lookup lookup;
        lookup.type = lookup.real_type = word(at);
        lookup.flags = word(at + 2);
        const std::size_t count = word(at + 4), extra = (lookup.flags & 0x10) ? 2 : 0;
        if (!ok_ || !has(at + 6, count * 2 + extra)) return fail(), lookup;
        if (extra) lookup.mark_set = word(at + 6 + count * 2);
        for (std::size_t i = 0; i < count && ok_; ++i) {
            std::size_t subtable = at + word(at + 6 + i * 2);
            if (lookup.type == 9) {
                if (word(subtable) != 1 || (i > 0 && word(subtable + 2) != lookup.real_type)) return fail(), lookup;
                lookup.real_type = word(subtable + 2);
                subtable += dword(subtable + 4);
            }
            Bytes built;
            if (lookup.real_type == 1 || lookup.real_type == 2) {
                const auto format = word(subtable);
                if (format != 1 && format != 2) return fail(), lookup;
                built = lookup.real_type == 1 ? single_values(subtable) : format == 1 ? pair_glyphs(subtable) : pair_classes(subtable);
            } else built = attachment(lookup.real_type, subtable);
            if (!built.empty()) lookup.subtables.push_back(std::move(built));
        }
        return lookup;
    }
    // The lookups behind their list: each with its subtables after it, and an extension (8 bytes, 32-bit offsets) where
    // the lookup was one or the subtables would not reach; or, `extensions`, every one an extension, all subtables last.
    // Nothing when an offset does not fit.
    Bytes lookup_list(const std::vector<Lookup>& lookups, bool extensions) {
        Bytes list, bodies;
        push16(list, static_cast<std::uint16_t>(lookups.size()));
        list.resize(2 + lookups.size() * 2);
        std::vector<std::pair<std::size_t, std::size_t>> pointers;  // an extension's place, the subtable's place in `bodies`
        for (std::size_t i = 0; i < lookups.size(); ++i) {
            const Lookup& lookup = lookups[i];
            const std::size_t start = list.size(), extra = lookup.mark_set ? 2 : 0, header = 6 + lookup.subtables.size() * 2 + extra;
            if (start > 0xFFFF) return {};
            put16(list, 2 + i * 2, static_cast<std::uint16_t>(start));
            bool direct = !extensions && lookup.type != 9;
            if (direct) {
                std::size_t reaching = header;
                for (const Bytes& subtable : lookup.subtables) {
                    if (reaching > 0xFFFF) direct = false;
                    reaching += subtable.size();
                }
            }
            push16(list, direct ? lookup.real_type : std::uint16_t{9}), push16(list, lookup.flags);
            push16(list, static_cast<std::uint16_t>(lookup.subtables.size()));
            std::size_t place = header, index = 0;
            for (const Bytes& subtable : lookup.subtables) {
                const std::size_t reaches = direct ? place : header + index * 8;  // the subtable, or the extension that points to it
                if (reaches > 0xFFFF) return {};
                push16(list, static_cast<std::uint16_t>(reaches));
                place += subtable.size(), ++index;
            }
            if (lookup.mark_set) push16(list, *lookup.mark_set);
            if (direct) {
                for (const Bytes& subtable : lookup.subtables) list.insert(list.end(), subtable.begin(), subtable.end());
            } else {
                for (const Bytes& subtable : lookup.subtables) {
                    pointers.emplace_back(list.size(), bodies.size());
                    push16(list, 1), push16(list, lookup.real_type), push32(list, 0);
                    bodies.insert(bodies.end(), subtable.begin(), subtable.end());
                }
            }
        }
        for (const auto& [extension, body] : pointers) put32(list, extension + 4, static_cast<std::uint32_t>(list.size() - extension + body));
        list.insert(list.end(), bodies.begin(), bodies.end());
        return list;
    }
};

// GPOS written again for the kept glyphs, when that is smaller and understood.
std::optional<Bytes> pruned_gpos(std::span<const std::uint8_t> table, const std::vector<bool>& kept) {
    auto pruned = GposPruner(table, kept).run();
    if (!pruned || pruned->size() >= table.size()) return std::nullopt;
    return pruned;
}

// ── Tables indexed by glyph id that need nothing for a glyph no kept character reaches ───────────────────────────────
// Emptied in place, nothing moves: a zero costs nothing to send and the glyph is never drawn or measured. The last
// advance width of `hmtx` and the last entry of HVAR's advance mapping stand for every glyph after them, so they stay.

// `hmtx`: advance and left side bearing of each glyph (the first `metrics` have both, the rest the last advance and
// their own bearing).
Bytes emptied_hmtx(std::span<const std::uint8_t> hmtx, std::size_t metrics, const std::vector<bool>& kept) {
    Bytes out(hmtx.begin(), hmtx.end());
    const std::size_t glyphs = kept.size();
    for (std::size_t g = 0; g < glyphs; ++g) {
        if (kept[g]) continue;
        if (g < metrics) {
            if (g + 1 == metrics) continue;  // the advance every glyph after it has
            if (g * 4 + 4 <= out.size()) std::fill_n(out.begin() + static_cast<std::ptrdiff_t>(g * 4), 4, std::uint8_t{0});
        } else if (metrics * 4 + (g - metrics) * 2 + 2 <= out.size()) {
            std::fill_n(out.begin() + static_cast<std::ptrdiff_t>(metrics * 4 + (g - metrics) * 2), 2, std::uint8_t{0});
        }
    }
    return out;
}

// HVAR's advance width mapping (a DeltaSetIndexMap): the delta set of each glyph, as outer and inner index packed in 1
// to 4 bytes; a glyph past the last entry has the last. An entry of 0 is the first delta set, which exists.
Bytes emptied_hvar(std::span<const std::uint8_t> hvar, const std::vector<bool>& kept) {
    Bytes out(hvar.begin(), hvar.end());
    if (hvar.size() < 20 || u16(hvar, 0) != 1) return out;
    const std::size_t map = u32(hvar, 8);
    if (!map || map + 4 > hvar.size()) return out;
    const std::uint8_t format = hvar[map];
    const std::uint8_t entry_format = hvar[map + 1];
    const std::size_t size = ((entry_format >> 4) & 3) + 1;
    const std::size_t count = format == 0 ? u16(hvar, map + 2) : format == 1 ? u32(hvar, map + 2) : 0;
    const std::size_t entries = map + (format == 0 ? 4 : 6);
    if (!count || entries + count * size > hvar.size()) return out;
    for (std::size_t g = 0; g + 1 < count && g < kept.size(); ++g)
        if (!kept[g]) std::fill_n(out.begin() + static_cast<std::ptrdiff_t>(entries + g * size), size, std::uint8_t{0});
    return out;
}

}  // namespace

std::span<const std::string_view> shaping_features() noexcept {
    // What HarfBuzz turns on by itself for text in a line (its common and horizontal features, the direction's forms and
    // the variable fonts' substitutions), and the tabular figures omui's text asks for (tnum).
    static constexpr std::array<std::string_view, 20> features{"rvrn", "ccmp", "locl", "mark", "mkmk", "rlig", "calt", "clig", "curs", "dist",
                                                               "kern", "liga", "rclt", "ltra", "ltrm", "rtla", "rtlm", "abvm", "blwm", "tnum"};
    return features;
}

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

std::expected<std::vector<std::pair<std::uint32_t, std::uint32_t>>, std::string> font_blocks(std::span<const std::uint8_t> font) {
    if (font.size() < 12) return std::unexpected("not a font");
    std::span<const std::uint8_t> cmap;
    for (std::uint16_t i = 0, count = u16(font, 4); i < count; ++i) {
        const std::size_t rec = 12 + std::size_t{i} * 16;
        if (rec + 16 > font.size()) return std::unexpected("truncated font table directory");
        if (std::string_view(reinterpret_cast<const char*>(font.data() + rec), 4) == "cmap") {
            const std::size_t at = u32(font, rec + 8), len = u32(font, rec + 12);
            if (at + len > font.size()) return std::unexpected("cmap lies outside the file");
            cmap = font.subspan(at, len);
        }
    }
    if (cmap.empty()) return std::unexpected("no cmap");
    // Each Unicode subtable's ranges, by block.
    std::set<std::uint32_t> blocks;
    for (std::uint16_t i = 0, n = u16(cmap, 2); i < n; ++i) {
        const std::size_t rec = 4 + std::size_t{i} * 8;
        const std::uint16_t platform = u16(cmap, rec), encoding = u16(cmap, rec + 2);
        if (!(platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10)))) continue;
        const std::size_t at = u32(cmap, rec + 4);
        const std::uint16_t format = u16(cmap, at);
        if (format == 12) {
            for (std::uint32_t g = 0, groups = u32(cmap, at + 12); g < groups; ++g) {
                const std::size_t group = at + 16 + std::size_t{g} * 12;
                const std::uint32_t last = std::min<std::uint32_t>(u32(cmap, group + 4), 0x10FFFF);
                for (std::uint32_t block = u32(cmap, group) >> 8; block <= last >> 8; ++block) blocks.insert(block);
            }
        } else if (format == 4) {
            const std::size_t segs = u16(cmap, at + 6) / 2, ends = at + 14, starts = ends + segs * 2 + 2;
            for (std::size_t seg = 0; seg < segs; ++seg) {
                const std::uint16_t end = u16(cmap, ends + seg * 2), start = u16(cmap, starts + seg * 2);
                if (start == 0xFFFF) continue;
                for (std::uint32_t block = start >> 8; block <= std::uint32_t{end} >> 8; ++block) blocks.insert(block);
            }
        }
    }
    std::vector<std::pair<std::uint32_t, std::uint32_t>> out;
    for (const std::uint32_t block : blocks)
        if (!out.empty() && out.back().second + 1 == block) out.back().second = block;
        else out.emplace_back(block, block);
    return out;
}

std::expected<Bytes, std::string> subset_font(std::span<const std::uint8_t> font, const std::set<char32_t>& keep, FontSubsetReport* report,
                                              std::span<const std::string_view> features) {
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
    // Only the given features' lookups: the glyphs the others would make are not kept, and the others are taken out.
    const auto reached = by.contains("GSUB") ? substitutions.reached(features) : std::nullopt;
    Bytes new_gsub;
    if (reached) new_gsub = substitutions.without_other_features(features, *reached);
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
            if ((!reached || (*reached)[rule.lookup]) && std::ranges::all_of(rule.inputs, [&](auto id) { return kept[id]; }))
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

    // The metrics of the glyphs that are not kept emptied, in place (hmtx; HVAR's advance mapping).
    Bytes new_hmtx, new_hvar;
    if (by.contains("hmtx") && by.contains("hhea") && by["hhea"].size() >= 36) new_hmtx = emptied_hmtx(by["hmtx"], u16(by["hhea"], 34), kept);
    if (by.contains("HVAR")) new_hvar = emptied_hvar(by["HVAR"], kept);

    // GPOS again, for the kept glyphs, when it comes out smaller and is understood.
    Bytes new_gpos;
    if (by.contains("GPOS"))
        if (auto pruned = pruned_gpos(by["GPOS"], kept)) new_gpos = std::move(*pruned);

    // The file again, its tables in their order, each on four bytes.
    std::vector<std::pair<std::string, std::span<const std::uint8_t>>> out_tables;
    for (const Table& t : tables) {
        if (t.tag == "glyf") out_tables.push_back({t.tag, new_glyf});
        else if (t.tag == "loca") out_tables.push_back({t.tag, new_loca});
        else if (t.tag == "head") out_tables.push_back({t.tag, new_head});
        else if (t.tag == "cmap") out_tables.push_back({t.tag, new_cmap});
        else if (t.tag == "gvar") out_tables.push_back({t.tag, new_gvar});
        else if (t.tag == "GPOS" && !new_gpos.empty()) out_tables.push_back({t.tag, new_gpos});
        else if (t.tag == "GSUB" && !new_gsub.empty()) out_tables.push_back({t.tag, new_gsub});
        else if (t.tag == "hmtx" && !new_hmtx.empty()) out_tables.push_back({t.tag, new_hmtx});
        else if (t.tag == "HVAR" && !new_hvar.empty()) out_tables.push_back({t.tag, new_hvar});
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

}  // namespace mlibs::text::subset
