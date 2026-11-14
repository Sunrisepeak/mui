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
        static constexpr std::array<control::Parameter, 1> set_parameters{{{"to", "integer", "the count from now on", false}}};
        static constexpr std::array<std::string_view, 1> set_examples{R"({"op":"counter.set","to":3})"};
        static constexpr std::array<control::CatalogEntry, 4> all{{
            {{"counter.read", openas::Effect::read, false}, "The count", {}, "json", control::Cost::instant, "", false, false, false},
            {{"counter.add", openas::Effect::reversible, false}, "Adds to the count", add_parameters, "json", control::Cost::instant, "counter.add with the opposite", false, false, false},
            {{"counter.reset", openas::Effect::irreversible, false}, "Forgets the count", {}, "json", control::Cost::instant, "", false, false, false, {}, "zero clear"},
            {{"counter.set", openas::Effect::reversible, false}, "Sets the count", set_parameters, "json", control::Cost::instant, "counter.set to what it was", false, false, false, set_examples},
        }};
        return all;
    }
    control::Guide guide() const noexcept override {
        static constexpr std::array<std::string_view, 2> steps{R"({"op":"counter.add","by":2})", R"({"op":"counter.read"})"};
        static constexpr std::array<control::Task, 1> tasks{{{"count-up", "Add to the count and read it", steps}}};
        return {"A count.", tasks};
    }
    std::vector<agent::Term> terms() const override { return {{"page", "three", "The page the counter is on", R"({"op":"counter.read"})"}}; }
    std::uint64_t state_hash() const override { return static_cast<std::uint64_t>(count) + 1; }
    void operate(std::string_view operation, const agent::Request& request, agent::Answer& answer) override {
        ++operated;
        by = request.actor.name;
        if (operation == "counter.add") count += request["by"].integer().value_or(1);
        if (operation == "counter.reset") count = 0;
        if (operation == "counter.set") {
            if (request["to"].integer().value_or(0) < 0) return answer.fail(openas::error_code::invalid_argument, "a count is not negative");
            count = request["to"].integer().value_or(0);
        }
        answer.data(std::format("{{\"count\":{}}}", count));
    }
    void poll() override { ++polled; }

    std::int64_t count = 0;
    int          operated = 0, polled = 0;
    std::string  by;  // who asked last, as the session said
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
    // The error's ways out: each one's request, as JSON.
    [[nodiscard]] std::vector<std::string> next() const {
        std::vector<std::string> out;
        for (const auto& record : records)
            if (record.root()["kind"].text() == "error")
                for (const auto& way : record.root()["next"].items()) out.push_back(way["request"].dump());
        return out;
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
        expect(eq(subject["capabilities"].size(), std::size_t{8})) << "observation, budget, control, time, causes, provenance, preview, history";
        expect(subject["hash"].text() == "0000000000000001");
        expect(who.records[0].root()["re"].integer() == 1) << "every record echoes the request's re";

        const auto described = ask(session, R"({"op":"describe","subject":"counter"})");
        const auto operations = described.records[0].root()["data"]["operations"];
        expect(eq(operations.size(), std::size_t{4}));
        expect(operations[1]["name"].text() == "counter.add" && operations[1]["effect"].text() == "reversible");
        expect(!operations[1]["parameters"]) << "briefly: a name, its effect, a line";
        expect(!described.records[0].root()["re"]) << "no re asked, none echoed";
        const auto one = ask(session, R"({"op":"describe","operation":"counter.add"})");
        const auto entry = one.records[0].root()["data"];
        expect(entry["parameters"][0]["name"].text() == "by" && entry["parameters"][0]["optional"].boolean() == true) << "one operation whole";
        expect(entry["undo"].text() == "counter.add with the opposite" && entry["cost"].text() == "instant");
        expect(ask(session, R"({"op":"describe","operation":"counter.fly"})").error() == "not-found");
        expect(ask(session, R"({"op":"describe","subject":"clock"})").error() == "not-found") << "a subject that is not here";
        expect(eq(ask(session, R"({"op":"describe"})").records[0].root()["data"]["operations"].size(), std::size_t{4})) << "the only subject here, when none is named";
    };

    "an agent that knows nothing of it begins from `describe`: what it is, tasks whose steps run as written, examples with an operation (D-186)"_test = [] {
        Counter counter;
        agent::SubjectSession session({&counter});
        const auto described = ask(session, R"({"op":"describe","subject":"counter"})");
        const auto data = described.records[0].root()["data"];
        expect(data["about"].text() == "A count.");
        expect(eq(data["tasks"].size(), std::size_t{1}) && data["tasks"][0]["name"].text() == "count-up" && data["tasks"][0]["summary"].text() == "Add to the count and read it");
        // Each step is a request: sent as it is given.
        for (const auto& step : data["tasks"][0]["steps"].items()) expect(eq(ask(session, step.dump()).exit_code(), std::int64_t{0})) << step.dump();
        expect(eq(counter.count, std::int64_t{2}));
        const auto one = ask(session, R"({"op":"describe","operation":"counter.set"})");
        const auto whole = one.records[0].root()["data"];
        expect(eq(whole["examples"].size(), std::size_t{1}));
        expect(eq(ask(session, whole["examples"][0].dump()).exit_code(), std::int64_t{0}) && eq(counter.count, std::int64_t{3}));
        expect(!ask(session, R"({"op":"describe","operation":"counter.read"})").records[0].root()["data"]["examples"]) << "none declared, none shown";
    };

    "what is here is found by intent: operations, tasks and what a subject's content names, the best first, each with requests (D-186)"_test = [] {
        Counter counter;
        agent::SubjectSession session({&counter});
        const auto names = [&](std::string_view query) {
            std::vector<std::string> out;
            const auto found = ask(session, std::format(R"({{"op":"find","q":"{}"}})", query));
            for (const auto& each : found.records[0].root()["data"]["found"].items()) out.push_back(each["kind"].text().value_or("") + " " + each["name"].text().value_or(""));
            return out;
        };
        using Names = std::vector<std::string>;
        expect(names("forget the count").front() == "operation counter.reset") << "the word only it has outweighs the one all have";
        expect(names("adding").front() == "operation counter.add") << "a word's ending does not matter";
        expect(names("I want to add to it and read").front() == "task count-up") << "a task by what it gets done";
        expect(names("ZERO") == Names{"operation counter.reset"}) << "a keyword: a word an intent uses that the summary does not";
        expect(names("the page I am on") == Names{"page three"}) << "what the content names (terms)";
        const auto found = ask(session, R"({"op":"find","q":"set the count"})");
        const auto best = found.records[0].root()["data"]["found"][0];
        expect(best["name"].text() == "counter.set" && best["subject"].text() == "counter" && best["requests"][0].dump() == R"({"op":"counter.set","to":3})") << "its example, to send as it is";
        const auto task = ask(session, R"({"op":"find","q":"count up"})");
        expect(eq(task.records[0].root()["data"]["found"][0]["requests"].size(), std::size_t{2})) << "a task's steps";
        const auto read = ask(session, R"({"op":"find","q":"read","limit":1})");
        expect(eq(read.records[0].root()["data"]["found"].size(), std::size_t{1}));
        expect(read.records[0].root()["data"]["found"][0]["requests"][0].dump() == R"({"op":"describe","operation":"counter.read"})") << "no example: its description";
        // Nothing found: where to look. Nothing asked: said so.
        const auto nothing = ask(session, R"({"op":"find","q":"xyzzy"})");
        expect(eq(nothing.exit_code(), std::int64_t{0}) && nothing.records[0].root()["data"]["next"][0]["request"].dump() == R"({"op":"describe","subject":"counter"})");
        expect(ask(session, R"({"op":"find"})").error() == "invalid-argument");
        expect(eq(counter.operated, 0)) << "finding reaches no subject's state";
    };

    "every error carries its ways out, requests to send as they are (D-186)"_test = [] {
        Counter counter;
        agent::SubjectSession session({&counter});
        using Ways = std::vector<std::string>;
        // A name a letter off: the operation it is nearest, described; then what the subject can do.
        expect(ask(session, R"({"op":"counter.ad"})").next() == Ways{R"({"op":"describe","operation":"counter.add"})", R"({"op":"describe","subject":"counter"})"});
        // Nothing near it, and no subject of that name: who is here.
        expect(ask(session, R"({"op":"clock.read"})").next() == Ways{R"({"op":"subjects"})"});
        expect(ask(session, "not json").next() == Ways{R"({"op":"subjects"})"});
        expect(ask(session, R"({"op":"describe","subject":"clock"})").next() == Ways{R"({"op":"subjects"})"});
        expect(ask(session, R"({"op":"describe","operation":"counter.rest"})").next().front() == R"({"op":"describe","operation":"counter.reset"})");
        // Without what it needs: answered before the subject, with the operation whole and a request that is well formed.
        const int before = counter.operated;
        const auto lacking = ask(session, R"({"op":"counter.set"})");
        expect(lacking.error() == "invalid-argument" && eq(counter.operated, before)) << "it did not reach the subject";
        expect(lacking.next() == Ways{R"({"op":"describe","operation":"counter.set"})", R"({"op":"counter.set","to":3})"});
        for (const std::string& way : lacking.next()) expect(eq(ask(session, way).exit_code(), std::int64_t{0})) << way;
        // The subject's own failure, which names no way out, carries the operation's.
        expect(ask(session, R"({"op":"counter.set","to":-1})").next() == lacking.next());

        // What a person must agree to: the same request with the answer. What is refused: what else there is.
        auto access = std::make_shared<control::Access>(control::Policy{});
        agent::SubjectSession asking({&counter}, {.access = access});
        const auto asked = ask(asking, R"({"op":"counter.reset","re":7})");
        expect(asked.next() == Ways{R"({"op":"counter.reset","re":7,"answer":"yes"})"});
        expect(eq(ask(asking, asked.next().front()).exit_code(), std::int64_t{0}) && eq(counter.count, std::int64_t{0}));
        control::Policy narrow;
        narrow.user.operations = std::vector<std::string>{"counter.read"};
        agent::SubjectSession limited({&counter}, {.access = std::make_shared<control::Access>(narrow)});
        expect(ask(limited, R"({"op":"counter.add"})").next() == Ways{R"({"op":"describe","subject":"counter"})"});
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

        expect(eq(ask(session, R"({"op":"counter.read","actor":"person:mallory"})").exit_code(), std::int64_t{0})) << "a reading runs";
        expect(counter.by == "reviewer") << "who asks is the session's to say, not the line's";

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
