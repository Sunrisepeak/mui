module openmui.gen.unicode;

import std;
import openmui.gen.model;  // utf8, generic_utf8

namespace omui::gen {

namespace {

constexpr std::uint32_t code_points = 0x110000;

std::string_view trim(std::string_view s) {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r')) s.remove_suffix(1);
    return s;
}

std::optional<std::pair<std::uint32_t, std::uint32_t>> parse_range(std::string_view r) {
    r = trim(r);
    auto dots = r.find("..");
    auto hex = [](std::string_view h) -> std::optional<std::uint32_t> {
        std::uint32_t v = 0;
        auto [p, ec] = std::from_chars(h.data(), h.data() + h.size(), v, 16);
        if (ec != std::errc{} || p != h.data() + h.size()) return std::nullopt;
        return v;
    };
    auto lo = hex(dots == std::string_view::npos ? r : r.substr(0, dots));
    auto hi = hex(dots == std::string_view::npos ? r : r.substr(dots + 2));
    if (!lo || !hi) return std::nullopt;
    return std::pair{*lo, *hi};
}

// Assignments of a property file: "@missing" defaults first (in file order),
// then the explicit ranges. `field` selects the value column (1 for most).
struct Assignment {
    std::uint32_t lo, hi;
    std::string   value;
};

std::expected<std::vector<Assignment>, std::string> read_property(const std::filesystem::path& file, int field = 1) {
    std::ifstream in(file);
    if (!in) return std::unexpected(std::format("cannot read {}", generic_utf8(file)));
    std::vector<Assignment> defaults, explicit_;
    for (std::string line; std::getline(in, line);) {
        std::string_view l = line;
        bool missing = false;
        if (l.starts_with("# @missing:")) {
            l.remove_prefix(11);
            missing = true;
        } else {
            auto hash = l.find('#');
            if (hash != std::string_view::npos) l = l.substr(0, hash);
        }
        l = trim(l);
        if (l.empty()) continue;
        std::vector<std::string_view> fields;
        while (true) {
            auto semi = l.find(';');
            fields.push_back(trim(l.substr(0, semi)));
            if (semi == std::string_view::npos) break;
            l.remove_prefix(semi + 1);
        }
        if (static_cast<int>(fields.size()) <= field) continue;
        auto range = parse_range(fields[0]);
        if (!range) continue;
        (missing ? defaults : explicit_).push_back({range->first, range->second, std::string(fields[static_cast<std::size_t>(field)])});
    }
    defaults.insert(defaults.end(), explicit_.begin(), explicit_.end());
    return defaults;
}

// The code points listed (for binary properties: the ranges whose value is `name`).
std::vector<bool> flag(const std::vector<Assignment>& a, std::string_view name) {
    std::vector<bool> out(code_points, false);
    for (const Assignment& x : a)
        if (x.value == name)
            for (std::uint32_t c = x.lo; c <= x.hi && c < code_points; ++c) out[c] = true;
    return out;
}

struct Table {
    std::vector<std::uint16_t> stage1;  // block index per 256 code points
    std::vector<std::uint8_t>  stage2;  // concatenated unique blocks
};

Table compress(const std::vector<std::uint8_t>& values) {
    Table t;
    std::map<std::vector<std::uint8_t>, std::uint16_t> seen;
    for (std::uint32_t b = 0; b < code_points / 256; ++b) {
        std::vector<std::uint8_t> block(values.begin() + b * 256, values.begin() + (b + 1) * 256);
        auto [it, inserted] = seen.emplace(block, static_cast<std::uint16_t>(seen.size()));
        if (inserted) t.stage2.insert(t.stage2.end(), block.begin(), block.end());
        t.stage1.push_back(it->second);
    }
    return t;
}

void emit_table(std::string& out, std::string_view name, const Table& t) {
    out += std::format("inline constexpr std::array<std::uint16_t, {}> {}_stage1{{{{", t.stage1.size(), name);
    for (std::size_t i = 0; i < t.stage1.size(); ++i) out += std::format("{}{}", i == 0 ? "\n    " : (i % 24 ? "," : ",\n    "), t.stage1[i]);
    out += "\n}};\n";
    out += std::format("inline constexpr std::array<std::uint8_t, {}> {}_stage2{{{{", t.stage2.size(), name);
    for (std::size_t i = 0; i < t.stage2.size(); ++i) out += std::format("{}{}", i == 0 ? "\n    " : (i % 32 ? "," : ",\n    "), t.stage2[i]);
    out += "\n}};\n\n";
}

std::string enum_text(std::string_view name, const std::vector<std::string>& values, std::string_view doc) {
    std::string out = std::format("// {}\nenum class {} : std::uint8_t {{", doc, name);
    for (std::size_t i = 0; i < values.size(); ++i) out += std::format("{}{}", i == 0 ? "\n    " : (i % 8 ? ", " : ",\n    "), values[i]);
    out += ",\n};\n\n";
    return out;
}

}  // namespace

// Simple case mappings of UnicodeData.txt (field 12 uppercase, 13 lowercase)
// as runs of code points that map by the same delta, consecutive (stride 1)
// or alternating (stride 2: the paired upper and lower case letters of
// Latin Extended-A and its like).
struct CaseRun {
    std::uint32_t first = 0, last = 0, stride = 1;
    std::int64_t  delta = 0;
};

std::expected<std::array<std::vector<CaseRun>, 2>, std::string> read_case_runs(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot read {}", generic_utf8(file)));
    std::array<std::vector<std::pair<std::uint32_t, std::int64_t>>, 2> maps;  // upper, lower
    for (std::string line; std::getline(in, line);) {
        std::vector<std::string_view> f;
        std::string_view rest = line;
        for (std::size_t p; (p = rest.find(';')) != std::string_view::npos; rest.remove_prefix(p + 1)) f.push_back(rest.substr(0, p));
        f.push_back(rest);
        if (f.size() < 14) continue;
        std::uint32_t cp = 0;
        if (std::from_chars(f[0].data(), f[0].data() + f[0].size(), cp, 16).ec != std::errc{}) continue;
        for (int k = 0; k < 2; ++k) {
            const std::string_view m = trim(f[k == 0 ? 12 : 13]);
            std::uint32_t to = 0;
            if (m.empty() || std::from_chars(m.data(), m.data() + m.size(), to, 16).ec != std::errc{}) continue;
            maps[static_cast<std::size_t>(k)].emplace_back(cp, static_cast<std::int64_t>(to) - static_cast<std::int64_t>(cp));
        }
    }
    std::array<std::vector<CaseRun>, 2> out;
    for (std::size_t k = 0; k < 2; ++k) {
        std::vector<CaseRun>& runs = out[k];
        for (const auto& [cp, delta] : maps[k]) {
            if (!runs.empty()) {
                CaseRun& r = runs.back();
                const std::uint32_t gap = cp - r.last;
                const bool single = r.first == r.last;
                if (r.delta == delta && ((single && (gap == 1 || gap == 2)) || (!single && gap == r.stride))) {
                    if (single) r.stride = gap;
                    r.last = cp;
                    continue;
                }
            }
            runs.push_back({cp, cp, 1, delta});
        }
    }
    return out;
}

// SpecialCasing.txt's unconditional full mappings: one code point to up to
// three (ß → SS, İ → i̇, ﬃ → FFI). The conditional entries are left to code:
// Final_Sigma, the one that is not a language's, needs the context (case.cpp);
// the language-specific ones (lt, tr, az) need a locale these functions do
// not take.
struct CaseSpecial {
    std::uint32_t                cp = 0;
    std::vector<std::uint32_t>   to;
};

std::expected<std::array<std::vector<CaseSpecial>, 2>, std::string> read_special_casing(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(std::format("cannot read {}", generic_utf8(file)));
    std::array<std::vector<CaseSpecial>, 2> out;  // upper, lower
    auto code_points_of = [](std::string_view list) -> std::optional<std::vector<std::uint32_t>> {
        std::vector<std::uint32_t> cps;
        list = trim(list);
        while (!list.empty()) {
            const std::size_t sp = list.find(' ');
            const std::string_view h = list.substr(0, sp);
            std::uint32_t v = 0;
            if (std::from_chars(h.data(), h.data() + h.size(), v, 16).ec != std::errc{}) return std::nullopt;
            cps.push_back(v);
            list = sp == std::string_view::npos ? std::string_view{} : trim(list.substr(sp + 1));
        }
        return cps;
    };
    for (std::string line; std::getline(in, line);) {
        std::string_view l = line;
        if (const auto hash = l.find('#'); hash != std::string_view::npos) l = l.substr(0, hash);
        std::vector<std::string_view> f;
        for (std::size_t p; (p = l.find(';')) != std::string_view::npos; l.remove_prefix(p + 1)) f.push_back(trim(l.substr(0, p)));
        if (f.size() != 4) continue;  // a comment, or a fifth field: a condition
        auto cp = code_points_of(f[0]);
        if (!cp || cp->size() != 1) return std::unexpected(std::format("SpecialCasing: bad code point `{}`", f[0]));
        for (int k = 0; k < 2; ++k) {
            auto to = code_points_of(f[k == 0 ? 3 : 1]);
            if (!to || to->empty() || to->size() > 3) return std::unexpected(std::format("SpecialCasing: bad mapping of {:04X}", (*cp)[0]));
            if (to->size() == 1 && (*to)[0] == (*cp)[0]) continue;  // maps to itself: nothing special
            out[static_cast<std::size_t>(k)].push_back({(*cp)[0], std::move(*to)});
        }
    }
    for (auto& v : out) std::ranges::sort(v, {}, &CaseSpecial::cp);
    return out;
}

std::expected<std::string, std::string> emit_unicode_tables(const std::filesystem::path& ucd, std::string_view version) {
    auto cases = read_case_runs(ucd / "UnicodeData.txt");
    if (!cases) return std::unexpected(cases.error());
    auto special = read_special_casing(ucd / "SpecialCasing.txt");
    if (!special) return std::unexpected(special.error());
    auto core = read_property(ucd / "DerivedCoreProperties.txt");
    auto lb = read_property(ucd / "LineBreak.txt");
    auto ea = read_property(ucd / "EastAsianWidth.txt");
    auto gcb = read_property(ucd / "GraphemeBreakProperty.txt");
    auto wb = read_property(ucd / "WordBreakProperty.txt");
    auto emoji = read_property(ucd / "emoji-data.txt");
    auto incb = read_property(ucd / "DerivedCoreProperties.txt", 2);
    auto gc = read_property(ucd / "DerivedGeneralCategory.txt");
    for (auto* r : {&lb, &ea, &gcb, &wb, &emoji, &incb, &gc, &core})
        if (!*r) return std::unexpected(r->error());

    // Value sets: the line-break classes and word-break values as the data
    // names them, sorted, so the enumerations are stable for a UCD version.
    auto values_of = [](const std::vector<Assignment>& a, std::string fallback) {
        std::set<std::string> s{fallback};
        for (const Assignment& x : a) s.insert(x.value);
        return std::vector<std::string>(s.begin(), s.end());
    };
    std::vector<std::string> lb_values = values_of(*lb, "XX");
    std::vector<std::string> gcb_values{"Other", "CR", "LF", "Control", "Extend", "ZWJ", "Regional_Indicator",
                                        "Prepend", "SpacingMark", "L", "V", "T", "LV", "LVT"};
    std::vector<std::string> wb_values = values_of(*wb, "Other");
    std::vector<std::string> ea_values{"N", "A", "F", "H", "Na", "W"};

    auto index_in = [](const std::vector<std::string>& v, const std::string& s) -> int {
        auto it = std::ranges::find(v, s);
        return it == v.end() ? -1 : static_cast<int>(it - v.begin());
    };
    auto fill = [&](const std::vector<Assignment>& a, const std::vector<std::string>& values, std::string_view fallback,
                    std::vector<std::uint8_t>& out, int shift, int mask) -> std::expected<void, std::string> {
        int def = index_in(values, std::string(fallback));
        for (std::uint32_t c = 0; c < code_points; ++c) out[c] = static_cast<std::uint8_t>(out[c] | (def << shift));
        for (const Assignment& x : a) {
            int v = index_in(values, x.value);
            if (v < 0) return std::unexpected(std::format("unexpected value {}", x.value));
            for (std::uint32_t c = x.lo; c <= x.hi && c < code_points; ++c)
                out[c] = static_cast<std::uint8_t>((out[c] & ~(mask << shift)) | (v << shift));
        }
        return {};
    };

    std::vector<std::uint8_t> line(code_points, 0), grapheme(code_points, 0), word(code_points, 0);
    if (auto r = fill(*lb, lb_values, "XX", line, 0, 0xff); !r) return std::unexpected(r.error());
    if (auto r = fill(*gcb, gcb_values, "Other", grapheme, 0, 0x0f); !r) return std::unexpected(r.error());
    if (auto r = fill(*wb, wb_values, "Other", word, 0, 0x1f); !r) return std::unexpected(r.error());
    if (auto r = fill(*ea, ea_values, "N", word, 5, 0x07); !r) return std::unexpected(r.error());
    auto pict = flag(*emoji, "Extended_Pictographic");
    auto cn = flag(*gc, "Cn");
    // InCB is field 2 of DerivedCoreProperties' "InCB" lines: "range ; InCB ; Linker".
    std::vector<std::uint8_t> incb_of(code_points, 0);
    for (const Assignment& x : *incb) {
        int v = x.value == "Linker" ? 1 : x.value == "Consonant" ? 2 : x.value == "Extend" ? 3 : 0;
        if (v == 0) continue;
        for (std::uint32_t c = x.lo; c <= x.hi && c < code_points; ++c) incb_of[c] = static_cast<std::uint8_t>(v);
    }
    for (std::uint32_t c = 0; c < code_points; ++c) {
        grapheme[c] = static_cast<std::uint8_t>(grapheme[c] | (pict[c] ? 0x10 : 0) | (incb_of[c] << 5) | (cn[c] ? 0x80 : 0));
    }
    // LB1 for SA (South-East Asian scripts, without a dictionary): marks
    // (Mn, Mc) become CM, everything else AL — resolved here because it needs
    // the general category. Pi and Pf (initial and final punctuation) qualify
    // QU in LB15a, LB15b and LB19.
    auto mn = flag(*gc, "Mn"), mc = flag(*gc, "Mc"), pi = flag(*gc, "Pi"), pf = flag(*gc, "Pf");
    int sa = index_in(lb_values, "SA"), cm = index_in(lb_values, "CM"), al = index_in(lb_values, "AL");
    for (std::uint32_t c = 0; c < code_points; ++c) {
        if (line[c] == sa) line[c] = static_cast<std::uint8_t>((mn[c] || mc[c]) ? cm : al);
        line[c] = static_cast<std::uint8_t>(line[c] | (pi[c] ? 0x40 : 0) | (pf[c] ? 0x80 : 0));
    }

    // Cased and Case_Ignorable, the context of Final_Sigma (Unicode §3.13).
    std::vector<std::uint8_t> casing(code_points, 0);
    {
        auto cased = flag(*core, "Cased"), ignorable = flag(*core, "Case_Ignorable");
        for (std::uint32_t c = 0; c < code_points; ++c) casing[c] = static_cast<std::uint8_t>((cased[c] ? 1 : 0) | (ignorable[c] ? 2 : 0));
    }

    std::string out = std::format(R"(// Generated by omui-gen from the Unicode Character Database {0}. Do not edit:
//     omui-gen unicode <ucd-{0}> --out modules/unicode/src/generated
//
// Two-stage tables: stage1[cp >> 8] is a block, stage2[block * 256 + (cp & 255)]
// the value. Packing:
//   line      bits 0-5 the Line_Break class (enum Lb; SA already resolved
//             to CM or AL by LB1), bit 6 General_Category = Pi, bit 7 Pf
//   grapheme  bits 0-3 Grapheme_Cluster_Break (enum Gcb), bit 4
//             Extended_Pictographic, bits 5-6 Indic_Conjunct_Break
//             (1 Linker, 2 Consonant, 3 Extend), bit 7 General_Category = Cn
//   word      bits 0-4 Word_Break (enum Wb), bits 5-7 East_Asian_Width (enum Ea)
//   casing    bit 0 Cased, bit 1 Case_Ignorable (DerivedCoreProperties.txt)
//
// Simple case mappings (UnicodeData.txt fields 12 and 13): sorted runs of code
// points that map by one delta, every `stride`th code point from `first` to
// `last`. Full mappings (SpecialCasing.txt, unconditional): sorted by code
// point, `count` code points of `to`.
export module openmui.unicode:tables;

import std;

export namespace omui::unicode {{

inline constexpr std::string_view ucd_version = "{0}";

)",
                                  version);
    out += enum_text("Lb", lb_values, "Line_Break classes (UAX #14).");
    out += enum_text("Gcb", gcb_values, "Grapheme_Cluster_Break values (UAX #29).");
    out += enum_text("Wb", wb_values, "Word_Break values (UAX #29).");
    out += enum_text("Ea", ea_values, "East_Asian_Width values (UAX #11).");
    out += "}  // namespace omui::unicode\n\nexport namespace omui::unicode::tables {\n\n";
    emit_table(out, "line", compress(line));
    emit_table(out, "grapheme", compress(grapheme));
    emit_table(out, "word", compress(word));
    emit_table(out, "casing", compress(casing));
    out += "struct CaseRun {\n    char32_t      first, last;\n    std::uint8_t  stride;\n    std::int32_t  delta;\n};\n\n";
    for (std::size_t k = 0; k < 2; ++k) {
        const auto& runs = (*cases)[k];
        out += std::format("inline constexpr std::array<CaseRun, {}> {}{{{{\n", runs.size(), k == 0 ? "to_upper" : "to_lower");
        for (std::size_t i = 0; i < runs.size(); ++i)
            out += std::format("    {{0x{:04X}, 0x{:04X}, {}, {}}},{}", runs[i].first, runs[i].last, runs[i].stride, runs[i].delta,
                               (i % 3 == 2 || i + 1 == runs.size()) ? "\n" : " ");
        out += "}};\n\n";
    }
    out += "struct CaseSpecial {\n    char32_t                cp;\n    std::uint8_t            count;\n    std::array<char32_t, 3> to;\n};\n\n";
    for (std::size_t k = 0; k < 2; ++k) {
        const auto& list = (*special)[k];
        out += std::format("inline constexpr std::array<CaseSpecial, {}> {}{{{{\n", list.size(), k == 0 ? "full_upper" : "full_lower");
        for (const CaseSpecial& c : list) {
            std::string to;
            for (std::size_t i = 0; i < 3; ++i) to += std::format("{}0x{:04X}", i ? ", " : "", i < c.to.size() ? c.to[i] : 0u);
            out += std::format("    {{0x{:04X}, {}, {{{}}}}},\n", c.cp, c.to.size(), to);
        }
        out += "}};\n\n";
    }
    out += R"([[nodiscard]] constexpr std::uint8_t lookup(const auto& stage1, const auto& stage2, char32_t cp) noexcept {
    if (cp >= 0x110000) cp = 0xFFFD;
    return stage2[static_cast<std::size_t>(stage1[cp >> 8]) * 256 + (cp & 255)];
}

}  // namespace omui::unicode::tables
)";
    return out;
}

std::expected<std::string, std::string> unicode_test_subset(const std::filesystem::path& file, int step) {
    std::ifstream in(file);
    if (!in) return std::unexpected(std::format("cannot read {}", generic_utf8(file)));
    std::string out = std::format("# Every {}th test line of {} (omui-gen unicode-tests).\n", step, utf8(file.filename()));
    int n = 0;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line.starts_with('#')) continue;
        if (n++ % step == 0) {
            auto hash = line.find('#');
            out += line.substr(0, hash);
            out += '\n';
        }
    }
    return out;
}

}  // namespace omui::gen
