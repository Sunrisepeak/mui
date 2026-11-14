// Swarm at the runtime, no interface: the same inputs give the same bits, time goes back and is played again
// the same, an enemy is shot and the score rises, the game can be lost, the game's own tasks do what they say
// when sent as written, a player that aims and backs away gets through waves, and the agent contract holds.
import std;
import mlibs.agent;
import mlibs.agent.cts;
import mlibs.json;
import omui.showcase.swarm.headless;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace runtime = xengine::runtime;

namespace {

constexpr xengine::PlayTier assist{.name = "assist", .reaction_steps = 0, .actions_per_second = 0, .full_view = true};
// The game played by its owner's input: each step's actions as given, no lane between.
constexpr runtime::Options owned{.seat = runtime::Seat::owner, .keep = 1200};

struct Answered {
    std::optional<mlibs::json::Document> data;
    std::string                          error, text;
    [[nodiscard]] mlibs::json::Node operator[](std::string_view key) const { return data->root()["data"][key]; }
};
Answered ask(agent::SubjectSession& session, std::string_view line) {
    Answered out;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
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

// A fixed play: the player walks a slow square, the gun sweeps round, the button held four steps in five.
swarm::Input scripted(std::int64_t frame) {
    constexpr std::array<std::array<std::int16_t, 2>, 4> ways{{{32767, 0}, {0, 32767}, {-32767, 0}, {0, -32767}}};
    xengine::Actions actions;
    const auto way = ways[static_cast<std::size_t>(frame / 90 % 4)];
    actions.axes[0] = way[0], actions.axes[1] = way[1];
    const std::int64_t turn = frame * 7 % 400;  // round the arena's edge
    const float along = static_cast<float>(turn % 100) / 100.0f;
    const swarm::Vec2 place = turn < 100   ? swarm::Vec2{swarm::arena_width * along, 0}
                              : turn < 200 ? swarm::Vec2{swarm::arena_width, swarm::arena_height * along}
                              : turn < 300 ? swarm::Vec2{swarm::arena_width * (1 - along), swarm::arena_height}
                                           : swarm::Vec2{0, swarm::arena_height * (1 - along)};
    std::tie(actions.pointer_x, actions.pointer_y) = swarm::pointer_at(place);
    if (frame % 5 != 0) actions.held = 1u << swarm::shoot_button;
    return {.player = actions};
}

// The enemy nearest the player (nothing: none in the arena).
std::optional<swarm::Vec2> nearest(const swarm::State& state) {
    std::optional<swarm::Vec2> found;
    float least = 0;
    state.field.each<swarm::Body, swarm::Enemy>([&](xengine::Entity, const swarm::Body& body, const swarm::Enemy&) {
        const swarm::Vec2 apart = body.at - state.player;
        if (const float squared = dot(apart, apart); !found || squared < least) found = body.at, least = squared;
    });
    return found;
}

// Each of a task's requests, sent as `describe` wrote it; the data of the last (its `world.observe`).
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
    expect(fatal(false)) << "no task `" << name << "` among " << tasks.dump();
    return {};
}

}  // namespace

