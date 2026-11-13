// The player on a game that is not Kiln: a corridor walked to its end, with a door a key opens, a fire that
// burns out, and — in the flawed corridor — a second door and still one key. The game gives the player a table
// and is played through the agent kernel's requests only.
import std;
import mlibs.agent;
import mlibs.json;
import xengine.input;
import xengine.runtime;
import xengine.playtest;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace agent = mlibs::agent;
namespace playtest = xengine::playtest;

namespace {

struct Corridor {
    struct State {
        std::int64_t                frame = 0;
        std::int32_t                at = 0, health = 100, keys = 1, spare = 0;
        std::array<std::int32_t, 2> doors{200, 0};  // where a shut door stands; 0: none, or opened
        std::int32_t                fire_from = 400, fire_to = 440;
        std::int64_t                fire_until = 1500;  // the frame it has burned out by
    };
    struct Input {
        xengine::Actions player{};
    };
    static constexpr std::int32_t steps_per_second = 120;
    static constexpr std::int32_t end = 800, reach = 60;
    static void step(State& state, const Input& input) {
        if (over(state)) return;
        ++state.frame;
        const double axis = input.player.axis(0);
        const bool   shut = std::ranges::contains(state.doors, state.at + 1);
        if (axis > 0.5 && !shut) ++state.at;
        if (axis < -0.5 && state.at > 0) --state.at;
        // A key held out opens the door within reach, and is spent.
        if (input.player.down(2) && state.keys > 0)
            for (std::int32_t& door : state.doors)
                if (door > state.at && door - state.at <= reach) {
                    door = 0, --state.keys;
                    break;
                }
        const bool burning = state.frame < state.fire_until && state.at >= state.fire_from && state.at < state.fire_to;
        if (burning) --state.health;
        if (!burning && axis > -0.5 && axis < 0.5 && state.frame % 10 == 0 && state.health < 100) ++state.health;
    }
    static std::uint64_t checksum(const State& state) noexcept {
        std::uint64_t sum = static_cast<std::uint64_t>(state.frame);
        for (const std::int32_t part : {state.at, state.health, state.keys, state.doors[0], state.doors[1]}) sum = sum * 1099511628211ull + static_cast<std::uint32_t>(part);
        return sum;
    }
    static std::string observe(const State& state, bool) {
        return std::format(R"({{"at":{},"health":{},"keys":{},"doors":[{},{}],"burning":{},"outcome":"{}"}})", state.at, state.health, state.keys, state.doors[0], state.doors[1],
                           state.frame < state.fire_until, state.at >= end ? "won" : state.health <= 0 ? "lost" : "");
    }
    static void play(Input& input, const xengine::Actions& actions) { input.player = actions; }
    static bool over(const State& state) noexcept { return state.at >= end || state.health <= 0; }
};
using Game = xengine::runtime::Runtime<Corridor>;

// The corridor, as the player needs it.
struct Table {
    struct View {
        std::int64_t                frame = 0, at = 0, health = 0, keys = 0;
        std::array<std::int64_t, 2> doors{};
        bool                        burning = false;
        std::string                 outcome;
    };
    using Tool = playtest::Tool;
    [[nodiscard]] View read(const mlibs::json::Node& data) const {
        const auto view = data["view"];
        View seen;
        seen.frame = data["frame"].integer().value_or(0);
        seen.at = view["at"].integer().value_or(0), seen.health = view["health"].integer().value_or(0), seen.keys = view["keys"].integer().value_or(0);
        std::size_t index = 0;
        for (const auto& door : view["doors"].items()) seen.doors[index++ % 2] = door.integer().value_or(0);
        seen.burning = view["burning"].boolean() == true;
        seen.outcome = std::string(view["outcome"].text().value_or(""));
        return seen;
    }
    [[nodiscard]] playtest::Vitals vitals(const View& seen) const {
        return {.progress = static_cast<double>(seen.at), .health = static_cast<double>(seen.health), .frame = seen.frame, .won = seen.outcome == "won", .lost = seen.outcome == "lost"};
    }
    [[nodiscard]] std::span<const std::string_view> ways() const {
        static constexpr std::array<std::string_view, 1> ways{R"("axis":[1])"};
        return ways;
    }
    [[nodiscard]] std::string_view back() const { return R"("axis":[-1])"; }
    [[nodiscard]] std::string_view stay() const { return R"("axis":[0])"; }
    [[nodiscard]] std::string_view surface() const { return R"("axis":[1])"; }
    [[nodiscard]] bool             passes(const View& seen) const { return seen.burning; }
    // A shut door within a hundred cells of where she was blocked.
    [[nodiscard]] int in_the_way(const View& seen, const View& blocked) const {
        int count = 0;
        for (const std::int64_t door : seen.doors) count += door > blocked.at && door - blocked.at <= 100 ? 5 : 0;
        return count;
    }
    // A knock costs nothing and opens nothing; a key opens a door and is spent.
    [[nodiscard]] std::span<const Tool> tools() const {
        static constexpr std::array<Tool, 2> tools{{{"knock", 30}, {"key", 30}}};
        return tools;
    }
    [[nodiscard]] bool        suits(const Tool& tool, const View& seen) const { return tool.name == "knock" || seen.keys > 0; }
    [[nodiscard]] bool        ready(const Tool&, playtest::Hands&) const { return true; }
    [[nodiscard]] std::size_t aims() const { return 1; }
    [[nodiscard]] std::string use(const Tool& tool, std::size_t, const View&) const { return std::format(R"("axis":[0],"buttons":[{}])", tool.name == "knock" ? 1 : 2); }
    [[nodiscard]] std::string aim(std::size_t) const { return "(the door)"; }
    [[nodiscard]] std::string where(const View& seen) const { return std::format("cell {}", seen.at); }
    [[nodiscard]] std::string has(const View& seen) const { return std::format("{} keys", seen.keys); }
};

playtest::Report played(const Corridor::State& from) {
    Game                  game(from, {.keep = 600, .most_steps = 1200});
    agent::SubjectSession session({&game});
    playtest::Hands       hands(session);
    return playtest::play(hands, Table{}, {.say = false});
}
bool said(const playtest::Report& report, std::string_view words) {
    return std::ranges::any_of(report.log, [&](const std::string& line) { return line.contains(words); });
}

}  // namespace

