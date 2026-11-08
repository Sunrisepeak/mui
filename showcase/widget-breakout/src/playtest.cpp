// omui-breakout-playtest — the agent as playtester (Part 2 §4.5, G): the
// game plays itself, headless, at a person's tier — the same field, the same
// protocol and the same reaction and rate an external agent has (the field's
// `human` tier), no pixels drawn — once per room and player, players of
// different skill (how often they look, how far their aim is off, from a
// seed: the same report every run). What a designer reads: how often each
// room is cleared, how long it takes, how many lives it costs, its longest
// stretch where nothing is scored (tedium), where the paddle misses. A play
// that ran out of lives or of time, or went quiet too long, is a problem: its
// record (the agent's plays, its tier, the checksum) is written beside the
// report, and replayed here to the same bytes before it is.
//
//   omui-breakout-playtest [--rooms 1,2,3,4,5] [--players 4] [--minutes 2] [--out DIR]
//
// DIR/playtest.omui.json (the report), DIR/<room>-<player>.record.omui.json
// (each problem's record); a summary on standard output.
module omui.showcase.widget_breakout;

import std;
import omui;
import mlibs.math;

namespace omui::breakout {

namespace bo = omui::breakout;

namespace {

constexpr double play_rate = 120;

struct PlaySession {
    Headless h{HeadlessOptions{.level = HeadlessLevel::layout, .size = {1280, 720}}};
    std::optional<agent::Protocol> protocol;
    explicit PlaySession(std::int32_t room) {
        auto access = std::make_shared<AgentAccess>();
        access->set_policy({.mode = AgentMode::development, .source = "breakout-playtest"});
        h.runtime().set_agent_access(std::move(access));
        AppSpec spec = bo::app();
        h.runtime().set_agent_players(spec.agent_players);
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.step();
        if (room != 1) {
            h.runtime().set(h.runtime().find("main/field"), "room", Value(static_cast<std::int64_t>(room)));
            h.step();
        }
        h.runtime().set(h.runtime().find("main/field"), "agent_player", Value(std::string("human")));
        h.advance(seconds(0.6));
        protocol.emplace(h.runtime(), agent::ProtocolOptions{.app = "breakout", .default_actor = "agent:playtester"});
    }
    // One request as an external agent sends it: its data, or its error's text.
    std::expected<Value, std::string> act(std::string_view action, Value args = {}) {
        Value request;
        request.set("op", "act").set("node", "main/field").set("action", std::string(action)).set("args", std::move(args));
        Value data, error;
        std::int64_t exit = 0;
        protocol->request(request.json(), [&](std::string_view line) {
            const Value record = parse_json(line).value_or(Value{});
            if (const Value* d = record.get("data")) data = *d;
            if (record.get("kind") && record.get("kind")->as_string() == "error") error = record;
            if (const Value* x = record.get("exitCode")) exit = x->as_int();
        });
        if (exit != 0) return std::unexpected(error.get("text") ? std::string(error.get("text")->as_string()) : std::string("refused"));
        return data;
    }
    const bo::State& state() { return *h.runtime().component<bo::Field>(h.runtime().find("main/field"))->state(); }
};

// A player of some skill: it looks every `every` steps (no sooner than its
// tier lets it act), aims where the lowest falling ball will be a reaction's
// time on, off by up to `error` of the field's width (seeded).
struct Tester {
    std::int32_t       every = 12;
    double             error = 0;
    mlibs::math::SplitMix64 noise{1};
    std::int64_t       last_play = -1000;
    double             last_ball_x = 0.5;
    void turn(PlaySession& s) {
        const auto seen = s.act("observe");
        if (!seen) return;
        const std::int64_t step = seen->get("step") ? seen->get("step")->as_int() : 0;
        if (step - last_play < every) return;
        const double paddle = seen->get("paddle") ? seen->get("paddle")->as_double() : 0.5;
        double target = paddle, lowest = -1;
        bool waiting = false;
        if (const Value* balls = seen->get("balls"); balls && balls->list())
            for (const Value& ball : *balls->list()) {
                if (ball.get("on_paddle")->as_bool()) {
                    waiting = true;
                    continue;
                }
                const double y = ball.get("y")->as_double();
                if (ball.get("vy")->as_double() > 0 && y > lowest) {
                    lowest = y;
                    last_ball_x = ball.get("x")->as_double();
                    const double off = (mlibs::math::unit24(noise.next()) * 2 - 1) * error;
                    target = std::clamp(last_ball_x + ball.get("vx")->as_double() * every / 1280.0 + off, 0.0, 1.0);
                }
            }
        const double gap = target - paddle;
        const double axis = std::abs(gap) < 0.01 ? 0.0 : std::clamp(gap * 12, -1.0, 1.0);
        if (s.act("play", Value{}.set("seen", step).set("axis", axis).set("launch", waiting).set("look", false))) last_play = step;
    }
};

struct Played {
    std::int32_t room = 0, player = 0;
    std::int64_t steps = 0, score = 0;
    std::int32_t over = 0, lives_lost = 0;
    double       quiet_s = 0;  // the longest stretch with nothing scored
    std::vector<double> misses;  // where the falling ball was when a life was lost (0…1 across)
    std::uint64_t checksum = 0;
    std::string  problem;      // why it is one (empty: none)
    Value        record;
};

Played play(std::int32_t room, std::int32_t player_index, std::int64_t up_to_steps) {
    static constexpr std::array<std::pair<std::int32_t, double>, 4> skills{{{12, 0.0}, {12, 0.04}, {18, 0.06}, {24, 0.1}}};
    PlaySession session(room);
    Tester player;
    player.every = skills[static_cast<std::size_t>(player_index) % skills.size()].first;
    player.error = skills[static_cast<std::size_t>(player_index) % skills.size()].second;
    player.noise = mlibs::math::SplitMix64{0x9e3779b97f4a7c15ull ^ static_cast<std::uint64_t>(room * 131 + player_index)};
    Played g{.room = room, .player = player_index};
    std::int32_t lives = session.state().lives;
    std::int64_t last_score = session.state().score, scored_at = session.state().frame;
    while (!session.state().over && session.state().frame < up_to_steps) {
        player.turn(session);
        session.h.advance(seconds(6.0 / play_rate));
        const bo::State& s = session.state();
        if (s.lives < lives) {
            g.lives_lost += lives - s.lives;
            g.misses.push_back(player.last_ball_x);
            lives = s.lives;
        }
        if (s.score != last_score) {
            g.quiet_s = std::max(g.quiet_s, static_cast<double>(s.frame - scored_at) / play_rate);
            last_score = s.score;
            scored_at = s.frame;
        }
    }
    const bo::State& s = session.state();
    g.quiet_s = std::max(g.quiet_s, static_cast<double>(s.frame - scored_at) / play_rate);
    g.steps = s.frame;
    g.score = s.score;
    g.over = static_cast<std::int32_t>(s.over);
    g.checksum = bo::checksum(s);
    if (g.over == 2) g.problem = "out of lives";
    else if (g.over == 0) g.problem = "not cleared in time";
    else if (g.quiet_s > 30) g.problem = std::format("{:.0f} s with nothing scored", g.quiet_s);
    if (!g.problem.empty())
        if (auto record = session.act("record")) g.record = *record;
    return g;
}

// A problem's record, played alone from the room's start: the same checksum, or it is no evidence.
bool replays(const Played& g) {
    const Value* plays = g.record.get("plays");
    if (!plays) return false;
    PlaySession again(g.room);
    if (!again.act("replay", Value{}.set("plays", *plays))) return false;
    while (!again.state().over && again.state().frame < g.steps) again.h.advance(seconds(6.0 / play_rate));
    return again.state().frame == g.steps && bo::checksum(again.state()) == g.checksum;
}

double median(std::vector<double> values) {
    if (values.empty()) return 0;
    std::ranges::sort(values);
    return values[values.size() / 2];
}

}  // namespace

Value playtest(const PlaytestOptions& options) {
    const std::filesystem::path& out = options.out;
    if (!out.empty()) std::filesystem::create_directories(out);
    const auto up_to = static_cast<std::int64_t>(options.minutes * 60 * play_rate);
    const std::int32_t players = options.players;
    Value::List room_rows, problems;
    for (const std::int32_t room : options.rooms) {
        std::vector<Played> games;
        for (std::int32_t p = 0; p < players; ++p) games.push_back(play(room, p, up_to));
        std::int32_t cleared = 0, lost = 0, unfinished = 0;
        std::vector<double> steps, scores, quiet;
        std::array<std::int32_t, 5> where{};
        for (const Played& g : games) {
            cleared += g.over == 1;
            lost += g.over == 2;
            unfinished += g.over == 0;
            steps.push_back(static_cast<double>(g.steps) / play_rate);
            scores.push_back(static_cast<double>(g.score));
            quiet.push_back(g.quiet_s);
            for (const double x : g.misses) ++where[static_cast<std::size_t>(std::clamp(x, 0.0, 0.999) * 5)];
            if (g.problem.empty()) continue;
            const bool ok = replays(g);
            const std::string file = std::format("{}-{}.record.omui.json", g.room, g.player);
            if (!out.empty()) std::ofstream(out / file) << g.record.json();
            problems.push_back(Value{}.set("room", g.room).set("player", g.player).set("problem", g.problem).set("record", file)
                                   .set("replays", ok).set("checksum", std::format("{:016x}", g.checksum)));
        }
        Value::List misses;
        for (const std::int32_t n : where) misses.emplace_back(static_cast<std::int64_t>(n));
        room_rows.push_back(Value{}.set("room", room).set("players", players).set("cleared", cleared).set("out_of_lives", lost).set("unfinished", unfinished)
                                .set("median_s", median(steps)).set("median_score", median(scores)).set("longest_quiet_s", median(quiet) > 0 ? *std::ranges::max_element(quiet) : 0.0)
                                .set("lives_lost", static_cast<std::int64_t>(std::ranges::fold_left(games, 0, [](int sum, const Played& g) { return sum + g.lives_lost; })))
                                .set("misses_by_fifth", Value(std::move(misses))));
    }
    Value report;
    report.set("kind", "breakout.playtest").set("tier", "human").set("minutes", options.minutes).set("rooms", Value(std::move(room_rows)))
        .set("problems", Value(std::move(problems)));
    if (!out.empty()) std::ofstream(out / "playtest.omui.json") << report.json();
    return report;
}

}  // namespace omui::breakout
