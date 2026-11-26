// An out-of-tree game on meng's own packages (the closure checklist's item 9), materialised outside the repository by
// `omui-devtools consumer meng`: a field of cells a spark sets alight (meng.cellular, the engine's kernel) is the state
// of a game on meng.runtime (the game layer), played through the agent kernel as an agent plays it — acted on,
// stepped, observed, branched ahead and sought back — with no omui, hanim or renderer in its graph. One JSON line
// says what held; the probe compares it across profiles.
import std;
import mlibs.agent;
import mlibs.json;
import mlibs.hash;
import meng.input;
import meng.runtime;
import meng.cellular;

namespace {

namespace agent = mlibs::agent;
namespace ca = meng::cellular;

constexpr std::uint32_t side = 24;

// A cell catches from a burning neighbour: the cellular README's first rule. Prepared once.
const ca::Program* spread() {
    static const std::optional<ca::Program> program = [] {
        const std::array rules{ca::Rule{{ca::Condition::neighbors({0, {1, 1}}, ca::cardinal, {1, 4})}, {{0, ca::WriteKind::set, 1}}}};
        auto prepared = ca::Program::prepare(1, rules);
        return prepared ? std::optional<ca::Program>(std::move(*prepared)) : std::nullopt;
    }();
    return program ? &*program : nullptr;
}

// The player walks along the field's middle row and sets a spark down with its button; the fire spreads a cell every
// fourth step; it is over when every cell burns.
struct Spark {
    struct State {
        std::int64_t                            frame = 0;
        std::int32_t                            x = side / 2;
        std::uint32_t                           lit = 0;
        std::array<std::uint32_t, side * side> cells{};
    };
    struct Input {
        meng::Actions player{};
    };
    static constexpr std::int32_t steps_per_second = 60;
    static void step(State& state, const Input& input) {
        ++state.frame;
        if (input.player.axis(0) > 0.5 && state.x + 1 < static_cast<std::int32_t>(side)) ++state.x;
        if (input.player.axis(0) < -0.5 && state.x > 0) --state.x;
        if (input.player.down(0)) state.cells[static_cast<std::size_t>(side / 2 * side) + static_cast<std::size_t>(state.x)] = 1;
        if (state.frame % 4 == 0)
            if (const ca::Program* program = spread()) {
                std::array<std::uint32_t, side * side> next{};
                if (program->step({side, side}, state.cells, next)) state.cells = next;
            }
        state.lit = static_cast<std::uint32_t>(std::ranges::count(state.cells, 1u));
    }
    static std::uint64_t checksum(const State& state) noexcept {
        const std::array<std::uint64_t, 3> head{static_cast<std::uint64_t>(state.frame), static_cast<std::uint64_t>(static_cast<std::uint32_t>(state.x)), state.lit};
        return mlibs::hash::fnv1a(std::as_bytes(std::span(head))) ^ mlibs::hash::fnv1a(std::as_bytes(std::span(state.cells)));
    }
    static std::string observe(const State& state, bool) { return std::format(R"({{"x":{},"lit":{}}})", state.x, state.lit); }
    static void play(Input& input, const meng::Actions& actions) { input.player = actions; }
    static bool over(const State& state) noexcept { return state.lit == side * side; }
};
using Game = meng::runtime::Runtime<Spark>;

constexpr meng::PlayTier assist{.name = "assist", .reaction_steps = 0, .actions_per_second = 0, .full_view = true};

// One request's data (its first record's), or null where it answered an error.
std::optional<mlibs::json::Document> ask(agent::SubjectSession& session, std::string_view line) {
    std::optional<mlibs::json::Document> data;
    bool failed = false;
    session.request(line, [&](std::string_view record) {
        auto parsed = mlibs::json::Document::parse(record, "record");
        if (!parsed) return void(failed = true);
        if (parsed->root()["kind"].text() == "error") failed = true;
        if (!data) data = std::move(*parsed);
    });
    return failed ? std::nullopt : std::move(data);
}

std::string text(const std::optional<mlibs::json::Document>& answer, std::string_view field) {
    return answer ? std::string(answer->root()["data"][field].text().value_or("")) : std::string();
}
std::int64_t number(const std::optional<mlibs::json::Document>& answer, std::string_view field) {
    return answer ? answer->root()["data"][field].integer().value_or(-1) : -1;
}

// The same script on a game: walk right three cells, set the spark down, let go, let it burn. The hash at the
// spark's frame and at the end.
std::pair<std::string, std::string> play(agent::SubjectSession& session) {
    for (const std::string_view line : {R"({"op":"world.act","axis":[1]})", R"({"op":"world.step","steps":3})", R"({"op":"world.act","buttons":[0]})",
                                        R"({"op":"world.step","steps":1})", R"({"op":"world.act"})"})
        (void)ask(session, line);
    const std::string sparked = text(ask(session, R"({"op":"world.state"})"), "hash");
    (void)ask(session, R"({"op":"world.step","steps":40})");
    return {sparked, text(ask(session, R"({"op":"world.state"})"), "hash")};
}

}  // namespace

int main() {
    Game game(Spark::State{}, meng::runtime::Options{.tier = assist});
    agent::SubjectSession session({&game});
    const auto [sparked, ended] = play(session);
    const auto seen = ask(session, R"({"op":"world.observe"})");
    const std::int64_t lit = seen ? seen->root()["data"]["view"]["lit"].integer().value_or(-1) : -1;
    const std::int64_t frame = number(ask(session, R"({"op":"world.state"})"), "frame");

    // The same requests on another game: the same bits.
    Game again(Spark::State{}, meng::runtime::Options{.tier = assist});
    agent::SubjectSession other({&again});
    const bool same_bits = play(other) == std::pair{sparked, ended};

    // What would be is a branch: the game itself untouched.
    const auto ahead = ask(session, R"({"op":"world.branch","steps":20})");
    const std::int64_t ahead_lit = ahead ? ahead->root()["data"]["view"]["lit"].integer().value_or(-1) : -1;
    const bool branch_untouched = ahead && ahead_lit > lit && text(ask(session, R"({"op":"world.state"})"), "hash") == ended;

    // Time is the game's: sought back to the spark's frame, the hash there.
    const auto sought = ask(session, R"({"op":"world.seek","frame":4})");
    const bool seek_equal = sought && text(ask(session, R"({"op":"world.state"})"), "hash") == sparked;

    const bool ok = spread() && !ended.empty() && frame == 44 && lit > 1 && same_bits && branch_untouched && seek_equal;
    std::println(R"({{"kind":"meng-consumer","ok":{},"frame":{},"lit":{},"ahead_lit":{},"hash":"{}","same_bits":{},"branch_untouched":{},"seek_equal":{}}})", ok,
                 frame, lit, ahead_lit, ended, same_bits, branch_untouched, seek_equal);
    return ok ? 0 : 1;
}
