// Case mapping: a binary search of the generated runs and full mappings
// (tables.cppm), and Final_Sigma's context (Unicode §3.13).
module openmui.unicode;

import std;
import :tables;

namespace omui::unicode {

namespace {

char32_t mapped(std::span<const tables::CaseRun> runs, char32_t cp) noexcept {
    auto it = std::ranges::upper_bound(runs, cp, {}, &tables::CaseRun::first);
    if (it == runs.begin()) return cp;
    const tables::CaseRun& r = *(it - 1);
    if (cp > r.last || (cp - r.first) % r.stride != 0) return cp;
    return static_cast<char32_t>(static_cast<std::int64_t>(cp) + r.delta);
}

void append_utf8(std::string& out, char32_t cp) {
    if (cp < 0x80) out += static_cast<char>(cp);
    else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

const tables::CaseSpecial* special(std::span<const tables::CaseSpecial> list, char32_t cp) noexcept {
    auto it = std::ranges::lower_bound(list, cp, {}, &tables::CaseSpecial::cp);
    return it != list.end() && it->cp == cp ? &*it : nullptr;
}

bool cased(char32_t cp) noexcept { return tables::lookup(tables::casing_stage1, tables::casing_stage2, cp) & 1; }
bool case_ignorable(char32_t cp) noexcept { return tables::lookup(tables::casing_stage1, tables::casing_stage2, cp) & 2; }

// Whether the capital sigma at [at, at + 2) ends a word: a cased letter
// before it and none after, case-ignorable code points skipped both ways.
bool final_sigma(std::string_view text, std::size_t at) {
    bool before = false;
    for (std::size_t i = at; i > 0;) {
        i = previous(text, i);
        const char32_t c = decode(text, i).cp;
        if (case_ignorable(c)) continue;
        before = cased(c);
        break;
    }
    if (!before) return false;
    for (std::size_t i = at + 2; i < text.size();) {
        const Decoded d = decode(text, i);
        if (!case_ignorable(d.cp)) return !cased(d.cp);
        i += d.length;
    }
    return true;
}

// `upper`: which table; every code point mapped (invalid UTF-8 copied as it is).
std::string map_text(std::string_view text, bool upper) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size();) {
        const Decoded d = decode(text, i);
        // A replacement character that is not literally U+FFFD was invalid
        // input: copied as it is.
        if (d.cp == 0xFFFD && text.substr(i, d.length) != "\xEF\xBF\xBD") {
            out.append(text.substr(i, d.length));
        } else if (const auto* s = special(upper ? std::span<const tables::CaseSpecial>(tables::full_upper)
                                                 : std::span<const tables::CaseSpecial>(tables::full_lower), d.cp)) {
            for (std::size_t k = 0; k < s->count; ++k) append_utf8(out, s->to[k]);
        } else if (!upper && d.cp == 0x03A3 && final_sigma(text, i)) {
            append_utf8(out, 0x03C2);  // ς
        } else {
            append_utf8(out, upper ? simple_upper(d.cp) : simple_lower(d.cp));
        }
        i += d.length;
    }
    return out;
}

}  // namespace

char32_t simple_upper(char32_t cp) noexcept { return mapped(tables::to_upper, cp); }
char32_t simple_lower(char32_t cp) noexcept { return mapped(tables::to_lower, cp); }

std::string upper(std::string_view text) { return map_text(text, true); }
std::string lower(std::string_view text) { return map_text(text, false); }

}  // namespace omui::unicode