UTest cases{"swarm", [] {
    "the same inputs, the same checksum: two runs agree at every step, and another seed is another game"_test = [] {
        swarm::Standalone first(swarm::start(), owned), second(swarm::start(), owned), other(swarm::start(2), owned);
        bool same = true;
        for (std::int64_t frame = 0; frame < 2400 && same; ++frame) {
            first.step(scripted(frame)), second.step(scripted(frame)), other.step(scripted(frame));
            same = first.state_hash() == second.state_hash();
            if (!same) expect(false) << std::format("frame {}: {:016x} and {:016x}", first.frame(), first.state_hash(), second.state_hash());
        }
        expect(same && first.state().wave >= 2 && first.state().shots > 50 && first.state().kills > 0)
            << std::format("it was a play: wave {}, {} shots, {} kills", first.state().wave, first.state().shots, first.state().kills);
        expect(first.state_hash() != other.state_hash()) << "a wave after the first comes from where the seed says";
        std::println("    2400 steps twice: {:016x} (wave {}, score {}, lives {})", first.state_hash(), first.state().wave, first.state().score, first.state().lives);

        // The same through requests, on a person's tier: the same lines, the same hash.
        const auto played = [] {
            swarm::Standalone game(swarm::start());
            agent::SubjectSession session({&game});
            told(session, R"({"op":"world.act","buttons":[0],"axis":[-1,0.5],"pointer":[0.9,0.4]})");
            told(session, R"({"op":"world.step","steps":500})");
            told(session, R"({"op":"world.act","axis":[1,-1],"pointer":[1,0.6]})");
            return std::string(told(session, R"({"op":"world.step","steps":300})")["hash"].text().value_or(""));
        };
        expect(played() == played());
    };

    "a seek back and the same inputs again: the same bits at every step"_test = [] {
        swarm::Standalone game(swarm::start(), owned);
        std::vector<std::uint64_t> hashes;
        for (std::int64_t frame = 0; frame < 1000; ++frame) {
            game.step(scripted(frame));
            hashes.push_back(game.state_hash());
        }
        expect(fatal(game.seek(300)));
        expect(eq(game.frame(), std::int64_t{300}) && eq(game.state_hash(), hashes[299])) << "back where step 300 left it";
        game.branch();
        std::int64_t differed = -1;
        for (std::int64_t frame = 300; frame < 1000 && differed < 0; ++frame) {
            game.step(scripted(frame));
            if (game.state_hash() != hashes[static_cast<std::size_t>(frame)]) differed = frame;
        }
        expect(eq(differed, std::int64_t{-1})) << "the replay left the first play at this step";
        expect(game.state().enemies > 0 || game.state().kills > 0) << "there was something to get wrong";

        // And through a request: world.seek, then the same requests, the same hash.
        swarm::Standalone asked(swarm::start(), {.tier = assist});
        agent::SubjectSession session({&asked});
        told(session, R"({"op":"world.step","steps":200})");
        told(session, R"({"op":"world.act","buttons":[0],"axis":[0,1],"pointer":[1,0.5]})");
        const std::string once(told(session, R"({"op":"world.step","steps":400})")["hash"].text().value_or(""));
        told(session, R"({"op":"world.seek","frame":200})");
        told(session, R"({"op":"world.act","buttons":[0],"axis":[0,1],"pointer":[1,0.5]})");
        expect(told(session, R"({"op":"world.step","steps":400})")["hash"].text() == once);
    };

    "an enemy is shot: the bullet that meets it removes it, and the score rises by what it is worth"_test = [] {
        swarm::Standalone game(swarm::start(), owned);
        std::int64_t shot_at = -1;
        for (std::int64_t frame = 0; frame < 1200 && shot_at < 0; ++frame) {
            const swarm::State before = game.state();
            xengine::Actions actions;
            if (const auto enemy = nearest(before)) {  // the gun on the nearest enemy, the button held
                std::tie(actions.pointer_x, actions.pointer_y) = swarm::pointer_at(*enemy);
                actions.held = 1u << swarm::shoot_button;
            }
            game.step({.player = actions});
            if (game.state().kills == 0) continue;
            shot_at = game.frame();
            // One fewer of those in the arena and still to come (another of the wave may enter on the same step).
            expect(eq(game.state().kills, 1) && eq(game.state().enemies + game.state().to_come, before.enemies + before.to_come - 1)) << "one enemy fewer";
            expect(eq(game.state().score, before.score + swarm::worth(1)) && eq(before.score, 0)) << "and the first wave's ten points";
            expect(eq(game.state().lives, swarm::first_lives));
        }
        expect(fatal(shot_at > 0)) << "no enemy was shot in ten seconds";
        std::println("    the first enemy shot at step {}, after {} bullets", shot_at, game.state().shots);
        // What a player is shown says so too.
        const auto view = mlibs::json::Document::parse(swarm::observe(game.state(), false), "view");
        expect(fatal(view.has_value()));
        expect(view->root()["score"].integer() == 10 && view->root()["outcome"].text() == "playing" && !view->root()["hidden"]);
        expect(eq(view->root()["enemies"].size(), static_cast<std::size_t>(game.state().enemies)));
    };

    "the game can be lost: a player that does nothing is touched three times, and a game lost stays as it ended"_test = [] {
        swarm::Standalone game(swarm::start(), owned);
        std::int32_t lives = swarm::first_lives;
        std::vector<std::int64_t> touched;
        for (int frame = 0; frame < 6000 && !game.over(); ++frame) {
            game.step({});
            if (game.state().lives < lives) touched.push_back(game.frame()), lives = game.state().lives;
        }
        expect(fatal(game.over())) << "still playing after fifty seconds of standing still";
        expect(eq(touched.size(), std::size_t{3}) && eq(game.state().lives, 0) && eq(game.state().score, 0));
        expect(touched[1] - touched[0] >= swarm::safe_steps && touched[2] - touched[1] >= swarm::safe_steps) << "a touch, then a moment in which nothing costs a life";
        std::println("    lost at step {} (touched at {}, {}, {})", game.frame(), touched[0], touched[1], touched[2]);
        const std::uint64_t ended = game.state_hash();
        const std::int64_t lost_at = game.frame();  // the runtime's count goes on with a step given; the game does not
        game.step(scripted(7));
        expect(eq(game.state_hash(), ended));
        // As an agent sees it: the view says lost, and a step asked for plays none.
        swarm::Standalone asked(swarm::start());
        agent::SubjectSession session({&asked});
        const auto played = told(session, R"({"op":"world.step","steps":3600})");
        expect(played["over"].boolean() == true && played["played"].integer() == lost_at) << played.text;
        const auto seen = told(session, R"({"op":"world.observe"})");
        expect(seen["view"]["outcome"].text() == "lost" && seen["view"]["lives"].integer() == 0) << seen.text;
        expect(told(session, R"({"op":"world.step","steps":10})")["played"].integer() == 0);
    };

    "the game says its controls and its tasks; each task, sent as written through a session, does what it says (D-186)"_test = [] {
        {
            swarm::Standalone game(swarm::start());  // an agent in the seat, on a person's tier
            agent::SubjectSession session({&game});
            const auto state = told(session, R"({"op":"world.state"})");
            expect(eq(state["controls"].size(), std::size_t{6})) << state["controls"].dump();
            expect(state["controls"][2]["name"].text() == "up" && state["controls"][2]["act"].dump() == R"({"axis":[0,-1]})") << state["controls"][2].dump();
            expect(state["controls"][4]["name"].text() == "shoot" && state["controls"][4]["act"].dump() == R"({"buttons":[0]})") << state["controls"][4].dump();
            const auto found = told(session, R"({"op":"find","q":"fire a bullet"})");
            expect(found["found"][0]["kind"].text() == "control" && found["found"][0]["name"].text() == "shoot") << found.text;
            const auto described = told(session, R"({"op":"describe","subject":"world"})");
            expect(described["tasks"][0]["name"].text() == "hold-the-east-gate" && described["tasks"][1]["name"].text() == "sidestep" &&
                   described["tasks"][2]["name"].text() == "stand-and-be-caught")
                << "the game's own tasks first: " << described["tasks"].dump();
            expect(described["tasks"][0]["steps"][0].dump() == R"({"op":"world.act","buttons":[0],"pointer":[1,0.5]})") << described["tasks"][0]["steps"][0].dump();

            // hold-the-east-gate: the first wave, all four, shot as it closes in; no life lost.
            const auto held = run_task(session, "hold-the-east-gate");
            expect(eq(game.state().kills, swarm::wave_size(1)) && eq(game.state().lives, swarm::first_lives)) << held.text;
            expect(held["view"]["score"].integer() == swarm::wave_size(1) * swarm::worth(1)) << "the score it says to see: " << held.text;
        }
        {
            swarm::Standalone game(swarm::start());
            agent::SubjectSession session({&game});
            const swarm::Vec2 from = game.state().player;
            const auto moved = run_task(session, "sidestep");
            const double x = moved["view"]["player"]["x"].number().value_or(0), y = moved["view"]["player"]["y"].number().value_or(0);
            expect(x < from.x - 10 && y < from.y - 10) << std::format("up and left of ({}, {}): ({}, {})", from.x, from.y, x, y);
            const swarm::Vec2 stopped = game.state().player;
            told(session, R"({"op":"world.step","steps":30})");
            expect(game.state().player == stopped) << "and let go: she stands";
        }
        {
            swarm::Standalone game(swarm::start());
            agent::SubjectSession session({&game});
            const auto caught = run_task(session, "stand-and-be-caught");
            expect(caught["view"]["lives"].integer().value_or(3) < swarm::first_lives && eq(game.state().kills, 0)) << caught.text;
        }
    };

    "it plays: an agent shown what a person is, that aims at the nearest enemy and backs away from it, clears waves"_test = [] {
        swarm::Standalone game(swarm::start(), {.most_steps = 60});
        agent::SubjectSession session({&game});
        std::int64_t refused = 0;
        for (int turn = 0; turn < 300 && !game.over(); ++turn) {  // a turn: look, act, 24 steps (five actions a second)
            const auto seen = told(session, R"({"op":"world.observe"})");
            const auto view = seen["view"];
            const double x = view["player"]["x"].number().value_or(0), y = view["player"]["y"].number().value_or(0);
            double near_x = 0, near_y = 0, least = 1e9;
            for (const auto& enemy : view["enemies"].items())
                if (const double away = enemy["away"].number().value_or(1e9); away < least) least = away, near_x = enemy["x"].number().value_or(0), near_y = enemy["y"].number().value_or(0);
            std::string act = R"({"op":"world.act"})";
            if (least < 1e9) {
                // Away from it when it is close, toward the middle otherwise; never faster than the axes go.
                const double toward_x = least < 22 ? x - near_x : swarm::arena_width / 2 - x, toward_y = least < 22 ? y - near_y : swarm::arena_height / 2 - y;
                const double most = std::max({std::abs(toward_x), std::abs(toward_y), 1.0});
                act = std::format(R"({{"op":"world.act","seen":{},"buttons":[0],"axis":[{:.3f},{:.3f}],"pointer":[{:.4f},{:.4f}]}})", seen["frame"].integer().value_or(0),
                                  toward_x / most, toward_y / most, std::clamp(near_x / swarm::arena_width, 0.0, 1.0), std::clamp(near_y / swarm::arena_height, 0.0, 1.0));
            }
            refused += !ask(session, act).error.empty();
            told(session, R"({"op":"world.step","steps":24})");
        }
        std::println("    a minute of it: wave {}, score {}, {} kills of {} shots, {} lives left, step {}", game.state().wave, game.state().score, game.state().kills,
                     game.state().shots, game.state().lives, game.frame());
        expect(eq(refused, std::int64_t{0})) << "it asked no more often than a person's tier allows";
        expect(game.state().wave >= 3 && game.state().kills >= swarm::wave_size(1) + swarm::wave_size(2)) << "the first two waves cleared by shooting";
    };

    "the agent contract on the game: what it has passes, what it says it has not is unsupported"_test = [] {
        namespace contract = mlibs::agent::cts;
        const contract::Probe probe{
            .make = []() -> std::unique_ptr<agent::Subject> { return std::make_unique<swarm::Standalone>(swarm::start(), runtime::Options{.tier = assist}); },
            .read = R"({"op":"world.observe"})",
            .change = R"({"op":"world.step","steps":3})",
            .undo = R"({"op":"world.seek","frame":0})",
            .preview = R"({"op":"world.branch","steps":300,"buttons":[0],"pointer":[1,0.5]})",
            .script = {R"({"op":"world.act","buttons":[0],"axis":[1,0],"pointer":[1,0.5]})", R"({"op":"world.step","steps":400})", R"({"op":"world.observe"})",
                       R"({"op":"world.act","axis":[-1,1]})", R"({"op":"world.step","steps":200})", R"({"op":"world.state"})"},
        };
        contract::declare(probe, {.subject = "swarm"});
        const std::vector<std::string> unsupported = contract::unsupported(probe.make()->capabilities());
        expect(unsupported == std::vector<std::string>{"agent.source.chain", "agent.actor.recorded", "agent.observe.budget", "agent.causes.explained"})
            << "as the runtime's own game: causes, the source of what it puts out, a budget — not there, and said so";
    };
}};
