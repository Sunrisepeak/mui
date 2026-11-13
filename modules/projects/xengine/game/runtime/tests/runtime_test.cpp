// A game on its own, with no interface anywhere: a small chase stepped by the
// agent that plays it, through the agent kernel — what a player is shown, the
// same actions a person has on a tier's lane, time stepped, sought and
// branched ahead, and the same requests giving the same bits.
import std;
import openas;
import mlibs.agent;
import mlibs.agent.cts;
import mlibs.json;
import mlibs.testing;
import mlibs.hash;
import xengine.input;
import xengine.runtime;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace runtime = xengine::runtime;

namespace {

// The player walks a line after a mark that jumps when reached; five catches end it.
struct Chase {
    struct State {
        std::int64_t  frame = 0;
        std::int32_t  player = 0, mark = 40, caught = 0, spare = 0;
        std::uint64_t rng = 0x9e3779b97f4a7c15ull;
    };
    struct Input {
        xengine::Actions player{};
    };
    static constexpr std::int32_t steps_per_second = 120;
    static void step(State& state, const Input& input) {
        ++state.frame;
        if (input.player.axis(0) > 0.5) ++state.player;
        if (input.player.axis(0) < -0.5) --state.player;
        if (state.player == state.mark) {
            ++state.caught;
            state.rng = state.rng * 6364136223846793005ull + 1442695040888963407ull;
            state.mark = static_cast<std::int32_t>((state.rng >> 40) % 61) - 30;
        }
    }
    static std::uint64_t checksum(const State& state) noexcept {
        // Field by field (no padding hashed): the same play, the same number.
        const std::array<std::uint64_t, 5> parts{static_cast<std::uint64_t>(state.frame), static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.player)),
                                                 static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.mark)), static_cast<std::uint64_t>(state.caught),
                                                 state.rng};
        return mlibs::hash::fnv1a(std::as_bytes(std::span(parts)));
    }
    // A player is shown where they and the mark stand; only the full view shows where the mark jumps next.
    static std::string observe(const State& state, bool full) {
        std::string out = std::format("{{\"player\":{},\"mark\":{},\"caught\":{}", state.player, state.mark, state.caught);
        if (full) out += std::format(",\"rng\":\"{:016x}\"", state.rng);
        return out + "}";
    }
    static void play(Input& input, const xengine::Actions& actions) { input.player = actions; }
    static bool over(const State& state) noexcept { return state.caught >= 5; }
};
using Game = runtime::Runtime<Chase>;

constexpr xengine::PlayTier assist{.name = "assist", .reaction_steps = 0, .actions_per_second = 0, .full_view = true};

struct Answered {
    std::vector<mlibs::json::Document> records;
    [[nodiscard]] mlibs::json::Node data() const { return records.front().root()["data"]; }
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
    return out;
}

// Toward the mark, as a player would: the axis held one way, decided on the step seen.
std::string toward(const Answered& seen) {
    const auto view = seen.data()["view"];
    const auto player = view["player"].integer().value_or(0), mark = view["mark"].integer().value_or(0);
    return std::format(R"({{"op":"world.act","seen":{},"axis":[{}]}})", seen.data()["frame"].integer().value_or(0), mark > player ? 1 : (mark < player ? -1 : 0));
}

// The same script on a game: chase the mark for at most `turns` turns of `stride` steps. The hash it ends on.
std::string play_out(agent::SubjectSession& session, int turns, int stride) {
    for (int turn = 0; turn < turns; ++turn) {
        const auto seen = ask(session, R"({"op":"world.observe"})");
        if (seen.data()["over"].boolean() == true) break;
        (void)ask(session, toward(seen));
        (void)ask(session, std::format(R"({{"op":"world.step","steps":{}}})", stride));
    }
    return ask(session, R"({"op":"world.state"})").data()["hash"].text().value_or("");
}

}  // namespace

