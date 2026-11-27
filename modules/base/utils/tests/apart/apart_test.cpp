// mlibs.apart: a mark written before a region, found again among other data — and what only looks like one is not.
import std;
import mlibs.apart;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace {
std::vector<std::uint8_t> bytes_of(std::string_view text) { return {text.begin(), text.end()}; }
}  // namespace

UTest cases{"apart", [] {
    "two marked regions among other data, each found with its kind and its place"_test = [] {
        const std::string first = mlibs::apart::mark("wgsl", 5) + "fn a;";
        const std::string second = mlibs::apart::mark("essl", 3) + "x;y";
        const std::string data = std::string("lead\0\0", 6) + first + std::string("middle", 6) + second + "tail";
        const auto found = mlibs::apart::regions(bytes_of(data));
        expect(eq(found.size(), std::size_t{2}));
        if (found.size() != 2) return;
        expect(eq(found[0].kind, std::string("wgsl")) && eq(found[0].mark, std::size_t{6}) && eq(data.substr(found[0].at, found[0].length), std::string("fn a;")));
        expect(eq(found[1].kind, std::string("essl")) && eq(data.substr(found[1].at, found[1].length), std::string("x;y")));
        expect(eq(found[1].mark, first.size() + 12));
    };
    "a mark's bytes are its prefix, its kind and its length, each ended by a NUL"_test = [] {
        expect(eq(mlibs::apart::mark("wgsl", 1234), std::string("\0omui.apart\0wgsl\0" "1234\0", 22)));
        expect(mlibs::apart::mark("x", 0).starts_with(mlibs::apart::prefix));
    };
    "what is not a mark: no kind, no length, a length past the end"_test = [] {
        const std::string prefix(mlibs::apart::prefix);
        expect(mlibs::apart::regions(bytes_of(prefix + std::string("\0" "3\0abc", 6))).empty());
        expect(mlibs::apart::regions(bytes_of(prefix + std::string("wgsl\0\0abc", 9))).empty());
        expect(mlibs::apart::regions(bytes_of(prefix + std::string("wgsl\0" "9\0abc", 10))).empty());
        expect(mlibs::apart::regions(bytes_of(prefix + std::string("w g\0" "1\0a", 7))).empty());
        expect(mlibs::apart::regions({}).empty());
    };
    "a region's bytes are not searched for marks of their own"_test = [] {
        const std::string inner = mlibs::apart::mark("essl", 1) + "z";
        const std::string data = mlibs::apart::mark("wgsl", inner.size()) + inner;
        const auto found = mlibs::apart::regions(bytes_of(data));
        expect(eq(found.size(), std::size_t{1}) && found[0].kind == "wgsl");
    };
}};
