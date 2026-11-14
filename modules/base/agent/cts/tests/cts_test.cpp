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
enum class Fault : std::uint8_t { none, reading_writes, preview_moves, undo_keeps, forgets_actor, over_budget, unstable, no_guide, task_stops, shows_nothing };

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
        static constexpr std::array<control::Parameter, 1> try_parameters{{{"by", "integer", "how much", false}}};
        static constexpr std::array<std::string_view, 1> try_examples{R"({"op":"ledger.try","by":3})"};
        static constexpr std::array<control::CatalogEntry, 7> silent{{
            {{"ledger.read", openas::Effect::read, false}, "The total and the entries, the latest first", read_parameters, "json", control::Cost::instant, "", false, false, false},
            {{"ledger.add", openas::Effect::reversible, false}, "Adds an entry", add_parameters, "json", control::Cost::instant, "ledger.undo", false, true, false},
            {{"ledger.undo", openas::Effect::reversible, false}, "Takes the latest entry back", {}, "json", control::Cost::instant, "ledger.add with what it held", false, false, false},
            {{"ledger.try", openas::Effect::read, false}, "The total an entry would give, nothing added", try_parameters, "json", control::Cost::instant, "", false, false, false},
            {{"ledger.tick", openas::Effect::reversible, false}, "Its clock, one on", {}, "json", control::Cost::instant, "none: its clock only goes on", false, false, false},
            {{"ledger.why", openas::Effect::read, false}, "Why the total is what it is: the entry that last changed it, and who made it", {}, "json", control::Cost::instant, "", false, false, false},
            {{"ledger.source", openas::Effect::read, false}, "The entries the total is made of, each with the request that made it", {}, "json", control::Cost::instant, "", false, false, false},
        }};
        if (fault_ == Fault::shows_nothing) return silent;  // what needs an argument shows no request of it
        static constexpr std::array<control::CatalogEntry, 7> all{{
            {{"ledger.read", openas::Effect::read, false}, "The total and the entries, the latest first", read_parameters, "json", control::Cost::instant, "", false, false, false},
            {{"ledger.add", openas::Effect::reversible, false}, "Adds an entry", add_parameters, "json", control::Cost::instant, "ledger.undo", false, true, false},
            {{"ledger.undo", openas::Effect::reversible, false}, "Takes the latest entry back", {}, "json", control::Cost::instant, "ledger.add with what it held", false, false, false},
            {{"ledger.try", openas::Effect::read, false}, "The total an entry would give, nothing added", try_parameters, "json", control::Cost::instant, "", false, false, false, try_examples},
            {{"ledger.tick", openas::Effect::reversible, false}, "Its clock, one on", {}, "json", control::Cost::instant, "none: its clock only goes on", false, false, false},
            {{"ledger.why", openas::Effect::read, false}, "Why the total is what it is: the entry that last changed it, and who made it", {}, "json", control::Cost::instant, "", false, false, false},
            {{"ledger.source", openas::Effect::read, false}, "The entries the total is made of, each with the request that made it", {}, "json", control::Cost::instant, "", false, false, false},
        }};
        return all;
    }
    control::Guide guide() const noexcept override {
        static constexpr std::array<std::string_view, 3> add_steps{R"({"op":"ledger.read"})", R"({"op":"ledger.add","by":4})", R"({"op":"ledger.why"})"};
        static constexpr std::array<std::string_view, 3> back_steps{R"({"op":"ledger.try","by":9})", R"({"op":"ledger.add","by":9})", R"({"op":"ledger.undo"})"};
        // A task whose third step has nothing left to take back.
        static constexpr std::array<std::string_view, 6> stopping{R"({"op":"ledger.undo"})", R"({"op":"ledger.undo"})", R"({"op":"ledger.undo"})",
                                                                  R"({"op":"ledger.undo"})", R"({"op":"ledger.undo"})", R"({"op":"ledger.read"})"};
        static constexpr std::array<control::Task, 2> tasks{{
            {"add-and-see-why", "Add an entry and ask why the total is what it is", add_steps},
            {"try-then-take-back", "See what an entry would give, add it, take it back", back_steps},
        }};
        static constexpr std::array<control::Task, 1> stops{{{"take-everything-back", "Take every entry back", stopping}}};
        if (fault_ == Fault::no_guide) return {};
        return {"Amounts added by someone, at a tick of its own clock.", fault_ == Fault::task_stops ? std::span<const control::Task>(stops) : std::span<const control::Task>(tasks)};
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

// The suite counted in this process on a probe: what failed, by name.
mlibs::cts::Tally counted(const contract::Probe& probe, mlibs::cts::Against who = {}) {
    return mlibs::cts::tally([&] { contract::declare(probe, std::move(who)); });
}
std::string failures(const mlibs::cts::Tally& tally) {
    std::string failed;
    for (const std::string& id : tally.ids(Status::failed)) failed += (failed.empty() ? "" : " ") + id;
    return failed;
}

}  // namespace

