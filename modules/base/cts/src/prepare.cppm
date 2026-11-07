// mlibs.cts:prepare — the preparation contract every engine keeps
// (convergence plan §5.2, §5.3 ②), as cases any engine's tests add:
//
//   prepare(description) → expected<Prepared, Error>
//     a description that is valid prepares, and the same way every time;
//     an invalid one is refused as invalid-argument, one beyond a budget as
//     refused — and a refusal leaves the engine as it was (nothing is
//     partly published);
//   execute(prepared) is noexcept and gives the same result every time.
//
//     struct Programs {                                    // an engine's adapter
//         using Description = hanim::Program;
//         auto prepare(const Description& d) { return hanim::PreparedProgram::prepare(d, limits); }
//         std::string fingerprint(const hanim::PreparedProgram& p);   // what was prepared, comparable
//         std::string state();                             // what a refusal must not change
//         std::string execute(const hanim::PreparedProgram& p) noexcept;   // optional
//     };
//     mlibs::cts::Suite<Programs> suite{"hanim.program.prepare", "1"};
//     mlibs::cts::prepare::add(suite, "hanim.program@1", {.valid = {{"move", move}}, .invalid = {…}, .over_budget = {…}});
//
// The error is mlibs.error's (its kind() says how it was refused).
export module mlibs.cts:prepare;

import std;
import mlibs.error;
import :core;

export namespace mlibs::cts::prepare {

// An engine's preparation as the cases see it.
template <class A>
concept Preparer = requires(A& a, const typename A::Description& d) {
    { a.prepare(d).has_value() } -> std::convertible_to<bool>;
    { a.prepare(d).error().kind() } -> std::same_as<mlibs::error::Kind>;
    { a.prepare(d).error().text() } -> std::convertible_to<std::string>;
    { a.fingerprint(*a.prepare(d)) } -> std::convertible_to<std::string>;
    { a.state() } -> std::convertible_to<std::string>;
};

// Named descriptions: each valid one prepares; each invalid one is refused as
// invalid-argument; each over a budget is refused as refused.
template <class D>
struct Samples {
    std::vector<std::pair<std::string, D>> valid;
    std::vector<std::pair<std::string, D>> invalid;
    std::vector<std::pair<std::string, D>> over_budget;
};

// The cases, `prepare.<name>.<what>`, checking `contract` at level S (a
// refusal's kind and the state it leaves) and A (the same preparation, the
// same execution, bit for bit).
template <Preparer A>
void add(Suite<A>& suite, std::string_view contract, Samples<typename A::Description> samples) {
    using D = typename A::Description;
    const std::string c(contract);
    for (auto& [name, d] : samples.valid) {
        suite.add({.id = std::format("prepare.{}.accepted", name), .contract = c, .level = Level::exact, .needs = {},
                   .body = [d](A& a, Check& check) {
                       auto first = a.prepare(d);
                       if (!check.that(first.has_value(), first ? std::string() : "refused: " + std::string(first.error().text()))) return;
                       auto second = a.prepare(d);
                       if (!check.that(second.has_value(), "the second preparation was refused")) return;
                       const std::string f = a.fingerprint(*first);
                       check.equal(std::string(a.fingerprint(*second)), f, "the second preparation");
                       check.evidence("fingerprint", f);
                       if constexpr (requires { { a.execute(*first) } -> std::convertible_to<std::string>; }) {
                           check.that(noexcept(a.execute(*first)), "execution is noexcept");
                           const std::string once = a.execute(*first);
                           check.equal(std::string(a.execute(*first)), once, "the second execution");
                           check.equal(std::string(a.execute(*second)), once, "the other preparation's execution");
                           check.evidence("execution", once);
                       }
                   }});
    }
    const auto refusal = [&](std::string name, D d, mlibs::error::Kind want, std::string_view what) {
        suite.add({.id = std::move(name), .contract = c, .level = Level::semantic, .needs = {},
                   .body = [d = std::move(d), want, what = std::string(what)](A& a, Check& check) {
                       const std::string before = a.state();
                       const auto r = a.prepare(d);
                       if (!check.that(!r.has_value(), std::format("{} was accepted", what))) return;
                       const auto kind = r.error().kind();
                       check.that(kind == want, std::format("{} was refused as {}, not {}: {}", what, mlibs::error::name(kind),
                                                            mlibs::error::name(want), r.error().text()));
                       check.equal(std::string(a.state()), before, "the state after the refusal");
                       check.evidence("refusal", r.error().text());
                   }});
    };
    for (auto& [name, d] : samples.invalid)
        refusal(std::format("prepare.{}.refused", name), d, mlibs::error::Kind::invalid_argument, "an invalid description");
    for (auto& [name, d] : samples.over_budget)
        refusal(std::format("prepare.{}.over-budget", name), d, mlibs::error::Kind::refused, "a description beyond its budget");
}

}  // namespace mlibs::cts::prepare
