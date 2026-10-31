// Conformance against the UCD's own test files (a committed subset; see
// tests/data), plus UTF-8 decoding and caret navigation.
import std;
import openmui.unicode;
import openmui.testing;

using namespace omui::testing;
namespace uc = omui::unicode;

namespace {

struct Case {
    std::string              text;
    std::vector<std::size_t> breaks;  // byte offsets with ÷ before them (and the end)
    std::string              line;
};

std::vector<Case> load(std::string_view name) {
    auto file = std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data" / std::string(name);
    std::ifstream in(file);
    std::vector<Case> out;
    for (std::string line; std::getline(in, line);) {
        if (line.empty() || line[0] == '#') continue;
        Case c;
        c.line = line;
        std::istringstream words(line);
        bool brk = false;
        for (std::string w; words >> w;) {
            if (w == "÷") brk = true;
            else if (w == "×") brk = false;
            else {
                if (brk) c.breaks.push_back(c.text.size());
                uc::append_utf8(c.text, static_cast<char32_t>(std::stoul(w, nullptr, 16)));
            }
        }
        if (brk) c.breaks.push_back(c.text.size());
        out.push_back(std::move(c));
    }
    return out;
}

template <class F>
void conformance(std::string_view file, F compute, bool include_start) {
    auto cases = load(file);
    check(cases.size() > 100, std::format("{} cases", cases.size()));
    int failed = 0;
    for (const Case& c : cases) {
        std::vector<std::size_t> got = compute(c.text);
        std::vector<std::size_t> want = c.breaks;
        if (!include_start) std::erase(want, std::size_t{0});
        if (got != want) {
            if (++failed <= 5) fail(std::format("{}: {}", file, c.line));
        }
    }
    std::println("    {}: {} of {} pass", file, cases.size() - static_cast<std::size_t>(failed), cases.size());
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"unicode"};

    s.test("simple case mapping (UnicodeData.txt)", [] {
        check(uc::simple_upper(U'a') == U'A' && uc::simple_lower(U'Z') == U'z');
        check(uc::simple_upper(U'\u00E4') == U'\u00C4', "a with diaeresis");
        check(uc::simple_lower(U'\u0100') == U'\u0101' && uc::simple_lower(U'\u0101') == U'\u0101', "Latin Extended-A pairs (stride 2)");
        check(uc::simple_upper(U'\u03C3') == U'\u03A3' && uc::simple_upper(U'\u03C2') == U'\u03A3', "both sigmas");
        check(uc::simple_lower(U'\u0416') == U'\u0436', "Cyrillic");
        check(uc::simple_lower(U'\u0130') == U'i', "dotted capital I: its simple lowercase is i");
        check(uc::simple_upper(U'\u00DF') == U'\u00DF', "sharp s has no simple uppercase");
        check(uc::simple_upper(U'\u4E2D') == U'\u4E2D' && uc::simple_lower(U'1') == U'1', "uncased");
        check(uc::simple_lower(U'\U00010400') == U'\U00010428', "Deseret, beyond the BMP");
        check(uc::upper("a\xFF" "b") == std::string("A\xFF" "B"), "invalid bytes pass through");
        // Every mapping round trips where Unicode says it does: upper(lower(x)) for x = upper(x).
        int pairs = 0, broken = 0;
        for (char32_t c = 0; c < 0x110000; ++c) {
            const char32_t l = uc::simple_lower(c);
            if (l == c) continue;
            ++pairs;
            if (uc::simple_upper(l) != c && uc::simple_lower(uc::simple_upper(l)) != l) ++broken;
        }
        check(pairs > 1400 && broken == 0, std::format("{} lowercase mappings, {} broken", pairs, broken));
    });

