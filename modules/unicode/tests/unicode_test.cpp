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