UTest cases{"xengine.playtest", [] {
    "a corridor is walked to its end: the door by the cheapest tool that opens it, the fire by waiting for it"_test = [] {
        const playtest::Report report = played({});
        expect(fatal(report.won)) << report.why;
        expect(report.why.starts_with("won at frame")) << report.why;
        // A knock was tried on a branch and left the door shut; the key was what was used.
        expect(said(report, "key at (the door), in the way 5 → 0"));
        expect(!said(report, "knock at"));
        // The fire was a hazard and not an obstacle: no tool spent on it, she backed off until it had burned out.
        expect(said(report, "hurts; back off and let it cool"));
        const auto frame = std::stoll(report.why.substr(std::string_view("won at frame ").size()));
        expect(frame > 1500) << frame;
    };

    "the same play twice is the same play, a line for a line"_test = [] {
        const playtest::Report first = played({}), second = played({});
        expect(first.log == second.log);
        expect(first.why == second.why);
    };

    "with a second door and still one key, she stops at it and says where and why"_test = [] {
        Corridor::State flawed;
        flawed.doors = {200, 600};
        const playtest::Report report = played(flawed);
        expect(!report.won);
        expect(report.why.starts_with("stuck at cell 599,")) << report.why;
        expect(report.why.contains("5 in the way ahead")) << report.why;
        expect(report.why.contains("0 keys")) << report.why;
        // What she tried is in the log: the knock, and what it left.
        expect(said(report, "tried knock(the door): 5 left"));
    };

    "hurt, she mends before anything else"_test = [] {
        Corridor::State hurt;
        hurt.health = 30;
        const playtest::Report report = played(hurt);
        expect(fatal(!report.log.empty()));
        expect(report.log.front().contains("back off and mend")) << report.log.front();
        expect(report.won) << report.why;
    };

    "a way that only hurts is never taken: a fire that does not burn out stops her, unharmed"_test = [] {
        Corridor::State lasting;
        lasting.fire_until = 1'000'000;
        const playtest::Report report = played(lasting);
        expect(!report.won);
        expect(report.why.starts_with("stuck at cell")) << report.why;
        expect(!said(report, "hurt (")) << "she was never hurt enough to need mending";
    };
}};
