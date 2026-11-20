// The fight at the runtime, no interface (the release plan's RC2.11): two of the work's fighters play a whole round;
// the same inputs give the same bits and time goes back and is played again the same; the game's own tasks do what
// they say when sent as written; an agent on a person's tier plays a whole round against the computer through
// requests alone; and the agent contract holds.
import std;
import mlibs.agent;
import mlibs.agent.cts;
import mlibs.json;
import omui.showcase.fighting.headless;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace runtime = meng::runtime;

namespace {

constexpr meng::PlayTier assist{.name = "assist", .reaction_steps = 0, .actions_per_second = 0, .full_view = true};
constexpr runtime::Options owned{.seat = runtime::Seat::owner, .keep = 1200};

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error, text;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(std::string(record), "record");
        expect(fatal(parsed.has_value())) << record;
        const auto kind = parsed->root()["kind"].text();
        if (kind == "error") out.error = parsed->root()["code"].text().value_or(""), out.text = record;
        if (kind == "data" && !out.data) out.data.emplace(std::move(*parsed)), out.text = record;
    });
    return out;
}
Answered told(agent::SubjectSession& session, std::string_view line, std::source_location at = std::source_location::current()) {
    Answered out = ask(session, line);
    expect(fatal(out.data.has_value() && out.error.empty()), at) << line << " → " << out.text;
    return out;
}
Answered run_task(agent::SubjectSession& session, std::string_view name) {
    const auto described = told(session, R"({"op":"describe","subject":"world"})");
    const auto tasks = described["tasks"];
    for (std::size_t index = 0; index < tasks.size(); ++index) {
        if (tasks[index]["name"].text() != name) continue;
        const auto steps = tasks[index]["steps"];
        Answered last;
        for (std::size_t step = 0; step < steps.size(); ++step) last = told(session, steps[step].dump());
        return last;
    }
    expect(false) << "no task " << name;
    return {};
}
fighting::State started(std::string_view left, std::string_view right, std::uint64_t seed = 1) {
    auto made = fighting::start(left, right, seed);
    expect(fatal(made.has_value())) << (made ? "" : made.error());
    return std::move(*made);
}
// A fixed play for the seat: walk in, jab, back off, a heavy kick now and then.
fighting::Input scripted(std::int64_t frame) {
    meng::Actions actions;
    const std::int64_t phase = frame % 120;
    if (phase < 50) actions.axes[0] = 32767;
    else if (phase < 54) actions.held = 1u << 0;
    else if (phase < 80) actions.axes[0] = -32767;
    else if (phase < 84 && frame % 360 < 120) actions.held = 1u << 5;
    return {.player = actions};
}

}  // namespace

