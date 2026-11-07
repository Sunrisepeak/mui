// mlibs.cts on a toy subject: passed, failed, unsupported from the
// capabilities alone, a required capability failing, the report and the
// comparisons.
import std;
import nlohmann.json;
import mlibs.cts;
import mlibs.error;
import mlibs.testing;

using namespace mlibs::testing;
namespace cts = mlibs::cts;

namespace {

struct Toy {
    std::vector<std::byte> bytes{std::byte{1}, std::byte{2}, std::byte{3}};
    int                    runs = 0;
};

cts::Suite<Toy> suite() {
    cts::Suite<Toy> s{"toy", "1"};
    s.add({.id = "toy.bytes", .contract = "toy@1", .level = cts::Level::exact, .needs = {},
           .body = [](Toy& t, cts::Check& c) { ++t.runs; c.that(cts::exact(t.bytes, t.bytes).same, "the same bytes"); }});
    s.add({.id = "toy.wrong", .contract = "toy@1", .level = cts::Level::semantic, .needs = {},
           .body = [](Toy& t, cts::Check& c) { ++t.runs; c.equal(t.runs, 100, "runs"); }});
    s.add({.id = "toy.gpu", .contract = "toy@1", .level = cts::Level::tolerance, .needs = {"gpu"},
           .body = [](Toy& t, cts::Check&) { ++t.runs; }});
    s.add({.id = "toy.compute", .contract = "toy@1", .level = cts::Level::exact, .needs = {"compute"},
           .body = [](Toy& t, cts::Check&) { ++t.runs; }});
    return s;
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

int main(int argc, char** argv) {
    Suite s{"cts"};

    s.test("a case passes or fails by its checks; unsupported comes from capabilities and never runs", [] {
        Toy toy;
        const auto has = [](std::string_view cap) { return cap == "copy"; };
        const auto r = suite().run(toy, has, {.required = {"compute"}}, {.subject = "toy", .platform = "test"});
        check_eq(r.outcomes.size(), std::size_t{4});
        check(r.outcomes[0].status == cts::Status::passed, "toy.bytes passed");
        check(r.outcomes[1].status == cts::Status::failed && !r.outcomes[1].failures.empty(), "toy.wrong failed with its failure");
        check(r.outcomes[1].failures[0].why == "runs: expected 100, got 2", r.outcomes[1].failures[0].why);
        check(r.outcomes[2].status == cts::Status::unsupported && r.outcomes[2].missing == std::vector<std::string>{"gpu"}, "toy.gpu unsupported");
        check(r.outcomes[3].status == cts::Status::failed, "a required capability missing fails");
        check_eq(toy.runs, 2);  // the unsupported and the failed-for-a-capability cases never ran
        check_eq(r.count(cts::Status::passed), std::size_t{1});
        check_eq(r.count(cts::Status::unsupported), std::size_t{1});
        check(!r.ok(), "a failed case: not ok");
        std::print("{}", r.text());
    });

    s.test("no capability query: every case with needs is unsupported, none counted as passed", [] {
        Toy toy;
        const std::vector<std::string> only{"toy.gpu"};
        const auto r = suite().run(toy, {}, {}, {}, only);
        check_eq(r.outcomes.size(), std::size_t{1});
        check(r.outcomes[0].status == cts::Status::unsupported, "unsupported");
        check(r.ok() && r.count(cts::Status::passed) == 0, "ok, and nothing passed");
    });

    s.test("the report: format, environment, totals, failures, missing capabilities", [] {
        Toy toy;
        const auto r = suite().run(toy, [](std::string_view) { return false; }, {}, {.subject = "toy", .platform = "test", .toolchain = "t"});
        const auto j = nlohmann::json::parse(r.json());
        check(j["format"] == "mlibs.cts@0.1" && j["suite"] == "toy" && j["environment"]["subject"] == "toy", "header");
        check(j["totals"]["passed"] == 1 && j["totals"]["failed"] == 1 && j["totals"]["unsupported"] == 2, j["totals"].dump());
        check(j["cases"][1]["failures"].size() == 1 && j["cases"][2]["missing"][0] == "gpu", "failures and missing capabilities");
        check(j["cases"][0]["level"] == "A" && j["cases"][1]["level"] == "S" && j["cases"][2]["level"] == "B", "levels");
    });

    s.test("comparisons say what differs where", [] {
        const std::array<std::byte, 4> a{std::byte{10}, std::byte{20}, std::byte{30}, std::byte{40}};
        const std::array<std::byte, 4> b{std::byte{10}, std::byte{21}, std::byte{30}, std::byte{44}};
        const auto e = cts::exact(a, b);
        check(!e.same && e.first == 1 && e.largest == 4 && e.beyond == 2, e.text());
        check(!cts::within(a, b, 1, 0).same && cts::within(a, b, 1, 1).same, "a tolerance and a budget");
        check(cts::within(a, b, 4, 0).same, "within ±4");
        check(!cts::exact(a, std::span(b).first(3)).same, "sizes differ");
        // Pictures: 2 × 1, the second pixel's blue off by 3.
        const std::array<std::byte, 8> p{std::byte{1}, std::byte{2}, std::byte{3}, std::byte{255}, std::byte{4}, std::byte{5}, std::byte{6}, std::byte{255}};
        auto q = p;
        q[6] = std::byte{9};
        const auto d = cts::image({p, 2, 1, 8}, {q, 2, 1, 8}, 1);
        check(!d.same && d.x == 1 && d.y == 0 && d.largest == 3 && d.beyond == 1, d.text());
        check(cts::image({p, 2, 1, 8}, {q, 2, 1, 8}, 3).same, "within ±3");
        check(!cts::image({p, 2, 1, 8}, {p, 1, 2, 4}).same, "sizes differ");
    });

    s.test("pinned hashes and sampled processes record their evidence", [] {
        cts::Check c;
        const std::string_view text = "foobar";
        const auto bytes = std::as_bytes(std::span(text));
        check(cts::pinned(c, bytes, "85944171f73967e8", "foobar"), "FNV-1a of foobar");
        check(!cts::pinned(c, bytes, "0000000000000000", "foobar") && c.failed(), "a wrong pin fails");
        cts::Check p;
        const std::array<double, 3> times{0.0, 0.5, 1.0};
        auto frame = [](double t) {
            std::vector<std::byte> f(4, std::byte{static_cast<unsigned char>(t * 100)});
            return f;
        };
        std::vector<std::string> pins;
        for (double t : times) pins.push_back(cts::hash(frame(t)));
        const std::array<std::string_view, 3> views{pins[0], pins[1], pins[2]};
        check(cts::sampled(p, times, frame, views, "fade") && !p.failed(), "every sample as pinned");
        check_eq(p.evidence().size(), std::size_t{3});
        check(p.evidence()[1].first == "fade at 0.5 s", p.evidence()[1].first);
    });

    s.test("the preparation contract: accepted twice alike, refused by kind, nothing partly published", [] {
        cts::Suite<Counts> suite{"toy.prepare", "1"};
        cts::prepare::add(suite, "toy@1", {.valid = {{"three", 3}, {"none", 0}}, .invalid = {{"negative", -1}}, .over_budget = {{"eleven", 11}}});
        check_eq(suite.cases().size(), std::size_t{4});
        Counts sound;
        const auto r = suite.run(sound);
        check(r.ok(), r.text());
        check_eq(r.outcomes[0].id, std::string("prepare.three.accepted"));
        check_eq(r.outcomes[3].id, std::string("prepare.eleven.over-budget"));
        Counts leaky{.leaky = true};
        const auto l = suite.run(leaky);
        check(!l.ok() && l.outcomes[3].status == cts::Status::failed, "a refusal that publishes part fails");
        check(l.outcomes[3].failures[0].why.contains("the state after the refusal"), l.text());
    });

    return s.run(argc, argv);
}
