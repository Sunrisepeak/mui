// mlibs.error: a domain's codes described once; one text for every error;
// causes kept beneath, in order; a structured location; the kinds named as
// openas names its error codes.
import std;
import mlibs.error;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace err = mlibs::error;

namespace demo {
enum class Errc : std::uint8_t { bad, gone };
constexpr err::Description describe(Errc e) noexcept {
    switch (e) {
        case Errc::bad: return {"demo.bad", err::Kind::invalid_argument, "a bad value"};
        case Errc::gone: return {"demo.gone", err::Kind::not_found};
    }
    return {"demo.?", err::Kind::internal};
}
struct Cell {
    int x = 0, y = 0;
};
std::string describe_where(const Cell& c) { return std::format("cell ({}, {})", c.x, c.y); }
}  // namespace demo

namespace outer {
enum class Errc : std::uint8_t { failed, inner };
constexpr err::Description describe(Errc e) noexcept {
    if (e == Errc::inner) return {"outer.inner", err::Kind::failed, {}, true};
    return {"outer.failed", err::Kind::failed};
}
}  // namespace outer

UTest cases{"error", [] {

    "a code is described once: its name, its kind, its text"_test = [] {
        const err::Error<demo::Errc> e{demo::Errc::bad};
        expect(eq(std::string(e.description().name), std::string("demo.bad")));
        expect(e.kind() == err::Kind::invalid_argument) << "kind";
        expect(eq(e.text(), std::string("demo.bad (invalid-argument): a bad value")));
        const err::Error<demo::Errc> m{demo::Errc::gone, "no such node", "/view/root"};
        expect(eq(m.text(), std::string("demo.gone (not-found) at /view/root: no such node")));
    };

    "a domain may keep a structured location; it formats into the text"_test = [] {
        const err::Error<demo::Errc, demo::Cell> e{demo::Errc::bad, {}, {3, 4}};
        expect(e.where.x == 3 && e.where.y == 4) << "the location is the domain's own";
        expect(eq(e.text(), std::string("demo.bad (invalid-argument) at cell (3, 4): a bad value")));
        expect(eq(e.cause().where, std::string("cell (3, 4)")));
    };

    "causes are kept beneath, outermost first, with theirs"_test = [] {
        const err::Error<demo::Errc, demo::Cell> inner{demo::Errc::gone, "the texture", {1, 2}};
        err::Error<outer::Errc> middle{outer::Errc::failed, "a pass"};
        middle.because(inner);
        const auto top = err::Error<demo::Errc>{demo::Errc::bad, "a frame", "track 2"}.because(middle);
        expect(eq(top.causes.size(), std::size_t{2}));
        expect(eq(top.causes[0].code, std::string("outer.failed")));
        expect(eq(top.causes[1].code, std::string("demo.gone")));
        expect(eq(top.text(), std::string("demo.bad (invalid-argument) at track 2: a frame; because outer.failed (failed): a pass; "
                                         "because demo.gone (not-found) at cell (1, 2): the texture")));
    };

    "the kinds are openas's error codes, by the same names (SPEC §6.3)"_test = [] {
        constexpr std::array<std::pair<err::Kind, std::string_view>, 8> openas{{{err::Kind::invalid_argument, "invalid-argument"},
                                                                              {err::Kind::not_found, "not-found"},
                                                                              {err::Kind::refused, "refused"},
                                                                              {err::Kind::stale, "stale"},
                                                                              {err::Kind::timeout, "timeout"},
                                                                              {err::Kind::needs_answer, "needs-answer"},
                                                                              {err::Kind::failed, "failed"},
                                                                              {err::Kind::internal, "internal"}}};
        for (const auto& [k, n] : openas) expect(eq(std::string(err::name(k)), std::string(n)));
    };

    "a code that wraps takes its kind from the error beneath it"_test = [] {
        auto e = err::Error<outer::Errc>{outer::Errc::inner};
        expect(e.kind() == err::Kind::failed) << "nothing beneath: its own kind";
        e.because(err::Error<demo::Errc>{demo::Errc::gone});
        expect(e.kind() == err::Kind::not_found) << "the cause's kind";
        expect(err::Error<outer::Errc>{outer::Errc::failed}.because(demo::Errc::gone).kind() == err::Kind::failed) << "a code that does not wrap keeps its own";
    };

    "a cause is found by its code, a bare code too"_test = [] {
        const auto e = err::Error<outer::Errc>{outer::Errc::inner}.because(err::Error<demo::Errc, demo::Cell>{demo::Errc::bad, {}, {5, 6}});
        expect(e.caused_by(demo::Errc::bad) && !e.caused_by(demo::Errc::gone)) << "by code";
        expect(eq(e.cause_of(demo::Errc::bad)->where, std::string("cell (5, 6)")));
        const auto bare = err::Error<outer::Errc>{outer::Errc::inner}.because(demo::Errc::gone);
        expect(bare.caused_by(demo::Errc::gone) && bare.kind() == err::Kind::not_found) << "a bare code is a cause";
        expect(eq(bare.text(), std::string("outer.inner (not-found); because demo.gone (not-found)")));
    };

    "an error is a Result's error"_test = [] {
        const auto f = [](int v) -> err::Result<int, demo::Errc> {
            if (v < 0) return std::unexpected(err::Error<demo::Errc>{demo::Errc::bad, std::format("{} is negative", v)});
            return v * 2;
        };
        expect(f(2) == 4) << "a value";
        expect(eq(f(-1).error().text(), std::string("demo.bad (invalid-argument): -1 is negative")));
    };
}};