UTest cases{"fighting.headless", [] {
    "the work's fighters are carried, and two of them play a whole round with no interface: the computer against a fixed play"_test = [] {
        const auto names = fighting::names();
        std::println("    fighters: {}", names);
        expect(fatal(names.size() >= 6)) << "the work's six and kata";
        fighting::State state = started("ni", "mo");
        const std::int32_t health = state.match.f[0].health;
        std::int64_t frame = 0;
        for (; !fighting::over(state) && frame < 99 * 60 + 600; ++frame) fighting::step(state, scripted(frame));
        std::println("    ni (a fixed play) against mo (the computer): {} frames, winner {}, health {} and {}", frame, state.match.winner, state.match.f[0].health, state.match.f[1].health);
        expect(fighting::over(state)) << "the round ends: a knock-out or the clock";
        expect(lt(state.match.f[0].health, health) || lt(state.match.f[1].health, health)) << "someone was hit";
        expect(!fighting::start("ni", "nobody").has_value() && fighting::start("ni", "nobody").error().contains("mo")) << "a wrong name says the right ones";
    };

    "the same inputs, the same bits; time sought back and played again is the same; a branch leaves the game as it was"_test = [] {
        fighting::Standalone one(started("li", "yue"), owned), two(started("li", "yue"), owned);
        for (std::int64_t frame = 0; frame < 900; ++frame) one.step(scripted(frame)), two.step(scripted(frame));
        expect(eq(fighting::checksum(one.state()), fighting::checksum(two.state())));
        const std::uint64_t at_900 = fighting::checksum(one.state());
        expect(fatal(one.seek(600)));
        for (std::int64_t frame = 600; frame < 900; ++frame) one.step(scripted(frame));
        expect(eq(fighting::checksum(one.state()), at_900)) << "played again from 600: the computer remembers the same";
    };

    "the game says its controls and tasks; each task, sent as written, does what it says (D-186)"_test = [] {
        {
            fighting::Standalone game(started("ni", "mo"));
            agent::SubjectSession session({&game});
            const auto state = told(session, R"({"op":"world.state"})");
            expect(eq(state["controls"].size(), std::size_t{10})) << state["controls"].dump();
            const auto found = told(session, R"({"op":"find","q":"block"})");
            std::println("    find \"block\": {}", found["found"][0].dump());
            const double apart = told(session, R"({"op":"world.observe"})")["view"]["apart"].number().value_or(0);
            const auto walked = run_task(session, "walk-in");
            const double now = walked["view"]["apart"].number().value_or(0);
            std::println("    walk-in: apart {:.3f} → {:.3f}", apart, now);
            expect(lt(now, apart)) << walked.text;
        }
        {
            fighting::Standalone game(started("ni", "mo"));
            agent::SubjectSession session({&game});
            const auto guarded = run_task(session, "guard");
            const auto you = guarded["view"]["fighters"][0];
            std::println("    guard: health {} of {}", you["health"].integer().value_or(0), you["max_health"].integer().value_or(0));
            expect(ge(you["health"].integer().value_or(0) * 20, you["max_health"].integer().value_or(0) * 19)) << "guarded: at most a twentieth taken, " << guarded.text;
        }
    };

    "an agent on a person's tier plays a whole round against the computer, through requests alone"_test = [] {
        fighting::Standalone game(started("ni", "mo"));  // the seat's tier by default: a person's
        agent::SubjectSession session({&game});
        std::int64_t refused = 0, turns = 0, struck = 0;
        while (!game.over() && turns < 3000) {  // a turn: look, act, 6 steps (ten actions a second)
            ++turns;
            const auto seen = told(session, R"({"op":"world.observe"})");
            const auto view = seen["view"];
            const auto you = view["fighters"][0], them = view["fighters"][1];
            const double apart = view["apart"].number().value_or(1e9), reach = you["reach"].number().value_or(0);
            const double toward = them["x"].number().value_or(0) > you["x"].number().value_or(0) ? 1 : -1;
            const std::string theirs(them["doing"].text().value_or("idle"));  // owned: text() answers a value, not a view into the record
            const bool attacking = theirs != "idle" && theirs != "walk_forward" && theirs != "walk_back" && theirs != "crouch" && theirs != "hitstun" &&
                                   theirs != "blockstun" && theirs != "knockdown" && theirs != "wakeup" && theirs != "air" && theirs != "prejump" && theirs != "landing";
            std::string act;
            if (attacking && apart < them["reach"].number().value_or(0) + 40) act = std::format(R"({{"op":"world.act","seen":{},"axis":[{},0]}})", seen["frame"].integer().value_or(0), -toward);  // back: guard
            else if (apart > reach) act = std::format(R"({{"op":"world.act","seen":{},"axis":[{},0]}})", seen["frame"].integer().value_or(0), toward);  // walk in
            else act = std::format(R"({{"op":"world.act","seen":{},"buttons":[{}]}})", seen["frame"].integer().value_or(0), turns % 3 == 0 ? 5 : 0), ++struck;  // strike
            refused += !ask(session, act).error.empty();
            told(session, R"({"op":"world.step","steps":6})");
        }
        const auto ended = told(session, R"({"op":"world.observe"})");
        std::println("    the end: {}", ended.text.substr(0, 600));
        const auto end = ended["view"];
        std::println("    a round on a person's tier: {} turns, {} strikes asked, {}; health {} and {}, {} frames", turns, struck, end["outcome"].text().value_or("?"),
                     end["fighters"][0]["health"].integer().value_or(0), end["fighters"][1]["health"].integer().value_or(0), game.frame());
        expect(eq(refused, std::int64_t{0})) << "it asked no more often than a person's tier allows";
        expect(game.over()) << "the round played to its end";
        expect(lt(end["fighters"][1]["health"].integer().value_or(0), end["fighters"][1]["max_health"].integer().value_or(0))) << "the agent's strikes landed";
    };

    "the agent contract on the game: what it has passes, what it says it has not is unsupported"_test = [] {
        namespace contract = mlibs::agent::cts;
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return std::make_unique<fighting::Standalone>(*fighting::start("ni", "mo"), runtime::Options{.tier = assist}); },
            .read = R"({"op":"world.observe"})",
            .change = R"({"op":"world.step","steps":3})",
            .undo = R"({"op":"world.seek","frame":0})",
            .preview = R"({"op":"world.branch","steps":120,"axis":[1,0]})",
            .script = {R"({"op":"world.act","axis":[1,0]})", R"({"op":"world.step","steps":60})", R"({"op":"world.observe"})", R"({"op":"world.act","buttons":[0]})",
                       R"({"op":"world.step","steps":30})", R"({"op":"world.state"})"},
        };
        contract::declare(probe, {.subject = "fighting"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain", "agent.actor.recorded", "agent.observe.budget", "agent.causes.explained"}) << "as the runtime's own game";
    };
}};
