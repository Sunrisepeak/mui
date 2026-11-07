// The first Agent player (convergence plan §8.4): an agent outside the game,
// speaking the agent protocol's JSON lines as over the endpoint, plays the
// bottom paddle the way a person does — the arrows and Space, through
// `play` — at the `human` tier: shown what is drawn (`observe`), its actions
// taking effect 100 ms after what it saw, ten a second at most. Headless, no
// GUI. The same agent twice plays the same game to the same bytes; the tiers
// differ where they say; with agent players off it cannot join.
import std;
import omui;
import omui.showcase.widget_breakout;
import mlibs.testing;

using namespace mlibs::testing;
using namespace omui;
namespace bo = omui::breakout;

namespace {
struct Session {
    Headless h{HeadlessOptions{.level = HeadlessLevel::pixels, .size = {1280, 720}}};
    std::optional<agent::Protocol> protocol;
    explicit Session(std::string_view tier, bool allowed = true) {
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development, .source = "agent-player-test"});
        h.runtime().set_agent_access(std::move(access));
        AppSpec spec = bo::app();
        h.runtime().set_agent_players(
            spec.agent_players &&
            allowed);  // as omui::run does: the application's, unless the person said no
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.step();
        if (!tier.empty())
            h.runtime().set(h.runtime().find("main/field"), "agent_player",
                            Value(std::string(tier)));
        h.advance(seconds(0.6));
        protocol.emplace(h.runtime(), agent::ProtocolOptions{.app = "breakout",
                                                             .default_actor = "agent:player"});
    }
    // One request, as an external agent sends it; its last record.
    Value ask(std::string_view action, Value args = {}) {
        Value request;
        request.set("op", "act")
            .set("node", "main/field")
            .set("action", std::string(action))
            .set("args", std::move(args));
        Value last;
        protocol->request(request.json(), [&](std::string_view line) {
            last = parse_json(line).value_or(Value{});
        });
        return last;
    }
    Value data(std::string_view action, Value args = {}) {
        Value request;
        request.set("op", "act")
            .set("node", "main/field")
            .set("action", std::string(action))
            .set("args", std::move(args));
        Value found;
        protocol->request(request.json(), [&](std::string_view line) {
            const auto record = parse_json(line).value_or(Value{});
            if (const Value* d = record.get("data"))
                found = *d;
        });
        return found;
    }
    const bo::State& state() {
        return *h.runtime().component<bo::Field>(h.runtime().find("main/field"))->state();
    }
};

// A person-like agent: it looks twenty times a second, holds an arrow toward
// where the lowest falling ball is (or the middle) and presses Space when a
// ball waits on the paddle; ten actions a second at most, as its tier allows.
struct Player {
    std::int64_t last_play = -1000;
    std::vector<std::string> log;
    void turn(Session& s) {
        const Value seen = s.data("observe");
        const std::int64_t step = seen.get("step") ? seen.get("step")->as_int() : 0;
        if (step - last_play < 12)
            return;  // ten a second: once every twelve steps
        const double paddle = seen.get("paddle") ? seen.get("paddle")->as_double() : 0.5;
        double target = 0.5;
        bool waiting = false, falling = false;
        double lowest = -1;
        if (const Value* balls = seen.get("balls"); balls && balls->list())
            for (const Value& ball : *balls->list()) {
                if (ball.get("on_paddle")->as_bool()) {
                    waiting = true;
                    continue;
                }
                const double y = ball.get("y")->as_double();
                if (ball.get("vy")->as_double() > 0 && y > lowest) {
                    lowest = y;
                    // Lead it by where it goes in a tenth of a second (its reaction).
                    target = std::clamp(ball.get("x")->as_double() +
                                            ball.get("vx")->as_double() * 12 / 1280.0,
                                        0.0, 1.0);
                    falling = true;
                }
            }
        if (!falling && !waiting)
            target = paddle;
        const double gap = target - paddle;
        const double axis = std::abs(gap) < 0.01 ? 0.0 : std::clamp(gap * 12, -1.0, 1.0);
        const Value answer = s.ask("play", Value{}
                                               .set("seen", step)
                                               .set("axis", axis)
                                               .set("launch", waiting)
                                               .set("look", false));
        if (answer.get("exitCode") && answer.get("exitCode")->as_int() == 0)
            last_play = step;
        log.push_back(std::format("{} {:.2f} {}", step, axis, waiting));
    }
};

