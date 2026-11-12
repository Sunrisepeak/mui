// The agent contract's suite on a subject made for it — a ledger that can do
// all eight — and on ledgers that cannot, or that say they can and do not:
// what a subject says it lacks is unsupported, never passed; what it claims
// and does not do fails by name.
import std;
import openas;
import mlibs.agent.cts;
import mlibs.control;
import mlibs.hash;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace contract = mlibs::agent::cts;
namespace control = mlibs::control;
using mlibs::cts::Status;

namespace {

// How a ledger departs from the contract (none: it keeps it).
enum class Fault : std::uint8_t { none, reading_writes, preview_moves, undo_keeps, forgets_actor, over_budget, unstable };

// Amounts added by someone, at a tick of its own clock.
class Ledger final : public agent::Subject {
public:
    explicit Ledger(agent::Capabilities can, Fault fault = Fault::none) : can_(can), fault_(fault) {
        static int made = 0;
        if (fault_ == Fault::unstable) tick_ = ++made;  // a first state that is not the same twice
    }
    std::string_view name() const noexcept override { return "ledger"; }
    std::string_view interface_name() const noexcept override { return "example.ledger@1"; }
    agent::Capabilities capabilities() const noexcept override { return can_; }
    std::span<const control::CatalogEntry> operations() const noexcept override {
        static constexpr std::array<control::Parameter, 1> read_parameters{{{"budget", "integer", "the most bytes of answer (all of it when none is given)", true}}};
        static constexpr std::array<control::Parameter, 1> add_parameters{{{"by", "integer", "how much (1 when none is given)", true}}};
        static constexpr std::array<control::CatalogEntry, 7> all{{
            {{"ledger.read", openas::Effect::read, false}, "The total and the entries, the latest first", read_parameters, "json", control::Cost::instant, "", false, false, false},
            {{"ledger.add", openas::Effect::reversible, false}, "Adds an entry", add_parameters, "json", control::Cost::instant, "ledger.undo", false, true, false},
            {{"ledger.undo", openas::Effect::reversible, false}, "Takes the latest entry back", {}, "json", control::Cost::instant, "ledger.add with what it held", false, false, false},
            {{"ledger.try", openas::Effect::read, false}, "The total an entry would give, nothing added", add_parameters, "json", control::Cost::instant, "", false, false, false},
            {{"ledger.tick", openas::Effect::reversible, false}, "Its clock, one on", {}, "json", control::Cost::instant, "none: its clock only goes on", false, false, false},
            {{"ledger.why", openas::Effect::read, false}, "Why the total is what it is: the entry that last changed it, and who made it", {}, "json", control::Cost::instant, "", false, false, false},
            {{"ledger.source", openas::Effect::read, false}, "The entries the total is made of, each with the request that made it", {}, "json", control::Cost::instant, "", false, false, false},
        }};
        return all;
    }
    std::uint64_t state_hash() const override {
        std::vector<std::uint64_t> parts{static_cast<std::uint64_t>(tick_), static_cast<std::uint64_t>(entries_.size())};
        for (const Entry& entry : entries_) parts.push_back(static_cast<std::uint64_t>(entry.amount) * 31 + static_cast<std::uint64_t>(entry.tick));
        return mlibs::hash::fnv1a(std::as_bytes(std::span(parts)));
    }
    void operate(std::string_view operation, const agent::Request& request, agent::Answer& answer) override {
        const std::int64_t by = request["by"].integer().value_or(1);
        if (operation == "ledger.read") {
            if (fault_ == Fault::reading_writes) ++tick_;
            const auto budget = request["budget"].integer();
            std::string out = std::format("{{\"total\":{},\"entries\":[", total());
            std::size_t shown = 0;
            for (auto entry = entries_.rbegin(); entry != entries_.rend(); ++entry, ++shown) {
                const std::string next = std::format("{}{}", shown ? "," : "", entry->amount);
                // What closes the answer still has to fit.
                if (budget && fault_ != Fault::over_budget && out.size() + next.size() + 12 > static_cast<std::size_t>(*budget)) break;
                out += next;
            }
            answer.data(out + std::format("],\"more\":{}}}", entries_.size() - shown));
        } else if (operation == "ledger.add") {
            entries_.push_back({by, tick_, fault_ == Fault::forgets_actor ? std::string{} : request.actor.name});
            answer.data(std::format("{{\"total\":{}}}", total()));
        } else if (operation == "ledger.undo") {
            if (entries_.empty()) return answer.fail(openas::error_code::stale, "there is nothing to take back");
            if (fault_ != Fault::undo_keeps) entries_.pop_back();
            answer.data(std::format("{{\"total\":{}}}", total()));
        } else if (operation == "ledger.try") {
            if (fault_ == Fault::preview_moves) entries_.push_back({by, tick_, {}});
            answer.data(std::format("{{\"total\":{}}}", total() + (fault_ == Fault::preview_moves ? 0 : by)));
        } else if (operation == "ledger.tick") {
            answer.data(std::format("{{\"tick\":{}}}", ++tick_));
        } else if (operation == "ledger.why") {
            if (entries_.empty()) return answer.data("{\"causes\":[]}");
            const Entry& last = entries_.back();
            answer.data(std::format("{{\"causes\":[{{\"operation\":\"ledger.add\",\"by\":{},\"tick\":{},\"actor\":{}}}]}}", last.amount, last.tick, agent::json_text(last.actor)));
        } else if (operation == "ledger.source") {
            std::string out = "{\"total\":[";
            for (std::size_t index = 0; index < entries_.size(); ++index)
                out += std::format("{}{{\"entry\":{},\"made-by\":\"ledger.add\",\"tick\":{}}}", index ? "," : "", index, entries_[index].tick);
            answer.data(out + "]}");
        }
    }

