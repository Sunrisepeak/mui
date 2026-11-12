// The cellular simulation on the preparation contract (mlibs.cts:prepare): a
// rule set prepares to the same encoding twice and steps a field to the same
// cells; a malformed one is refused as invalid-argument, one beyond its limits
// as refused.
import std;
import xengine.cellular;
import mlibs.cts;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace ca = xengine::cellular;
namespace cts = mlibs::cts;

namespace {
struct Rules {
    struct Description {
        std::uint32_t         channels = 2;
        std::vector<ca::Rule> rules;
        ca::Limits            limits;
    };
    auto prepare(const Description& d) { return ca::Program::prepare(d.channels, d.rules, d.limits); }
    std::string fingerprint(const ca::Program& p) {
        mlibs::hash::Fnv1a h;
        for (const auto w : p.words()) h.le(w, 4);
        return std::format("{} channels, halo {}, {} rules, {:016x}", p.channels(), p.halo(), p.rule_count(), h.value());
    }
    std::string state() { return "pure"; }  // preparation holds nothing of the caller's
    // Three steps of a 9×7 field, every cell hashed.
    std::string execute(const ca::Program& p) noexcept {
        const ca::Shape shape{9, 7};
        std::vector<std::uint32_t> a(shape.width * shape.height * p.channels()), b(a.size());
        for (std::size_t i = 0; i < a.size(); ++i) a[i] = static_cast<std::uint32_t>((i * 7) % 5);
        for (int t = 0; t < 3; ++t) {
            if (!p.step(shape, a, b, {.boundary = {ca::Edge::wrap}})) return "refused";
            a.swap(b);
        }
        mlibs::hash::Fnv1a h;
        for (const auto v : a) h.le(v, 4);
        return std::format("{:016x}", h.value());
    }
};
}  // namespace

UTest cases{"cellular.cts.prepare", [] {

    "rules prepare the same way twice; malformed ones and ones beyond the limits are refused by kind"_test = [] {
        using C = ca::Condition;
        using W = ca::WriteKind;
        const std::vector<ca::Rule> grow{{{C::self({0, {1, 1}}), C::neighbors({1, {2, 4}}, ca::cardinal, {2, 4})}, {{1, W::add_saturating, 1}}},
                                         {{C::self({1, {5, 9}})}, {{0, W::set, 0}, {1, W::subtract_saturating, 3}}}};
        const std::vector<ca::Rule> copy{{{}, {{0, W::set, 4}}}};
        std::vector<ca::Rule> many(5, copy[0]);
        cts::Suite<Rules> suite{"xengine.cellular.prepare", "1"};
        cts::prepare::add(suite, "xengine.cellular@1",
                          {.valid = {{"grow", {2, grow}}, {"copy", {1, copy}}, {"identity", {3, {}}}},
                           .invalid = {{"no-channels", {0, copy}},
                                       {"channel-out-of-range", {1, {{{}, {{3, W::set, 1}}}}}},
                                       {"empty-range", {1, {{{C::self({0, {5, 1}})}, {{0, W::set, 1}}}}}},
                                       {"duplicate-write", {1, {{{}, {{0, W::set, 1}, {0, W::set, 2}}}}}},
                                       {"unsound-limits", {1, copy, {.max_words = 2}}}},
                           .over_budget = {{"rules", {1, many, {.max_rules = 4}}}, {"channels", {20, copy}}}});
        Rules rules;
        const auto report = suite.run(rules, {}, {}, {.subject = "cpu"});
        expect(report.ok()) << report.text();
        expect(eq(report.count(cts::Status::passed), std::size_t{10}));
    };
}};
