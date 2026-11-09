// Streaming append against the whole-source parse: for every example of the
// conformance suites, and for long documents made of them, appending the
// source in random pieces gives exactly the document parse() gives, after
// every piece; and a long document freezes, so what an append re-parses stays
// small (the point of streaming).
import std;
import mlibs.math;
import mlibs.markdown;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace md = mlibs::markdown;

namespace {

std::filesystem::path data_dir() { return std::filesystem::path(std::source_location::current().file_name()).parent_path() / "data"; }

// The markdown of every example in a suite (test/run-testsuite.py's reader).
std::vector<std::string> sources(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::vector<std::string> out;
    std::string cur;
    int state = 0;
    const std::string fence(32, '`');
    for (std::string raw; std::getline(in, raw);) {
        std::string_view l = raw;
        while (!l.empty() && (l.front() == ' ' || l.front() == '\t')) l.remove_prefix(1);
        while (!l.empty() && (l.back() == ' ' || l.back() == '\t' || l.back() == '\r')) l.remove_suffix(1);
        if (l.starts_with(fence + " example")) {
            state = 1;
            cur.clear();
        } else if (state >= 2 && l == fence) {
            state = 0;
            for (std::size_t p; (p = cur.find("\xE2\x86\x92")) != std::string::npos;) cur.replace(p, 3, "\t");
            out.push_back(cur);
        } else if (l == ".") {
            ++state;
        } else if (state == 1) {
            cur += raw + "\n";
        }
    }
    return out;
}

// The same "random" numbers with every standard library: std::shuffle and
// std::uniform_int_distribution are implementation-defined (MSVC's STL and
// libstdc++ shuffle differently), so the documents and pieces are this.
struct Rng {
    std::uint64_t s;
    std::uint64_t next() noexcept {
        s += mlibs::math::golden;
        return mlibs::math::finalize(s);
    }
    std::size_t below(std::size_t n) noexcept { return static_cast<std::size_t>(next() % n); }
};

template <class T>
void shuffle(std::vector<T>& v, Rng& r) {
    for (std::size_t i = v.size(); i > 1; --i) std::swap(v[i - 1], v[r.below(i)]);
}

std::vector<std::string> all_sources() {
    std::vector<std::string> out;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(data_dir()))
        if (e.path().extension() == ".txt") files.push_back(e.path());
    std::ranges::sort(files);
    for (const auto& f : files) std::ranges::move(sources(f), std::back_inserter(out));
    return out;
}

// Appends `source` in pieces of 1 to `max` bytes and compares with parse()
// of the prefix after every piece; returns the bytes md4c read in total.
// The bytes parsed incrementally, or where the streamed document first differs from parse().
std::expected<std::size_t, std::string> stream_matches(std::string_view source, Rng& rng, std::size_t max, md::Extension ext, std::string_view what) {
    md::Stream s(ext);
    std::size_t pos = 0, parsed = 0;
    while (pos < source.size()) {
        const std::size_t n = std::min(1 + rng.below(max), source.size() - pos);
        s.append(source.substr(pos, n));
        pos += n;
        parsed += s.last_parsed();
        const md::Document whole = md::parse(source.substr(0, pos), ext);
        if (!(s.document() == whole))
            return std::unexpected(std::format("{}: the streamed document differs from parse() after {} of {} bytes (frozen at {})\n--- prefix\n{}", what,
                                               pos, source.size(), s.frozen(), source.substr(0, pos)));
    }
    return parsed;
}

}  // namespace

UTest cases{"markdown.stream", [] {

    "every suite example, appended in random pieces, equals the whole parse after every piece"_test = [] {
        const auto examples = all_sources();
        expect(eq(examples.size(), std::size_t{808}));
        Rng rng{20261027};
        for (std::size_t i = 0; i < examples.size(); ++i) {
            const auto streamed = stream_matches(examples[i], rng, 7, md::gfm, std::format("example {}", i));
            expect(fatal(streamed.has_value())) << (streamed ? std::string() : streamed.error());
        }
    };

    "long documents of examples (no reference definitions): equal, and they freeze"_test = [] {
        auto examples = all_sources();
        std::erase_if(examples, [](const std::string& e) { return e.find("]:") != std::string::npos; });
        Rng rng{7};
        for (int round = 0; round < 12; ++round) {
            shuffle(examples, rng);
            std::string doc;
            // Blank lines between examples, as separate blocks would be.
            for (std::size_t i = 0; i < 60; ++i) doc += examples[i] + "\n";
            md::Stream probe;
            probe.assign(doc);
            const auto streamed = stream_matches(doc, rng, 64, md::gfm, std::format("round {}", round));
            expect(fatal(streamed.has_value())) << (streamed ? std::string() : streamed.error());
            const std::size_t parsed = *streamed;
            expect(!probe.whole()) << "no reference definitions, no whole re-parse";
            expect(probe.stable_blocks() > 0) << std::format("round {}: some blocks froze", round);
        }
    };

    "reference definitions anywhere: the stream re-parses whole and still equals parse()"_test = [] {
        const std::string doc = "Intro [foo] and [bar].\n\n# Heading\n\n- a\n- b\n\nmore text\n\n[foo]: /foo \"Foo\"\n\n[bar]: /bar\n";
        Rng rng{3};
        const auto streamed = stream_matches(doc, rng, 5, md::gfm, "definitions");
        expect(streamed.has_value()) << (streamed ? std::string() : streamed.error());
        md::Stream s;
        s.assign(doc);
        expect(s.whole());
        expect(eq(s.document().links.size(), std::size_t{2}));
        expect(eq(s.document().links[0].href, std::string("/foo")));
    };

    "an LLM-style answer streamed token by token re-parses only its tail"_test = [] {
        std::string answer;
        for (int i = 0; i < 200; ++i)
            answer += std::format("## Step {}\n\nWe compute **{}** with `f({})`, then check it:\n\n```cpp\nauto x = f({});\n\nassert(x > 0);\n```\n\n", i, i * i, i, i);
        md::Stream s;
        std::size_t parsed = 0, pos = 0;
        while (pos < answer.size()) {
            const std::size_t n = std::min<std::size_t>(4, answer.size() - pos);  // a token is a few bytes
            s.append(std::string_view(answer).substr(pos, n));
            pos += n;
            parsed += s.last_parsed();
        }
        expect(s.document() == md::parse(answer));
        const double per_append = static_cast<double>(parsed) / (static_cast<double>(answer.size()) / 4);
        std::println("    {} bytes in {} appends: {:.0f} bytes parsed per append (whole re-parse: {:.0f})", answer.size(), answer.size() / 4,
                     per_append, static_cast<double>(answer.size()) / 2);
        expect(per_append < 400) << "an append re-parses about one block, not the answer";
        expect(s.stable_blocks() + 3 >= s.document().root.children.size()) << "all but the last blocks are frozen";
        // A blank line inside the fenced code is no boundary: the code stays one block.
        const auto& blocks = s.document().root.children;
        expect(eq(std::ranges::count_if(blocks, [](const md::Block& b) { return b.kind == md::Kind::code; }), std::ptrdiff_t{200}));
    };

    "assign and clear start over"_test = [] {
        md::Stream s;
        s.append("# One\n\ntext\n\n");
        s.assign("# Two\n");
        expect(s.document() == md::parse("# Two\n"));
        expect(eq(s.document().root.children[0].anchor, std::string("two")));
        const auto r = s.revision();
        s.clear();
        expect(s.document().root.children.empty() && s.source().empty() && s.revision() > r);
    };
}};
