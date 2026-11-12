// mlibs.cts on a toy subject: passed, failed, unsupported from the
// capabilities alone, a required capability failing — counted in the process
// and as mtest's own cases — and the comparisons.
import std;
import mlibs.cts;
import mlibs.error;
import mcpplibs.mtest;

// mtest by name: its `cts` is not this one.
using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::eq, mcpplibs::mtest::fatal, mcpplibs::mtest::operator""_test;
namespace cts = mlibs::cts;

namespace {

struct Toy {
    std::vector<std::byte> bytes{std::byte{1}, std::byte{2}, std::byte{3}};
    int                    runs = 0;
    int                    expected_runs = 2;  // what toy.runs holds the count to
};

// The toy's suite, against a toy that has what `has` says.
void declare(Toy& toy, cts::Against who) {
    cts::against(std::move(who), [&toy] {
        cts::declare({.id = "toy.bytes", .contract = "toy@1", .level = cts::Level::exact, .needs = {},
                      .body = [&toy](cts::Check& c) { ++toy.runs; c.that(cts::exact(toy.bytes, toy.bytes).same, "the same bytes"); }});
        cts::declare({.id = "toy.runs", .contract = "toy@1", .level = cts::Level::semantic, .needs = {},
                      .body = [&toy](cts::Check& c) { ++toy.runs; c.equal(toy.runs, toy.expected_runs, "runs"); }});
        cts::declare({.id = "toy.gpu", .contract = "toy@1", .level = cts::Level::tolerance, .needs = {"gpu"}, .body = [&toy](cts::Check&) { ++toy.runs; }});
        cts::declare({.id = "toy.compute", .contract = "toy@1", .level = cts::Level::exact, .needs = {"compute"}, .body = [&toy](cts::Check&) { ++toy.runs; }});
    });
}

// A toy engine for the preparation contract: a description is a count; a
// negative one is invalid, one above the budget is refused. `leaky` publishes
// what it can before it refuses a budget — the cases must catch it.
enum class ToyErrc : std::uint8_t { negative, budget };
constexpr mlibs::error::Description describe(ToyErrc e) noexcept {
    return e == ToyErrc::negative ? mlibs::error::Description{"toy.negative", mlibs::error::Kind::invalid_argument}
                                  : mlibs::error::Description{"toy.budget", mlibs::error::Kind::refused};
}
struct Counts {
    using Description = int;
    bool             leaky = false;
    std::vector<int> published;
    std::expected<std::vector<int>, mlibs::error::Error<ToyErrc>> prepare(int n) {
        if (n < 0) return std::unexpected(mlibs::error::Error<ToyErrc>{ToyErrc::negative});
        if (n > 10) {
            if (leaky) published.push_back(10);
            return std::unexpected(mlibs::error::Error<ToyErrc>{ToyErrc::budget, std::format("{} > 10", n)});
        }
        std::vector<int> out(static_cast<std::size_t>(n));
        std::iota(out.begin(), out.end(), 0);
        return out;
    }
    std::string fingerprint(const std::vector<int>& v) { return std::format("{}", v.size()); }
    std::string state() { return std::format("{}", published.size()); }
    std::string execute(const std::vector<int>& v) noexcept { return std::format("{}", std::accumulate(v.begin(), v.end(), 0)); }
};

}  // namespace

