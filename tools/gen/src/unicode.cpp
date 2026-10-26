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

std::expected<std::string, std::string> emit_unicode_tables(const std::filesystem::path& ucd, std::string_view version) {
    auto lb = read_property(ucd / "LineBreak.txt");
    auto ea = read_property(ucd / "EastAsianWidth.txt");
    auto gcb = read_property(ucd / "GraphemeBreakProperty.txt");
    auto wb = read_property(ucd / "WordBreakProperty.txt");
    auto emoji = read_property(ucd / "emoji-data.txt");
    auto incb = read_property(ucd / "DerivedCoreProperties.txt", 2);
    auto gc = read_property(ucd / "DerivedGeneralCategory.txt");
    for (auto* r : {&lb, &ea, &gcb, &wb, &emoji, &incb, &gc})
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
