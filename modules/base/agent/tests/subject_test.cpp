// A subject that is neither an interface nor a game: a counter with a
// reading, a change it can take back and one it cannot. What a session over
// it answers, and that nothing reaches it undecided.
import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace control = mlibs::control;

namespace {

class Counter final : public agent::Subject {
public:
    std::string_view name() const noexcept override { return "counter"; }
    std::string_view interface_name() const noexcept override { return "example.counter@1"; }
    agent::Capabilities capabilities() const noexcept override { return {.observation = true, .control = true}; }
    std::span<const control::CatalogEntry> operations() const noexcept override {
        static constexpr std::array<control::Parameter, 1> add_parameters{{{"by", "integer", "how much (1 when none is given)", true}}};
        static constexpr std::array<control::CatalogEntry, 3> all{{
            {{"counter.read", openas::Effect::read, false}, "The count", {}, "json", control::Cost::instant, "", false, false, false},
            {{"counter.add", openas::Effect::reversible, false}, "Adds to the count", add_parameters, "json", control::Cost::instant, "counter.add with the opposite", false, false, false},
            {{"counter.reset", openas::Effect::irreversible, false}, "Forgets the count", {}, "json", control::Cost::instant, "", false, false, false},
        }};
        return all;
    }
    std::uint64_t state_hash() const override { return static_cast<std::uint64_t>(count) + 1; }
    void operate(std::string_view operation, const agent::Request& request, agent::Answer& answer) override {
        ++operated;
        if (operation == "counter.add") count += request["by"].integer().value_or(1);
        if (operation == "counter.reset") count = 0;
        answer.data(std::format("{{\"count\":{}}}", count));
    }
    void poll() override { ++polled; }

    std::int64_t count = 0;
    int          operated = 0, polled = 0;
};

// What a session answers to one request: its records, parsed.
struct Answered {
    std::vector<mlibs::json::Document> records;
    [[nodiscard]] std::string kind(std::size_t index) const { return records[index].root()["kind"].text().value_or(""); }
    [[nodiscard]] std::int64_t exit_code() const { return records.back().root()["exitCode"].integer().value_or(-1); }
    [[nodiscard]] std::string error() const {
        for (const auto& record : records)
            if (record.root()["kind"].text() == "error") return record.root()["code"].text().value_or("");
        return {};
    }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        expect(fatal(parsed.has_value())) << record;
        out.records.push_back(std::move(*parsed));
    });
    expect(fatal(!out.records.empty()));
    expect(out.kind(out.records.size() - 1) == "result") << "an answer ends with its result";
    return out;
}

}  // namespace

UTest cases{"agent.subject", [] {
    "who is here, what each can do, and its operations with their effects"_test = [] {
        Counter counter;
        agent::SubjectSession session({&counter});
        const auto who = ask(session, R"({"op":"subjects","re":1})");
        expect(eq(who.exit_code(), std::int64_t{0}));
        const auto subject = who.records[0].root()["data"]["subjects"][0];
        expect(subject["name"].text() == "counter" && subject["interface"].text() == "example.counter@1");
        expect(subject["capabilities"]["observation"].boolean() == true && subject["capabilities"]["time"].boolean() == false)
            << "what it cannot do it says";
        expect(subject["hash"].text() == "0000000000000001");
        expect(who.records[0].root()["re"].integer() == 1) << "every record echoes the request's re";

        const auto described = ask(session, R"({"op":"counter.describe"})");
        const auto operations = described.records[0].root()["data"]["operations"];
        expect(eq(operations.size(), std::size_t{3}));
        expect(operations[1]["name"].text() == "counter.add" && operations[1]["effect"].text() == "reversible");
        expect(!operations[1]["parameters"]) << "briefly: a name, its effect, a line";
        expect(!described.records[0].root()["re"]) << "no re asked, none echoed";
        const auto one = ask(session, R"({"op":"counter.describe","operation":"counter.add"})");
        const auto entry = one.records[0].root()["data"];
        expect(entry["parameters"][0]["name"].text() == "by" && entry["parameters"][0]["optional"].boolean() == true) << "one operation whole";
        expect(entry["undo"].text() == "counter.add with the opposite" && entry["cost"].text() == "instant");
        expect(ask(session, R"({"op":"counter.describe","operation":"counter.fly"})").error() == "not-found");
    };

    "an operation runs and answers its data; what is not here is not-found; a line that is no request is said so"_test = [] {
        Counter counter;
        agent::SubjectSession session({&counter});
        const auto added = ask(session, R"({"op":"counter.add","by":5,"re":"a"})");
        expect(eq(added.exit_code(), std::int64_t{0}));
        expect(added.records[0].root()["data"]["count"].integer() == 5);
        expect(added.records[0].root()["re"].text() == "a");
        expect(eq(counter.count, std::int64_t{5}));

        expect(ask(session, R"({"op":"counter.fly"})").error() == "not-found");
        expect(ask(session, R"({"op":"clock.read"})").error() == "not-found");
        expect(ask(session, "not json").error() == "invalid-argument");
        expect(ask(session, R"({"re":3})").error() == "invalid-argument") << "a request names its operation";
        expect(eq(counter.operated, 1)) << "none of those reached the subject";
        session.poll();
        expect(eq(counter.polled, 1));
    };

    "nothing reaches a subject undecided: what a person must agree to waits for the answer, what is refused does not run"_test = [] {
        Counter counter;
        counter.count = 9;
        // The release default: both users, the person asked before what cannot be taken back.
        auto access = std::make_shared<control::Access>(control::Policy{});
        agent::SubjectSession session({&counter}, {.access = access, .actor = {openas::Kind::agent, "reviewer", {}}});

        expect(eq(ask(session, R"({"op":"counter.read"})").exit_code(), std::int64_t{0})) << "a reading runs";

        const auto asked = ask(session, R"({"op":"counter.reset"})");
        expect(asked.error() == "needs-answer");
        expect(eq(asked.exit_code(), std::int64_t{openas::exit_code::needs_answer}));
        expect(eq(counter.count, std::int64_t{9})) << "it did not run";

        const auto agreed = ask(session, R"({"op":"counter.reset","answer":"yes"})");
        expect(eq(agreed.exit_code(), std::int64_t{0}));
        expect(eq(counter.count, std::int64_t{0}));

        // A user who allows the reading alone.
        control::Policy narrow;
        narrow.user.operations = std::vector<std::string>{"counter.read"};
        auto restricted = std::make_shared<control::Access>(narrow);
        agent::SubjectSession limited({&counter}, {.access = restricted});
        const int before = counter.operated;
        const auto refused = ask(limited, R"({"op":"counter.add","by":2})");
        expect(refused.error() == "refused");
        expect(eq(refused.exit_code(), std::int64_t{openas::exit_code::failed}));
        expect(eq(counter.operated, before) && eq(counter.count, std::int64_t{0})) << "a refused operation did not run";
        expect(eq(ask(limited, R"({"op":"counter.read"})").exit_code(), std::int64_t{0}));
    };
}};
