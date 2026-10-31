// UAX #29: extended grapheme clusters and word boundaries (Unicode 17).
module openmui.unicode;

import std;

namespace omui::unicode {

namespace {

enum class InCB : std::uint8_t { none, linker, consonant, extend };

InCB incb(char32_t cp) {
    return static_cast<InCB>((tables::lookup(tables::grapheme_stage1, tables::grapheme_stage2, cp) >> 5) & 3);
}

struct Cp {
    std::size_t offset;
    char32_t    cp;
};

std::vector<Cp> code_points(std::string_view text) {
    std::vector<Cp> out;
    for (std::size_t i = 0; i < text.size();) {
        auto d = decode(text, i);
        out.push_back({i, d.cp});
        i += d.length;
    }
    return out;
}

bool control_like(Gcb g) { return g == Gcb::Control || g == Gcb::CR || g == Gcb::LF; }

}  // namespace

std::vector<std::size_t> grapheme_boundaries(std::string_view text) {
    std::vector<std::size_t> out;
    if (text.empty()) return out;
    out.push_back(0);
    auto cps = code_points(text);
    Gcb prev = grapheme_class(cps[0].cp);
    // GB11: ExtPict Extend* — then a ZWJ.
    bool pict_run = extended_pictographic(cps[0].cp);
    bool pict_zwj = false;
    // GB12/13: regional indicators before the current position.
    int ri = prev == Gcb::Regional_Indicator ? 1 : 0;
    // GB9c: Consonant [Extend Linker]* with at least one Linker.
    bool consonant = incb(cps[0].cp) == InCB::consonant, linker = false;

    for (std::size_t i = 1; i < cps.size(); ++i) {
        char32_t cp = cps[i].cp;
        Gcb cur = grapheme_class(cp);
        bool pict = extended_pictographic(cp);
        InCB ic = incb(cp);
        bool join;
        if (prev == Gcb::CR && cur == Gcb::LF) join = true;                                         // GB3
        else if (control_like(prev)) join = false;                                                  // GB4
        else if (control_like(cur)) join = false;                                                   // GB5
        else if (prev == Gcb::L && (cur == Gcb::L || cur == Gcb::V || cur == Gcb::LV || cur == Gcb::LVT)) join = true;  // GB6
        else if ((prev == Gcb::LV || prev == Gcb::V) && (cur == Gcb::V || cur == Gcb::T)) join = true;                   // GB7
        else if ((prev == Gcb::LVT || prev == Gcb::T) && cur == Gcb::T) join = true;               // GB8
        else if (cur == Gcb::Extend || cur == Gcb::ZWJ) join = true;                                // GB9
        else if (cur == Gcb::SpacingMark) join = true;                                              // GB9a
        else if (prev == Gcb::Prepend) join = true;                                                 // GB9b
        else if (ic == InCB::consonant && consonant && linker) join = true;                         // GB9c
        else if (prev == Gcb::ZWJ && pict && pict_zwj) join = true;                                 // GB11
        else if (prev == Gcb::Regional_Indicator && cur == Gcb::Regional_Indicator && ri % 2 == 1) join = true;  // GB12/13
        else join = false;                                                                          // GB999
        if (!join) out.push_back(cps[i].offset);

        pict_zwj = cur == Gcb::ZWJ && pict_run;
        pict_run = pict || (pict_run && cur == Gcb::Extend);
        ri = cur == Gcb::Regional_Indicator ? ri + 1 : 0;
        if (ic == InCB::consonant) {
            consonant = true;
            linker = false;
        } else if (consonant && ic == InCB::linker) {
            linker = true;
        } else if (!(consonant && ic == InCB::extend)) {
            consonant = false;
            linker = false;
        }
        prev = cur;
    }
    out.push_back(text.size());
    return out;
}

namespace {

bool ah(Wb w) { return w == Wb::ALetter || w == Wb::Hebrew_Letter; }
bool mid_letter_q(Wb w) { return w == Wb::MidLetter || w == Wb::MidNumLet || w == Wb::Single_Quote; }
bool mid_num_q(Wb w) { return w == Wb::MidNum || w == Wb::MidNumLet || w == Wb::Single_Quote; }
bool ignorable(Wb w) { return w == Wb::Extend || w == Wb::Format || w == Wb::ZWJ; }
bool newline(Wb w) { return w == Wb::Newline || w == Wb::CR || w == Wb::LF; }

}  // namespace

std::vector<std::size_t> word_boundaries(std::string_view text) {
    std::vector<std::size_t> out;
    if (text.empty()) return out;
    out.push_back(0);
    auto cps = code_points(text);
    std::size_t n = cps.size();
    std::vector<Wb> w(n);
    for (std::size_t i = 0; i < n; ++i) w[i] = word_class(cps[i].cp);
    constexpr std::size_t none = std::numeric_limits<std::size_t>::max();
    // The effective character before index i under WB4 (skipping
    // Extend/Format/ZWJ), and the one after.
    auto before = [&](std::size_t i) -> std::size_t {
        while (i > 0) {
            --i;
            if (!ignorable(w[i])) return i;
        }
        return none;
    };
    auto after = [&](std::size_t i) -> std::size_t {
        for (++i; i < n; ++i)
            if (!ignorable(w[i])) return i;
        return none;
    };
    auto cls = [&](std::size_t i) { return i == none ? Wb::Other : w[i]; };
    for (std::size_t i = 1; i < n; ++i) {
        Wb prev = w[i - 1], cur = w[i];
        bool join;
        if (prev == Wb::CR && cur == Wb::LF) join = true;                                   // WB3
        else if (newline(prev)) join = false;                                               // WB3a
        else if (newline(cur)) join = false;                                                // WB3b
        else if (prev == Wb::ZWJ && extended_pictographic(cps[i].cp)) join = true;          // WB3c
        else if (prev == Wb::WSegSpace && cur == Wb::WSegSpace) join = true;                // WB3d
        else if (ignorable(cur)) join = true;                                               // WB4
        else {
            std::size_t p = ignorable(prev) ? before(i) : i - 1;
            // A run of ignorables at the start of text: nothing to attach to.
            Wb a = cls(p);
            Wb a2 = p == none ? Wb::Other : cls(before(p));
            Wb b2 = cls(after(i));
            if (ah(a) && ah(cur)) join = true;                                              // WB5
            else if (ah(a) && mid_letter_q(cur) && ah(b2)) join = true;                     // WB6
            else if (ah(a2) && mid_letter_q(a) && ah(cur)) join = true;                     // WB7
            else if (a == Wb::Hebrew_Letter && cur == Wb::Single_Quote) join = true;        // WB7a
            else if (a == Wb::Hebrew_Letter && cur == Wb::Double_Quote && b2 == Wb::Hebrew_Letter) join = true;  // WB7b
            else if (a2 == Wb::Hebrew_Letter && a == Wb::Double_Quote && cur == Wb::Hebrew_Letter) join = true;  // WB7c
            else if (a == Wb::Numeric && cur == Wb::Numeric) join = true;                   // WB8
            else if (ah(a) && cur == Wb::Numeric) join = true;                              // WB9
            else if (a == Wb::Numeric && ah(cur)) join = true;                              // WB10
            else if (a2 == Wb::Numeric && mid_num_q(a) && cur == Wb::Numeric) join = true;  // WB11
            else if (a == Wb::Numeric && mid_num_q(cur) && b2 == Wb::Numeric) join = true;  // WB12
            else if (a == Wb::Katakana && cur == Wb::Katakana) join = true;                 // WB13
            else if ((ah(a) || a == Wb::Numeric || a == Wb::Katakana || a == Wb::ExtendNumLet) && cur == Wb::ExtendNumLet)
                join = true;                                                                // WB13a
            else if (a == Wb::ExtendNumLet && (ah(cur) || cur == Wb::Numeric || cur == Wb::Katakana)) join = true;  // WB13b
            else if (a == Wb::Regional_Indicator && cur == Wb::Regional_Indicator) {        // WB15/16
                int count = 0;
                for (std::size_t k = p; k != none && w[k] == Wb::Regional_Indicator; k = before(k)) ++count;
                join = count % 2 == 1;
            } else join = false;                                                            // WB999
        }
        if (!join) out.push_back(cps[i].offset);
    }
    out.push_back(text.size());
    return out;
}

namespace {

// A grapheme boundary at or before `offset` to segment from: the start of
// the line (a boundary by GB4/GB5) or of the text.
std::size_t segment_start(std::string_view text, std::size_t offset) {
    if (offset < 2) return 0;
    std::size_t nl = text.rfind('\n', offset - 2);
    return nl == std::string_view::npos ? 0 : nl + 1;
}

}  // namespace

std::size_t next_grapheme(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) return text.size();
    std::size_t start = segment_start(text, offset + 1);
    for (std::size_t b : grapheme_boundaries(text.substr(start)))
        if (start + b > offset) return start + b;
    return text.size();
}

std::size_t previous_grapheme(std::string_view text, std::size_t offset) {
    if (offset == 0) return 0;
    if (offset > text.size()) offset = text.size();
    std::size_t start = segment_start(text, offset);
    std::size_t best = start;
    for (std::size_t b : grapheme_boundaries(text.substr(start))) {
        if (start + b >= offset) break;
        best = start + b;
    }
    return best;
}

std::size_t grapheme_count(std::string_view text) {
    auto b = grapheme_boundaries(text);
    return b.empty() ? 0 : b.size() - 1;
}

std::pair<std::size_t, std::size_t> word_at(std::string_view text, std::size_t offset) {
    auto b = word_boundaries(text);
    for (std::size_t i = 0; i + 1 < b.size(); ++i)
        if (offset >= b[i] && offset < b[i + 1]) return {b[i], b[i + 1]};
    return {text.size(), text.size()};
}

}  // namespace omui::unicode