UTest cases{"cts", [] {

    "counted in the process: a case passes or fails by its checks; unsupported comes from capabilities and never runs"_test = [] {
        Toy toy{.expected_runs = 100};
        const auto r = cts::tally([&] { declare(toy, {.subject = "toy", .has = [](std::string_view capability) { return capability == "copy"; }, .required = {"compute"}}); });
        expect(fatal(eq(r.cases.size(), std::size_t{5})));
        expect(r.cases[0].id == "requires compute" && r.cases[0].status == cts::Status::failed) << "what the run requires and the subject lacks fails, once";
        expect(r.cases[1].status == cts::Status::passed) << "toy.bytes passed";
        expect(r.cases[2].status == cts::Status::failed && !r.cases[2].failures.empty()) << "toy.runs failed with its failure";
        expect(r.cases[2].failures[0].why == "runs: expected 100, got 2") << r.cases[2].failures[0].why;
        expect(r.cases[3].status == cts::Status::unsupported && r.cases[3].missing == std::vector<std::string>{"gpu"}) << "toy.gpu unsupported";
        expect(r.cases[4].status == cts::Status::unsupported) << "toy.compute did not run either";
        expect(eq(toy.runs, 2));  // the unsupported cases never ran
        expect(eq(r.count(cts::Status::passed), std::size_t{1}));
        expect(eq(r.count(cts::Status::unsupported), std::size_t{2}));
        expect(!r.ok()) << "a failed case: not ok";
        expect(r.ids(cts::Status::failed) == std::vector<std::string>{"requires compute", "toy.runs"});
        expect(r.find("toy.gpu") && r.find("toy.gpu")->level == cts::Level::tolerance && r.find("toy.gpu")->subject == "toy");
        expect(r.text().contains("toy: 1 passed, 2 failed, 2 unsupported")) << r.text();
    };

    "a subject that says nothing of what it has is taken to have everything; one that has nothing passes nothing it needs"_test = [] {
        Toy toy{.expected_runs = 2};
        const auto all = cts::tally([&] { declare(toy, {.subject = "toy"}); });
        expect(all.ok() && eq(all.count(cts::Status::passed), std::size_t{4})) << all.text();
        Toy bare{.expected_runs = 2};
        const auto none = cts::tally([&] { declare(bare, {.subject = "bare", .has = [](std::string_view) { return false; }}); });
        expect(none.ok() && eq(none.count(cts::Status::passed), std::size_t{2}) && eq(none.count(cts::Status::unsupported), std::size_t{2})) << none.text();
    };

    // The same suite as this program's own cases (mtest's): toy/toy.bytes and toy/toy.runs pass, toy/toy.gpu is
    // skipped as `unsupported: gpu` — the runner's report says so, and the count of cases with it.
    Toy toy{.expected_runs = 2};
    declare(toy, {.subject = "toy", .has = [](std::string_view capability) { return capability != "gpu"; }});
    "declared as cases of mtest, a suite's cases ran where they were declared"_test = [&toy] { expect(eq(toy.runs, 3)) << "bytes, runs and compute; gpu did not"; };

    "comparisons say what differs where"_test = [] {
        const std::array<std::byte, 4> a{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40}};
        const std::array<std::byte, 4> b{std::byte{10}, std::byte{21}, std::byte{30}, std::byte{44}};
        const auto e = cts::exact(a, b);
        expect(!e.same && e.first == 1 && e.largest == 4 && e.beyond == 2) << e.text();
        expect(!cts::within(a, b, 1, 0).same && cts::within(a, b, 1, 1).same) << "a tolerance and a budget";
        expect(cts::within(a, b, 4, 0).same) << "within ±4";
        expect(!cts::exact(a, std::span(b).first(3)).same) << "sizes differ";
        // Pictures: 2 × 1, the second pixel's blue off by 3.
        const std::array<std::byte, 8> p{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{255}, std::byte{4}, std::byte{5}, std::byte{6}, std::byte{255}};
        auto q = p;
        q[6] = std::byte{9};
        const auto d = cts::image({p, 2, 1, 8}, {q, 2, 1, 8}, 1);
        expect(!d.same && d.x == 1 && d.y == 0 && d.largest == 3 && d.beyond == 1) << d.text();
        expect(cts::image({p, 2, 1, 8}, {q, 2, 1, 8}, 3).same) << "within ±3";
        expect(!cts::image({p, 2, 1, 8}, {p, 1, 2, 4}).same) << "sizes differ";
    };

    "pinned hashes and sampled processes record their evidence"_test = [] {
        cts::Check c;
        const std::string_view text = "foobar";
        const auto bytes = std::as_bytes(std::span(text));
        expect(cts::pinned(c, bytes, "85944171f73967e8", "foobar")) << "FNV-1a of foobar";
        expect(!cts::pinned(c, bytes, "0000000000000000", "foobar") && c.failed()) << "a wrong pin fails";
        cts::Check p;
        const std::array<double, 3> times{0.0, 0.5, 1.0};
        auto frame = [](double t) {
            std::vector<std::byte> f(4, std::byte{static_cast<unsigned char>(t * 100)});
            return f;
        };
        std::vector<std::string> pins;
        for (double t : times) pins.push_back(cts::hash(frame(t)));
        const std::array<std::string_view, 3> views{pins[0], pins[1], pins[2]};
        expect(cts::sampled(p, times, frame, views, "fade") && !p.failed()) << "every sample as pinned";
        expect(eq(p.evidence().size(), std::size_t{3}));
        expect(p.evidence()[1].first == "fade at 0.5 s") << p.evidence()[1].first;
    };

    "the preparation contract: accepted twice alike, refused by kind, nothing partly published"_test = [] {
        const cts::prepare::Samples<int> samples{.valid = {{"three", 3}, {"none", 0}}, .invalid = {{"negative", -1}}, .over_budget = {{"eleven", 11}}};
        Counts sound;
        const auto r = cts::tally([&] { cts::against({.subject = "sound"}, [&] { cts::prepare::declare(sound, "toy@1", samples); }); });
        expect(fatal(eq(r.cases.size(), std::size_t{4})));
        expect(r.ok()) << r.text();
        expect(eq(r.cases[0].id, std::string("prepare.three.accepted")));
        expect(eq(r.cases[3].id, std::string("prepare.eleven.over-budget")));
        Counts leaky{.leaky = true};
        const auto l = cts::tally([&] { cts::against({.subject = "leaky"}, [&] { cts::prepare::declare(leaky, "toy@1", samples); }); });
        expect(!l.ok() && l.cases[3].status == cts::Status::failed) << "a refusal that publishes part fails";
        expect(l.cases[3].failures[0].why.contains("the state after the refusal")) << l.text();
    };
}};
