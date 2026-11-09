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
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;
namespace bo = omui::breakout;

namespace {
struct Session {
    Headless h{HeadlessOptions{.level = HeadlessLevel::pixels, .size = {1280, 720}}};
    std::optional<agent::Protocol> protocol;
    Value said;  // the last error record
    explicit Session(std::string_view tier, bool allowed = true, std::int32_t room = 1) {
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
        if (room != 1) {
            h.runtime().set(h.runtime().find("main/field"), "room",
                            Value(static_cast<std::int64_t>(room)));
            h.step();
        }
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
        // Its intent, beside what it holds: where it means the paddle to be, and why.
        const std::string why = waiting   ? "serve"
                                : falling ? std::format("meet the ball at {:.2f}", target)
                                          : "wait in place";
        (void)s.ask("intend", Value{}.set("target", target).set("note", why));
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

UTest cases{"breakout.agent-player", [] {

    mcpplibs::mtest::test(std::string_view("an external agent plays as a person, headless, at the human tier; the same agent, "
              "the same bytes")) = [] {
                  const auto first = play_a_game(120 * 90);
                  std::println("    played {} steps: score {}, over {}, checksum {:016x}, {} looks",
                               first.frames, first.score, first.over, first.checksum,
                               first.log.size());
                  expect(first.score > 0) << "it broke bricks: it played";
                  const auto again = play_a_game(120 * 90);
                  expect(again.checksum == first.checksum && again.frames == first.frames &&
                            again.log == first.log) << "the same agent plays the same game to the same bytes (its inputs replay)";
              };

    "the record of a game, exported headless, replays alone to the same bytes"_test = [] {
        Session session("human");
        Player player;
        while (!session.state().over && session.state().frame < 120 * 90) {
            player.turn(session);
            session.h.advance(seconds(6.0 / 120));
        }
        const Value record = session.data("record");
        expect(record.get("kind") && record.get("kind")->as_string() == "breakout.agent-record" &&
                  record.get("tier")->as_string() == "human") << "the record names its tier";
        const auto plays = record.get("plays") && record.get("plays")->list()
                               ? record.get("plays")->list()->size()
                               : 0;
        expect(plays > 50) << std::format("{} plays recorded", plays);
        std::ofstream(scratch_dir("breakout-agent") / "record.omui.json")
            << record.json();  // the export, for a reviewer
        // A fresh run, no agent thinking: the record alone.
        Session replayed("human");
        const Value taken = replayed.ask("replay", Value{}.set("plays", *record.get("plays")));
        expect(taken.get("exitCode")->as_int() == 0) << std::format("the record is taken: {}", replayed.said.json().substr(0, 300));
        const std::int64_t end = record.get("step")->as_int();
        while (replayed.state().frame < end)
            replayed.h.advance(seconds(6.0 / 120));
        expect(std::format("{:016x}", bo::checksum(replayed.state())) ==
                      record.get("checksum")->as_string() &&
                  replayed.state().frame == end) << "replayed from the record: the same checksum at the same step";
        const Value again = replayed.data("record");
        expect(again.get("plays")->list()->size() == plays) << "and the same plays recorded";
    };

    mcpplibs::mtest::test(std::string_view("the agent's intent drawn over its play and over its replay, exported headless; it "
               "never changes the play")) = [] {
                   // The same agent twice, its intent drawn in one run and not in the other.
                   const auto run = [](bool overlay) {
                       auto session = std::make_unique<Session>("human");
                       session->h.runtime().set(session->h.runtime().find("main/field"),
                                                "agent_overlay", Value(overlay));
                       Player player;
                       while (session->state().frame < 900) {
                           player.turn(*session);
                           session->h.advance(seconds(6.0 / 120));
                       }
                       return session;
                   };
                   auto drawn = run(true), plain = run(false);
                   expect(bo::checksum(drawn->state()) == bo::checksum(plain->state()) &&
                             drawn->state().frame == plain->state().frame) << "the same play with or without its intent drawn";
                   const auto with = drawn->h.png(), without = plain->h.png();
                   expect(with && without && *with != *without) << "the intent is drawn";
                   const Value record = drawn->data("record");
                   const auto& plays = *record.get("plays")->list();
                   expect(std::ranges::any_of(plays,
                                             [](const Value& p) {
                                                 return p.get("target") && p.get("note") &&
                                                        !p.get("note")->as_string().empty();
                                             })) << "the record keeps the intent";
                   // The replay, alone, draws the same intent at the same step: the same pixels.
                   Session replayed("human");
                   replayed.h.runtime().set(replayed.h.runtime().find("main/field"),
                                            "agent_overlay", Value(true));
                   expect(replayed.ask("replay", Value{}.set("plays", *record.get("plays")))
                                 .get("exitCode")
                                 ->as_int() == 0) << "the record is taken";
                   const auto dir = scratch_dir("breakout-agent-intent");
                   std::vector<std::filesystem::path> frames;
                   const std::int64_t end = drawn->state().frame;
                   for (int k = 1; end - replayed.state().frame > 6; ++k) {
                       replayed.h.advance(seconds(6.0 / 120));
                       if (k % 25 == 0 &&
                           frames.size() <
                               6) {  // the process, frame by frame: exported for a reviewer
                           frames.push_back(
                               dir / std::format("intent-{:04}.png", replayed.state().frame));
                           expect(replayed.h.save_png(frames.back()).has_value()) << "a frame exported";
                       }
                   }
                   // The last few steps one at a time: paused, stepped to the very step.
                   replayed.h.runtime().set(replayed.h.runtime().find("main/field"), "paused",
                                            Value(true));
                   while (replayed.state().frame < end) {
                       (void)replayed.ask("step");
                       replayed.h.step();
                   }
                   expect(replayed.state().frame == drawn->state().frame &&
                             bo::checksum(replayed.state()) == bo::checksum(drawn->state())) << std::format("replayed to the same step: {} vs {}, {:016x} vs {:016x}",
                                     replayed.state().frame, drawn->state().frame,
                                     bo::checksum(replayed.state()), bo::checksum(drawn->state()));
                   const auto again = replayed.h.png();
                   expect(again && with && *again == *with) << "the replay's intent, drawn: the very pixels of the live play's";
                   std::println("    {} frames of the replay with its intent in {}", frames.size(),
                                dir.string());
               };

    mcpplibs::mtest::test(std::string_view("the matrix: the human-tier agent plays every room, and plays each the same way "
               "twice")) = [] {
                   Value::List rows;
                   for (std::int32_t room = 1; room <= 5; ++room) {
                       const auto play = [room] {
                           Session session("human", true, room);
                           Player player;
                           while (!session.state().over && session.state().frame < 120 * 60) {
                               player.turn(session);
                               session.h.advance(seconds(6.0 / 120));
                           }
                           return std::tuple{session.state().frame, session.state().score,
                                             session.state().over != 0,
                                             bo::checksum(session.state())};
                       };
                       const auto [frames, score, over, checksum] = play();
                       const auto again = play();
                       expect(score > 0) << std::format("room {}: the agent scores ({})", room, score);
                       expect(std::get<3>(again) == checksum) << std::format("room {}: the same play-through twice", room);
                       rows.push_back(Value{}
                                          .set("room", room)
                                          .set("steps", frames)
                                          .set("score", score)
                                          .set("over", over)
                                          .set("checksum", std::format("{:016x}", checksum)));
                       std::println("    room {}: {} steps, score {}, {}", room, frames, score,
                                    over ? "over" : "playing at the minute");
                   }
                   std::ofstream(scratch_dir("breakout-agent") / "matrix.omui.json")
                       << Value(Value::List(rows)).json();
               };

    mcpplibs::mtest::test(std::string_view("the agent as playtester (Part 2 G): rooms played at a person's tier, a report; each problem's record "
               "replays to the same bytes; the same report every run")) = [] {
                   const auto dir = scratch_dir("breakout-playtest");
                   const bo::PlaytestOptions options{.rooms = {1, 4}, .players = 2, .minutes = 1, .out = dir};
                   const Value report = bo::playtest(options);
                   expect(report.get("kind")->as_string() == "breakout.playtest" && report.get("tier")->as_string() == "human");
                   const auto& rooms = *report.get("rooms")->list();
                   expect(rooms.size() == 2) << "a row a room";
                   for (const Value& r : rooms)
                       expect(r.get("cleared")->as_int() + r.get("out_of_lives")->as_int() + r.get("unfinished")->as_int() == 2 &&
                                 r.get("median_s")->as_double() > 0) << r.json();
                   for (const Value& p : *report.get("problems")->list()) {
                       expect(p.get("replays")->as_bool()) << "a problem's record replays: " + p.json();
                       expect(std::filesystem::exists(dir / std::string(p.get("record")->as_string()))) << "written beside the report";
                   }
                   expect(std::filesystem::exists(dir / "playtest.omui.json"));
                   expect(bo::playtest({.rooms = {1, 4}, .players = 2, .minutes = 1}).json() == report.json()) << "the same report every run";
               };

    "the tiers differ where they say: what it is shown, how soon it acts, how often"_test = [] {
                   Session human("human"), assist("assist");
                   const Value shown = human.data("observe"), all = assist.data("observe");
                   expect(shown.get("tier")->as_string() == "human" && shown.get("balls") &&
                             !shown.get("rules") && !shown.get("hidden")) << "a person's view: what is drawn";
                   expect(all.get("rules") && all.get("hidden")) << "assist: the rules and the hidden bricks too";
                   const std::int64_t step = shown.get("step")->as_int();
                   const Value taken = human.data("play", Value{}
                                                              .set("seen", step)
                                                              .set("axis", 1.0)
                                                              .set("launch", false)
                                                              .set("look", false));
                   expect(taken.get("at") && taken.get("at")->as_int() == step + 12) << "a tenth of a second after what it saw";
                   const Value at_once =
                       assist.data("play", Value{}
                                               .set("seen", all.get("step")->as_int())
                                               .set("axis", 1.0)
                                               .set("launch", false)
                                               .set("look", false));
                   expect(at_once.get("at") &&
                             at_once.get("at")->as_int() == all.get("step")->as_int() + 1) << "assist: the next step";
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
                   expect(accepted == 10) << std::format("ten actions a second at the human tier: {}", accepted);
                   const auto future = human.ask("play", Value{}
                                                             .set("seen", step + 1000)
                                                             .set("axis", 0.0)
                                                             .set("launch", false)
                                                             .set("look", false));
                   expect(future.get("exitCode")->as_int() != 0) << "not a step it has not seen";
               };

    "the application's field offers the human tier as it mounts"_test = [] {
        Session mounted("");  // the app's own setting, untouched
        const Value seen = mounted.data("observe");
        expect(seen.get("tier") && seen.get("tier")->as_string() == "human") << "observe answers at the human tier";
    };

    "with agent players off, an agent cannot join"_test = [] {
        Session off("off");
        expect(off.ask("observe").get("exitCode")->as_int() != 0) << "no view";
        expect(off.ask("play", Value{}
                                  .set("seen", 0)
                                  .set("axis", 1.0)
                                  .set("launch", true)
                                  .set("look", false))
                      .get("exitCode")
                      ->as_int() != 0) << "no play";
    };
}};