    s.test("full case mapping (SpecialCasing.txt, Final_Sigma)", [] {
        check_eq(uc::upper("Grüße, мир, σοφία"), std::string("GRÜSSE, МИР, ΣΟΦΊΑ"));
        check_eq(uc::upper("ﬁﬀ ŉ ΐ"), std::string("FIFF \u02BCN \u0399\u0308\u0301"));
        check_eq(uc::lower("İstanbul"), std::string("i̇stanbul"));
        check_eq(uc::lower("ÀÉÎ ΣΟΦΙΑ"), std::string("àéî σοφια"));
        // Σ lowers to ς at the end of a word, to σ elsewhere.
        check_eq(uc::lower("ΟΔΥΣΣΕΥΣ"), std::string("οδυσσευς"));
        check_eq(uc::lower("ΣΑΣ. Σ"), std::string("σας. σ"));
        check_eq(uc::lower("ΑΣ'Β ΑΣ'"), std::string("ασ'β ας'"));  // ' is case-ignorable: skipped both ways
        check_eq(uc::lower("1Σ"), std::string("1σ"));
        // Every unconditional full mapping is applied.
        check_eq(uc::upper("\u0587"), std::string("\u0535\u0552"));  // Armenian ech-yiwn
        check_eq(uc::lower("\u1F88"), std::string("\u1F80"));
        check_eq(uc::upper("\u1F80"), std::string("\u1F08\u0399"));
    });

    s.test("grapheme clusters (UAX #29 conformance)", [] {
        conformance("GraphemeBreakTest.txt", [](std::string_view t) { return uc::grapheme_boundaries(t); }, true);
    });

    s.test("word boundaries (UAX #29 conformance)", [] {
        conformance("WordBreakTest.txt", [](std::string_view t) { return uc::word_boundaries(t); }, true);
    });

    s.test("line breaking (UAX #14 conformance)", [] {
        conformance("LineBreakTest.txt", [](std::string_view t) {
            std::vector<std::size_t> out;
            for (auto b : uc::line_breaks(t)) out.push_back(b.offset);
            return out;
        }, false);
    });

    s.test("mandatory breaks", [] {
        auto b = uc::line_breaks("ab\ncd\r\nef");
        check(b.size() == 3);
        check(b[0].offset == 3 && b[0].mandatory);
        check(b[1].offset == 7 && b[1].mandatory);
        check(b[2].offset == 9 && b[2].mandatory);
        auto words = uc::line_breaks("hello world");
        check(words.size() == 2 && words[0].offset == 6 && !words[0].mandatory);
        auto cjk = uc::line_breaks("中文排版");
        check_eq(cjk.size(), std::size_t{4}, std::source_location::current());
    });

    s.test("UTF-8 decoding", [] {
        check(uc::decode("\xF0\x9F\x98\x80", 0).cp == 0x1F600);
        check(uc::decode("\xF0\x9F\x98\x80", 0).length == 4);
        check(uc::decode("\xC0\xAF", 0).cp == 0xFFFD, "overlong");
        check(uc::decode("\xED\xA0\x80", 0).cp == 0xFFFD, "surrogate");
        check(uc::decode("\xE2\x82", 0).length == 2, "maximal subpart");
        check(uc::to_utf32("a\xFF" "b") == std::u32string{U'a', 0xFFFD, U'b'});
        std::string s;
        uc::append_utf8(s, 0x10FFFF);
        check(s == "\xF4\x8F\xBF\xBF");
        check_eq(uc::previous("a\xE4\xB8\xAD", 4), std::size_t{1});
    });

    s.test("carets step over clusters", [] {
        std::string family = "a\U0001F468‍\U0001F469‍\U0001F467b";  // a 👨‍👩‍👧 b
        check_eq(uc::grapheme_count(family), std::size_t{3});
        auto after_a = uc::next_grapheme(family, 0);
        auto after_family = uc::next_grapheme(family, after_a);
        check_eq(after_family, family.size() - 1);
        check_eq(uc::previous_grapheme(family, family.size() - 1), std::size_t{1});
        std::string flags = "\U0001F1E8\U0001F1F3\U0001F1FA\U0001F1F8";  // 🇨🇳🇺🇸
        check_eq(uc::grapheme_count(flags), std::size_t{2});
        check_eq(uc::next_grapheme("x\r\ny", 1), std::size_t{3});
        auto w = uc::word_at("hello, world", 9);
        check(w.first == 7 && w.second == 12);
    });

    return s.run(argc, argv);
}