struct Outcome {
    std::int64_t frames = 0, score = 0;
    std::uint64_t checksum = 0;
    bool over = false;
    std::vector<std::string> log;
};
Outcome play_a_game(std::int64_t up_to_frames) {
    Session session("human");
    Player player;
    while (!session.state().over && session.state().frame < up_to_frames) {
        player.turn(session);
        session.h.advance(seconds(6.0 / 120));  // six steps: it looks twenty times a second
    }
    return {session.state().frame, session.state().score, bo::checksum(session.state()),
            session.state().over != 0, player.log};
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite{"breakout.agent-player"};

    suite
        .test("an external agent plays as a person, headless, at the human tier; the same agent, "
              "the same bytes",
              [] {
                  const auto first = play_a_game(120 * 90);
                  std::println("    played {} steps: score {}, over {}, checksum {:016x}, {} looks",
                               first.frames, first.score, first.over, first.checksum,
                               first.log.size());
                  check(first.score > 0, "it broke bricks: it played");
                  const auto again = play_a_game(120 * 90);
                  check(again.checksum == first.checksum && again.frames == first.frames &&
                            again.log == first.log,
                        "the same agent plays the same game to the same bytes (its inputs replay)");
              });

    suite.test("the tiers differ where they say: what it is shown, how soon it acts, how often",
               [] {
                   Session human("human"), assist("assist");
                   const Value shown = human.data("observe"), all = assist.data("observe");
                   check(shown.get("tier")->as_string() == "human" && shown.get("balls") &&
                             !shown.get("rules") && !shown.get("hidden"),
                         "a person's view: what is drawn");
                   check(all.get("rules") && all.get("hidden"),
                         "assist: the rules and the hidden bricks too");
                   const std::int64_t step = shown.get("step")->as_int();
                   const Value taken = human.data("play", Value{}
                                                              .set("seen", step)
                                                              .set("axis", 1.0)
                                                              .set("launch", false)
                                                              .set("look", false));
                   check(taken.get("at") && taken.get("at")->as_int() == step + 12,
                         "a tenth of a second after what it saw");
                   const Value at_once =
                       assist.data("play", Value{}
                                               .set("seen", all.get("step")->as_int())
                                               .set("axis", 1.0)
                                               .set("launch", false)
                                               .set("look", false));
                   check(at_once.get("at") &&
                             at_once.get("at")->as_int() == all.get("step")->as_int() + 1,
                         "assist: the next step");
                   int accepted = 1;
                   for (int more = 0; more < 12; ++more)
                       accepted += human
                                       .ask("play", Value{}
                                                        .set("seen", step)
                                                        .set("axis", 0.0)
                                                        .set("launch", false)
                                                        .set("look", false))
                                       .get("exitCode")
                                       ->as_int() == 0;
                   check(accepted == 10,
                         std::format("ten actions a second at the human tier: {}", accepted));
                   const auto future = human.ask("play", Value{}
                                                             .set("seen", step + 1000)
                                                             .set("axis", 0.0)
                                                             .set("launch", false)
                                                             .set("look", false));
                   check(future.get("exitCode")->as_int() != 0, "not a step it has not seen");
               });

    suite.test("the application's field offers the human tier as it mounts", [] {
        Session mounted("");  // the app's own setting, untouched
        const Value seen = mounted.data("observe");
        check(seen.get("tier") && seen.get("tier")->as_string() == "human",
              "observe answers at the human tier");
    });

    suite.test("with agent players off, an agent cannot join", [] {
        Session off("off");
        check(off.ask("observe").get("exitCode")->as_int() != 0, "no view");
        check(off.ask("play", Value{}
                                  .set("seen", 0)
                                  .set("axis", 1.0)
                                  .set("launch", true)
                                  .set("look", false))
                      .get("exitCode")
                      ->as_int() != 0,
              "no play");
    });

    return suite.run(argc, argv);
}