UTest cases{"runtime", [] {
    "the agent contract on a game: what it has passes, what it says it has not is unsupported"_test = [] {
        namespace contract = mlibs::agent::cts;
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return std::make_unique<Game>(Chase::State{}, runtime::Options{.tier = assist}); },
            .read = R"({"op":"world.observe"})",
            .change = R"({"op":"world.step","steps":3})",
            .undo = R"({"op":"world.seek","frame":0})",
            .preview = R"({"op":"world.branch","steps":30,"axis":[1]})",
            .script = {R"({"op":"world.act","axis":[1]})", R"({"op":"world.step","steps":45})", R"({"op":"world.observe"})", R"({"op":"world.act","axis":[-1]})",
                       R"({"op":"world.step","steps":20})", R"({"op":"world.state"})"},
        };
        contract::declare(probe, {.subject = "xengine.runtime"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain", "agent.actor.recorded", "agent.observe.budget", "agent.causes.explained"})
            << "causes, the source of what it puts out, a budget: not yet, and said so";
    };

    "a game is a subject: what it can do, its operations, what a player on its tier is shown"_test = [] {
        Game game(Chase::State{});
        agent::SubjectSession session({&game});
        const auto listed = ask(session, R"({"op":"subjects"})");
        const auto who = listed.data()["subjects"][0];
        expect(who["name"].text() == "world" && who["interface"].text() == "xengine.world@0.1");
        expect(who["capabilities"]["time"].boolean() == true && who["capabilities"]["preview"].boolean() == true);
        expect(who["capabilities"]["causes"].boolean() == false) << "what it cannot do yet it says";
        expect(eq(ask(session, R"({"op":"describe","subject":"world"})").data()["operations"].size(), std::size_t{6}));

        const auto seen = ask(session, R"({"op":"world.observe"})");
        expect(seen.data()["frame"].integer() == 0 && seen.data()["tier"].text() == "human");
        expect(seen.data()["view"]["mark"].integer() == 40);
        expect(!seen.data()["view"]["rng"]) << "a player as a person is not shown what is hidden";
        Game shown(Chase::State{}, {.tier = assist});
        agent::SubjectSession privileged({&shown});
        expect(static_cast<bool>(ask(privileged, R"({"op":"world.observe"})").data()["view"]["rng"])) << "the full view shows it";
    };

    "an agent plays as a person: its action takes effect after the tier's reaction, no more often than the tier allows"_test = [] {
        Game game(Chase::State{});
        agent::SubjectSession session({&game});
        const auto acted = ask(session, R"({"op":"world.act","seen":0,"axis":[1]})");
        expect(acted.data()["effective"].integer() == 12) << "a tenth of a second after the frame it saw";
        (void)ask(session, R"({"op":"world.step","steps":11})");
        expect(eq(game.state().player, 0)) << "nothing moved before the reaction passed";
        (void)ask(session, R"({"op":"world.step","steps":1})");
        expect(eq(game.state().player, 1)) << "frame 12 shows it";
        (void)ask(session, R"({"op":"world.step","steps":5})");
        expect(eq(game.state().player, 6)) << "then a step a step";

        expect(ask(session, R"({"op":"world.act","seen":999,"axis":[1]})").error() == "invalid-argument") << "a step not played yet was not seen";
        expect(ask(session, R"({"op":"world.act","axis":[3]})").error() == "invalid-argument");
        int refused = 0;
        for (int action = 0; action < 12; ++action) refused += ask(session, R"({"op":"world.act","axis":[-1]})").error() == "refused";
        expect(refused > 0) << "more than ten actions within a second of steps";
    };

    "what would be is a branch: its future is the one played, and the game is untouched"_test = [] {
        Game game(Chase::State{}, {.tier = assist});
        agent::SubjectSession session({&game});
        const std::uint64_t before = game.state_hash();
        const auto future = ask(session, R"({"op":"world.branch","steps":30,"axis":[1]})");
        expect(future.data()["frame"].integer() == 30 && future.data()["view"]["player"].integer() == 30);
        expect(eq(game.state_hash(), before) && eq(game.frame(), std::int64_t{0})) << "the game itself did not move";

        (void)ask(session, R"({"op":"world.act","axis":[1]})");
        const auto played = ask(session, R"({"op":"world.step","steps":30})");
        expect(played.data()["hash"].text() == future.data()["hash"].text()) << "the same steps, played";
    };

    "on a person's tier too the branch is the future that is played: the reaction's steps are in it, and what was held is held until then"_test = [] {
        Game game(Chase::State{});  // the human tier: a tenth of a second's reaction
        agent::SubjectSession session({&game});
        (void)ask(session, R"({"op":"world.act","axis":[1]})");
        (void)ask(session, R"({"op":"world.step","steps":20})");
        const auto walked = ask(session, R"({"op":"world.observe"})").data()["view"]["player"].integer().value_or(0);
        // Turning back, tried: she walks on for the reaction's steps first.
        const auto future = ask(session, R"({"op":"world.branch","steps":30,"axis":[-1]})");
        const auto there = future.data()["view"]["player"].integer().value_or(0);
        expect(there > walked - 30) << std::format("{} from {}: not thirty steps back, the first of them were still forward", there, walked);
        (void)ask(session, R"({"op":"world.act","axis":[-1]})");
        const auto played = ask(session, R"({"op":"world.step","steps":30})");
        expect(played.data()["hash"].text() == future.data()["hash"].text()) << "the same steps, played";
    };

    "time is the game's: sought back within what is kept, a new branch from there; outside it, stale"_test = [] {
        Game game(Chase::State{}, {.tier = assist});
        agent::SubjectSession session({&game});
        (void)ask(session, R"({"op":"world.act","axis":[1]})");
        const auto at_twenty = ask(session, R"({"op":"world.step","steps":20})").data()["hash"].text();
        (void)ask(session, R"({"op":"world.step","steps":40})");
        const auto back = ask(session, R"({"op":"world.seek","frame":20})");
        expect(back.data()["frame"].integer() == 20 && back.data()["hash"].text() == at_twenty);
        expect(back.data()["latest"].integer() == 20) << "what came after is another branch's";
        (void)ask(session, R"({"op":"world.step","steps":10})");
        expect(eq(game.state().player, 20)) << "what the player held belonged to the steps left behind";
        expect(ask(session, R"({"op":"world.seek","frame":9999})").error() == "stale");
    };

    "the same requests, the same bits; a game its owner steps refuses to be stepped by requests"_test = [] {
        Game first(Chase::State{}), second(Chase::State{});
        agent::SubjectSession one({&first}), two({&second});
        const auto ended = play_out(one, 400, 12);
        expect(first.over()) << "five catches by chasing the mark";
        expect(ended == play_out(two, 400, 12));

        Game clocked(Chase::State{}, {.stepping = runtime::Stepping::owner});
        agent::SubjectSession session({&clocked});
        expect(ask(session, R"({"op":"world.step"})").error() == "refused");
        clocked.step();
        expect(ask(session, R"({"op":"world.state"})").data()["frame"].integer() == 1) << "its owner's loop steps it";
    };

    "a play recorded is the play again: every step's input, on a game begun the same, the same bits — a seek back forgotten"_test = [] {
        Game played(Chase::State{}, {.record = true});
        agent::SubjectSession session({&played});
        (void)play_out(session, 30, 12);
        // A change of mind: back to step 120, then another way.
        (void)ask(session, R"({"op":"world.seek","frame":120})");
        expect(eq(played.recorded().size(), std::size_t{120})) << "what followed step 120 is another branch's";
        (void)ask(session, R"({"op":"world.act","axis":[-1]})");
        (void)ask(session, R"({"op":"world.step","steps":90})");
        expect(eq(played.recorded().size(), static_cast<std::size_t>(played.frame())));

        // The owner's seat: each step's input as it was given, no lane between.
        Game again(Chase::State{}, {.seat = runtime::Seat::owner});
        for (const Chase::Input& input : played.recorded()) again.step(input);
        expect(eq(again.frame(), played.frame()) && eq(again.state_hash(), played.state_hash())) << "the same bits";

        // The owner's own time: sought and branched as a game's rules need (a rewind).
        expect(again.seek(100) && eq(again.frame(), std::int64_t{100}) && eq(again.timeline().latest(), played.frame()));
        again.branch();
        expect(eq(again.timeline().latest(), std::int64_t{100}) && !again.seek(150)) << "after a branch, what followed is gone";
    };

    "found and played through an endpoint, by name, with no interface in the program"_test = [] {
        Game game(Chase::State{});
        const auto directory = scratch_dir("runtime-endpoint");
        agent::Endpoint endpoint({.name = "chase", .directory = directory, .app = "org.example.chase", .interface_name = "xengine.world@0.1",
                                  .session = [&] { return std::make_unique<agent::SubjectSession>(std::vector<agent::Subject*>{&game}); }});
        auto started = endpoint.start();
        if (!started && started.error().find("not supported") != std::string::npos) {
            std::println("    skipped: local endpoints are not supported here ({})", started.error());
            return;
        }
        expect(fatal(started.has_value())) << (started ? "" : started.error());
        expect(agent::list(directory).at(0).interface_name == "xengine.world@0.1");
        auto connection = agent::Connection::connect("chase", directory);
        expect(fatal(connection.has_value()));
        // One request, its records up to the result: the endpoint pumped meanwhile (this loop is the owner's).
        auto call = [&](std::string_view line) {
            expect(connection->send(line).has_value());
            std::vector<mlibs::json::Document> records;
            for (int turn = 0; turn < 4000; ++turn) {
                endpoint.pump();
                auto got = connection->receive(std::chrono::milliseconds(1));
                if (!got) break;
                if (!*got) continue;
                auto parsed = mlibs::json::Document::parse(**got, "record");
                expect(fatal(parsed.has_value()));
                const bool last = parsed->root()["kind"].text() == "result";
                records.push_back(std::move(*parsed));
                if (last) break;
            }
            return records;
        };
        for (int turn = 0; turn < 400 && !game.over(); ++turn) {
            const auto seen = call(R"({"op":"world.observe"})");
            expect(fatal(seen.size() == 2u)) << std::format("{} records", seen.size());
            const auto view = seen[0].root()["data"]["view"];
            const auto player = view["player"].integer().value_or(0), mark = view["mark"].integer().value_or(0);
            (void)call(std::format(R"({{"op":"world.act","seen":{},"axis":[{}]}})", seen[0].root()["data"]["frame"].integer().value_or(0),
                                   mark > player ? 1 : (mark < player ? -1 : 0)));
            (void)call(R"({"op":"world.step","steps":12})");
        }
        expect(game.over()) << "the game was played to its end over the connection";
        endpoint.stop();
    };
}};