    void seed() { entries_ = {{5, 0, "owner"}, {7, 0, "owner"}, {11, 0, "owner"}, {13, 0, "owner"}}; }

private:
    struct Entry {
        std::int64_t amount = 0, tick = 0;
        std::string  actor;
    };
    [[nodiscard]] std::int64_t total() const {
        std::int64_t sum = 0;
        for (const Entry& entry : entries_) sum += entry.amount;
        return sum;
    }
    agent::Capabilities can_;
    Fault               fault_;
    std::int64_t        tick_ = 0;
    std::vector<Entry>  entries_;
};

constexpr agent::Capabilities everything{.observation = true, .budget = true, .control = true, .time = true, .causes = true, .provenance = true, .preview = true, .history = true};

contract::Probe probe_of(agent::Capabilities can, Fault fault = Fault::none) {
    return {.make = [can, fault]() -> std::unique_ptr<agent::Subject> {
                auto ledger = std::make_unique<Ledger>(can, fault);
                ledger->seed();
                return ledger;
            },
            .read = R"({"op":"ledger.read"})",
            .change = R"({"op":"ledger.add","by":4})",
            .undo = R"({"op":"ledger.undo"})",
            .preview = R"({"op":"ledger.try","by":9})",
            .script = {R"({"op":"ledger.tick"})", R"({"op":"ledger.add","by":2})", R"({"op":"ledger.tick"})", R"({"op":"ledger.read"})"},
            .explain = R"({"op":"ledger.why"})",
            .source = R"({"op":"ledger.source"})",
            .budgeted = R"({"op":"ledger.read","budget":40})",
            .budget = 40};
}

// The one case that failed, when exactly one did ("" otherwise).
std::string only_failure(const mlibs::cts::Report& report) {
    std::string failed;
    for (const auto& outcome : report.outcomes)
        if (outcome.status == Status::failed) failed += (failed.empty() ? "" : " ") + outcome.id;
    return failed;
}

}  // namespace

UTest cases{"agent.contract", [] {
    "a subject that keeps the contract passes every case"_test = [] {
        const auto report = contract::run(probe_of(everything), {.subject = "ledger"});
        std::println("{}", report.text());
        expect(report.ok()) << report.text();
        expect(eq(report.count(Status::passed), report.outcomes.size()) && report.outcomes.size() >= 12u) << "all eight properties, none skipped";
        expect(report.json().contains("\"mlibs.agent.contract\"")) << "a machine-readable report";
    };

    "what a subject says it cannot do is unsupported — not passed, not failed — and fails where the runner requires it"_test = [] {
        const agent::Capabilities reads_only{.observation = true};
        const auto report = contract::run(probe_of(reads_only));
        expect(report.ok()) << report.text();
        const auto status = [&](std::string_view id) {
            const auto found = std::ranges::find(report.outcomes, id, &mlibs::cts::Outcome::id);
            return found == report.outcomes.end() ? Status::failed : found->status;
        };
        expect(status("agent.catalog.complete") == Status::passed && status("agent.read.pure") == Status::passed);
        expect(status("agent.authority.first") == Status::passed) << "a reading is decided too";
        for (const std::string_view id : {"agent.preview.untouched", "agent.undo.returns", "agent.time.replays", "agent.source.chain", "agent.actor.recorded",
                                          "agent.observe.budget", "agent.causes.explained"})
            expect(status(id) == Status::unsupported) << id;
        expect(eq(report.count(Status::unsupported), std::size_t{7}));

        const auto required = contract::run(probe_of(reads_only), {}, {.required = {"time"}});
        expect(!required.ok()) << "a capability the runner requires, missing, fails";
    };

    "a subject that says it can and does not fails that case by name"_test = [] {
        expect(only_failure(contract::run(probe_of(everything, Fault::reading_writes))) == "agent.read.pure");
        expect(only_failure(contract::run(probe_of(everything, Fault::preview_moves))) == "agent.preview.untouched");
        expect(only_failure(contract::run(probe_of(everything, Fault::undo_keeps))) == "agent.undo.returns");
        expect(only_failure(contract::run(probe_of(everything, Fault::forgets_actor))) == "agent.actor.recorded");
        expect(only_failure(contract::run(probe_of(everything, Fault::over_budget))) == "agent.observe.budget");
        const auto unstable = contract::run(probe_of(everything, Fault::unstable));
        expect(only_failure(unstable).contains("agent.catalog.stable") && only_failure(unstable).contains("agent.time.replays")) << only_failure(unstable);

        // It says it has time and a preview; its probe shows neither.
        contract::Probe silent = probe_of(everything);
        silent.script.clear();
        silent.preview.clear();
        expect(only_failure(contract::run(silent)) == "agent.preview.untouched agent.time.replays") << "a claim not shown is not passed";
    };
}};