UTest cases{"agent.contract", [] {
    // A subject that keeps the contract: every case, as this program's own (ledger/agent.catalog.complete, …).
    const contract::Probe whole = probe_of(everything);
    contract::declare(whole, {.subject = "ledger"});
    "a subject that keeps the contract passes every case"_test = [&whole] {
        const auto tally = counted(whole);
        expect(tally.ok()) << tally.text();
        expect(eq(tally.count(Status::passed), contract::case_count()) && contract::case_count() >= 16u) << "all eight properties and the four norms over them, none skipped";
        expect(contract::unsupported(everything).empty());
    };

    "what a subject says it cannot do is unsupported — not passed, not failed — and fails where the runner requires it"_test = [] {
        const agent::Capabilities reads_only{.observation = true};
        const auto tally = counted(probe_of(reads_only));
        expect(tally.ok()) << tally.text();
        const auto status = [&](std::string_view id) {
            const auto* found = tally.find(id);
            return found ? found->status : Status::failed;
        };
        expect(status("agent.catalog.complete") == Status::passed && status("agent.read.pure") == Status::passed);
        expect(status("agent.authority.first") == Status::passed) << "a reading is decided too";
        const std::vector<std::string> lacking{"agent.preview.untouched", "agent.undo.returns", "agent.time.replays", "agent.source.chain", "agent.actor.recorded",
                                               "agent.observe.budget", "agent.causes.explained"};
        expect(tally.ids(Status::unsupported) == lacking) << tally.text();
        expect(contract::unsupported(reads_only) == lacking) << "known from its capabilities alone";

        const auto required = counted(probe_of(reads_only), {.required = {"time"}});
        expect(!required.ok() && failures(required) == "requires time") << "a capability the runner requires, missing, fails — once";
    };

    // The same, as mtest reports it: reader/agent.time.replays is skipped, "unsupported: time".
    const contract::Probe reader = probe_of({.observation = true});
    contract::declare(reader, {.subject = "reader"});

    "a subject that says it can and does not fails that case by name"_test = [] {
        expect(failures(counted(probe_of(everything, Fault::reading_writes))) == "agent.read.pure");
        expect(failures(counted(probe_of(everything, Fault::preview_moves))) == "agent.preview.untouched");
        expect(failures(counted(probe_of(everything, Fault::undo_keeps))) == "agent.undo.returns");
        expect(failures(counted(probe_of(everything, Fault::forgets_actor))) == "agent.actor.recorded");
        expect(failures(counted(probe_of(everything, Fault::over_budget))) == "agent.observe.budget");
        const std::string unstable = failures(counted(probe_of(everything, Fault::unstable)));
        expect(unstable.contains("agent.catalog.stable") && unstable.contains("agent.time.replays")) << unstable;

        // What it tells an agent that knows nothing of it (D-186): no guide, a task that does not run as written,
        // an operation that needs an argument and shows no request of it.
        expect(failures(counted(probe_of(everything, Fault::no_guide))) == "agent.guide.runs");
        expect(failures(counted(probe_of(everything, Fault::task_stops))) == "agent.guide.runs");
        expect(failures(counted(probe_of(everything, Fault::shows_nothing))) == "agent.guide.runs");

        // It says it has time and a preview; its probe shows neither.
        contract::Probe silent = probe_of(everything);
        silent.script.clear();
        silent.preview.clear();
        expect(failures(counted(silent)) == "agent.preview.untouched agent.time.replays") << "a claim not shown is not passed";
    };
}};
