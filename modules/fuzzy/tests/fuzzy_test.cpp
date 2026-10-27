// Fuzzy matching as a person expects it: in order or not at all; word
// starts, camel humps and runs before scattered letters; smart case; every
// word of the query; the ranking deterministic, and the same on many
// threads as on one.
import std;
import openmui.fuzzy;
import openmui.testing;

using namespace omui::testing;
namespace fuzzy = omui::fuzzy;

namespace {

std::int32_t score(std::string_view q, std::string_view c) {
    auto m = fuzzy::match(q, c);
    return m ? m->score : -1;
}

std::string shown(const fuzzy::Match& m) {
    std::string out;
    for (const auto& s : m.segments) out += s.matched ? "[" + s.text + "]" : s.text;
    return out;
}

std::vector<std::string> names(std::span<const fuzzy::Match> ms, std::span<const std::string> c) {
    std::vector<std::string> out;
    for (const auto& m : ms) out.push_back(c[m.index]);
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"fuzzy"};

    s.test("in order, or not at all; an empty query matches everything", [] {
        check(!fuzzy::match("ba", "abc").has_value(), "b before a is not in abc");
        check(!fuzzy::match("abcd", "abc").has_value());
        auto m = fuzzy::match("", "anything");
        check(m.has_value() && m->score == 0 && m->positions.empty());
        check(fuzzy::match("ac", "abc").has_value());
    });

    s.test("what matched, as positions and as runs to highlight", [] {
        auto m = fuzzy::match("fb", "foo_bar");
        check(m.has_value());
        if (!m) return;
        check(m->positions == std::vector<std::uint32_t>{0, 4});
        check_eq(shown(*m), std::string("[f]oo_[b]ar"));
        auto u = fuzzy::match("née", "Renée.txt");  // code points, not bytes
        check(u.has_value() && u->positions == std::vector<std::uint32_t>{2, 3, 4}, u ? shown(*u) : "");
        check_eq(u ? shown(*u) : std::string(), std::string("Re[née].txt"));
    });

    s.test("word starts, camel humps and runs come before scattered letters", [] {
        check(score("fb", "foo_bar") > score("fb", "foobar"), "a word start");
        check(score("fb", "fooBar") > score("fb", "foobar"), "a camel hump");
        check(score("fb", "src/foo/bar") > score("fb", "srcfoobar"), "path segments");
        check(score("abc", "abc_xyz") > score("abc", "a_b_c"), "a run");
        check(score("cfg", "config") < score("cfg", "my_cfg"), "the run cfg beats c·f·g");
        // The best alignment, not the first: the later `b` starts a word.
        auto m = fuzzy::match("ab", "a_xb_b");
        check(m && m->positions == std::vector<std::uint32_t>{0, 5}, m ? shown(*m) : "");
    });

    s.test("smart case, and Unicode case", [] {
        check(fuzzy::match("readme", "README.md").has_value(), "lowercase matches either case");
        check(!fuzzy::match("ReadMe", "README.md").has_value(), "a capital asks for exactly that");
        check(fuzzy::match("ReadMe", "ReadMe.md").has_value());
        check(fuzzy::match("été", "ÉTÉ").has_value(), "é folds to É");
        check(!fuzzy::match("Été", "été").has_value());
    });

    s.test("every word of the query must match", [] {
        check(fuzzy::match("src cfg", "src/app/config.toml").has_value());
        check(!fuzzy::match("src cfg", "lib/config.toml").has_value());
        check(score("src cfg", "src/config") == score("src", "src/config") + score("cfg", "src/config"), "the words' scores add");
    });

    s.test("ranking: best first, then shorter, then earlier; a limit", [] {
        const std::vector<std::string> c{"src/main.cpp", "my_config.toml", "Makefile", "src/mcpp/config.cppm", "docs/mcfg.md", "zzz"};
        auto r = fuzzy::rank("mcfg", c, 0);
        check(r.size() == 3, std::format("{} matched", r.size()));
        check(!r.empty() && c[r[0].index] == "docs/mcfg.md", r.empty() ? "" : c[r[0].index] + ": the whole run, at a word start");
        auto top = fuzzy::rank("mcfg", c, 1);
        check(top.size() == 1 && top[0].index == r[0].index);
        const std::vector<std::string> same{"ab_c", "ab_c", "xab_c"};
        auto t = fuzzy::rank("abc", same, 0);
        check(t.size() == 3 && t[0].index == 0 && t[1].index == 1, "equal scores keep their order");
        check(!t.empty() && !t[0].segments.empty(), "the shown ones carry their runs");
        check(fuzzy::rank("", c, 2).size() == 2 && fuzzy::rank("", c, 2)[0].index == 0, "an empty query keeps the list's order");
    });

    s.test("many candidates: several threads, the same answer, pinned", [] {
        std::vector<std::string> c;
        std::uint64_t x = 0x9e3779b97f4a7c15ull;
        const std::array<std::string_view, 12> parts{"src", "lib", "test", "main", "config", "util", "view", "model", "Text", "Box", "io", "net"};
        for (int i = 0; i < 20000; ++i) {
            std::string path;
            const int depth = 1 + static_cast<int>(x % 4);
            for (int d = 0; d < depth; ++d) {
                x = x * 6364136223846793005ull + 1442695040888963407ull;
                path += (d ? "/" : "") + std::string(parts[(x >> 33) % parts.size()]);
            }
            path += std::format("_{}.cpp", i % 97);
            c.push_back(std::move(path));
        }
        auto fast = fuzzy::rank("srcvbox", c, 0);
        // One at a time, sorted the same way.
        std::vector<fuzzy::Match> slow;
        for (std::uint32_t i = 0; i < c.size(); ++i)
            if (auto m = fuzzy::match("srcvbox", c[i])) {
                m->index = i;
                slow.push_back(std::move(*m));
            }
        std::ranges::sort(slow, [&](const fuzzy::Match& a, const fuzzy::Match& b) {
            if (a.score != b.score) return a.score > b.score;
            if (c[a.index].size() != c[b.index].size()) return c[a.index].size() < c[b.index].size();
            return a.index < b.index;
        });
        check(fast.size() == slow.size() && !fast.empty(), std::format("{} and {}", fast.size(), slow.size()));
        bool same = fast.size() == slow.size();
        for (std::size_t i = 0; same && i < fast.size(); ++i)
            same = fast[i].index == slow[i].index && fast[i].score == slow[i].score && fast[i].positions == slow[i].positions;
        check(same, "the threads change nothing");
        Hash h;
        for (const auto& m : fast) {
            h.u64(m.index);
            h.u64(static_cast<std::uint64_t>(static_cast<std::int64_t>(m.score)));
            for (auto p : m.positions) h.u64(p);
        }
        std::println("fuzzy ranking {} ({} matched; best {})", h.hex(), fast.size(), names(std::span(fast).first(1), c)[0]);
        check_eq(h.hex(), std::string("e52b0d5236e85cab"));
    });

    return s.run(argc, argv);
}
