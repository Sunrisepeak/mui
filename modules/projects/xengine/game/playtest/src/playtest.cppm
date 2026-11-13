// xengine.playtest — a game played by trying ahead (the foundation plan's §6.14).
//
//     xengine::playtest::Hands hands(session);                 // a session on the game's `world` subject
//     const xengine::playtest::Report report = xengine::playtest::play(hands, game);
//     // report.won, or report.why: "stuck at x 111.5, frame 4226: 20 in the way ahead; 0 bombs, 100 mana; …"
//
// The player is shown what a person is shown and does what a person can: it never reads the game's state, only
// `world.observe`; it tries a thing on a branch (`world.branch`) before doing it (`world.act`, `world.step`).
// A turn, in this order — the first that applies:
//
//   0  hurt: out of harm's way, and mend (afloat: up and out, where backing off is drowning)
//   1  forward: each way of the game's, tried a stride ahead and further; the one that gets furthest unharmed
//   2a the way is open but would hurt, and what hurts passes with time: back off — as far as was tried and
//      leaves her standing — and let it
//   2  blocked: each tool that suits what is ahead, at each aim, over its own time; the first that clears the way
//   3  nothing helps now: wait where waiting costs nothing, a few times; then say where it is stuck and why
//
// What is the game's is a table (`Game`): how to read a view, its ways forward, its tools and aims, what stands
// in the way. What is the player's is here, and the numbers of it are `Policy`.
export module xengine.playtest;

import std;
export import mlibs.agent;
export import mlibs.json;

export namespace xengine::playtest {

// What any game's view says of the one who plays.
struct Vitals {
    double       progress = 0;  // how far along: forward is more
    double       health = 0;
    std::int64_t frame = 0;
    bool         won = false, lost = false;
    bool         afloat = false;  // where standing still is not safe and backing off is not out (in water)
};
// Something to try when blocked: its name, and how long it takes to work (a fire takes seconds to open wood).
struct Tool {
    std::string_view name;
    int              window = 120;
};
struct Policy {
    int    turns = 160;
    int    stride = 40, further = 120;        // steps a way forward is tried and taken for; and how far on it is looked
    double gain = 2;                          // progress in a stride that counts as forward
    double harm = 10, harm_further = 25;      // health a stride may cost; and the look further on
    double least = 40;                        // health a stride may not leave her below
    double hurt = 60;                         // below it: out of harm's way and mend, before anything
    int    mend_back = 30, mend_wait = 300, surface = 60;
    int    cool_tries = 12, cool_back = 30, cool_wait = 180;
    int    cool_piece = 15, cool_pieces = 4;  // where the whole way back is not safe: so many steps at a time, so many times
    int    tool_back = 20, tool_settle = 10;  // steps back, and standing, before a tool is tried
    int    tool_press = 2;                    // steps a tool's action is held for before she stands while it works
    double tool_least = 50;                   // health a tool's use may not leave her below
    int    clears = 3;                        // how much less in the way counts as clearing it
    int    waits = 5, wait = 240;
    double wait_cost = 25;                    // health a wait may cost
    bool   say = true;                        // each thing done printed as it is done (a playtest is long)
};
struct Report {
    bool                     won = false;
    std::string              why;
    std::vector<std::string> log;
};

// The player's hands and eyes: requests to the game's `world`, as a person's tier allows.
class Hands {
public:
    explicit Hands(mlibs::agent::SubjectSession& session) : session_(&session) {}
    void act(std::string_view what);                    // `what`: an action's members, as JSON (`"axis":[1]`)
    void step(int steps);
    // What is shown now; and what would be after `steps` of `what`, with nothing done. The record's `data`.
    [[nodiscard]] mlibs::json::Document observe();
    [[nodiscard]] mlibs::json::Document branch(int steps, std::string_view what);
    // The same, `what` and `after` so many steps `then`: a press and its release, tried as they would be done.
    [[nodiscard]] mlibs::json::Document branch(int steps, std::string_view what, int after, std::string_view then);
    // False once a request was answered with no record or with an error: the game is not there to play, or did
    // not do what was asked (`problem()` says which request's answer).
    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] std::string_view problem() const noexcept { return problem_; }

private:
    [[nodiscard]] mlibs::json::Document ask(std::string_view line);
    mlibs::agent::SubjectSession* session_;
    bool                          ok_ = true;
    std::string                   problem_;
};

// What a game gives the player.
template <class G>
concept Game = requires(const G& game, const typename G::View& view, const mlibs::json::Node& data, const Tool& tool, Hands& hands, std::size_t aim) {
    { game.read(data) } -> std::same_as<typename G::View>;              // an observation's `data`, as the game reads it
    { game.vitals(view) } -> std::same_as<Vitals>;
    { game.ways() } -> std::convertible_to<std::span<const std::string_view>>;   // ways forward, each an action's members
    { game.back() } -> std::convertible_to<std::string_view>;           // away from what is ahead
    { game.stay() } -> std::convertible_to<std::string_view>;
    { game.surface() } -> std::convertible_to<std::string_view>;        // up and out, afloat
    { game.passes(view) } -> std::same_as<bool>;                        // what would hurt ahead passes with time
    { game.in_the_way(view, view) } -> std::same_as<int>;               // of where she was blocked (the second view), as the first shows it
    { game.tools() } -> std::convertible_to<std::span<const Tool>>;     // cheapest first
    { game.suits(tool, view) } -> std::same_as<bool>;                   // she has it, and it answers what is ahead
    { game.ready(tool, hands) } -> std::same_as<bool>;                  // taken in hand, for real (it can be put back)
    { game.aims() } -> std::same_as<std::size_t>;
    { game.use(tool, aim, view) } -> std::convertible_to<std::string>;  // the action that uses it at an aim, from where she was blocked
    { game.aim(aim) } -> std::convertible_to<std::string>;              // an aim, for the report
    { game.where(view) } -> std::convertible_to<std::string>;           // "x 111.5"
    { game.has(view) } -> std::convertible_to<std::string>;             // "0 bombs, 100 mana"
};

// Said as it happens, and kept.
void note(Report& report, const Policy& policy, std::chrono::steady_clock::time_point began, std::string line);

template <Game G>
[[nodiscard]] Report play(Hands& hands, const G& game, const Policy& policy = {}) {
    using View = typename G::View;
    Report report;
    const auto began = std::chrono::steady_clock::now();
    const auto say = [&](std::string line) { note(report, policy, began, std::move(line)); };
    const auto now = [&] { return game.read(hands.observe().root()["data"]); };
    const auto ahead = [&](int steps, std::string_view what) { return game.read(hands.branch(steps, what).root()["data"]); };
    int waits = 0;
    double reached = 0;  // the furthest she has been: ground regained after backing off is not progress
    for (int turn = 0; turn < policy.turns && hands.ok(); ++turn) {
        const View seen = now();
        const Vitals vitals = game.vitals(seen);
        if (vitals.won) {
            report.won = true, report.why = std::format("won at frame {}", vitals.frame);
            return report;
        }
        if (vitals.lost) {
            report.why = std::format("lost at {} (frame {})", std::string(game.where(seen)), vitals.frame);
            return report;
        }
        // 0 hurt: first out of harm's way, and mend — afloat, up and out (backing off there is drowning).
        if (vitals.health < policy.hurt && vitals.afloat) {
            hands.act(game.surface());
            hands.step(policy.surface);
            say(std::format("frame {}: hurt afloat ({}), up and out", vitals.frame, vitals.health));
            continue;
        }
        if (vitals.health < policy.hurt) {
            hands.act(game.back());
            hands.step(policy.mend_back);
            hands.act(game.stay());
            hands.step(policy.mend_wait);
            say(std::format("frame {}: hurt ({}), back off and mend", vitals.frame, vitals.health));
            continue;
        }
        // 1 forward, a stride at a time (so she stops short of what would hurt her and sees it): the way that
        // gets furthest without much harm.
        std::optional<std::string_view> best;
        double furthest = vitals.progress + policy.gain;
        bool harmful = false;  // the way is open but hurts: a hazard, not an obstacle
        for (const std::string_view way : std::span<const std::string_view>(game.ways())) {
            const Vitals future = game.vitals(ahead(policy.stride, way));
            if (future.lost || future.health < vitals.health - policy.harm || future.health < policy.least) {
                harmful = harmful || future.progress > vitals.progress + policy.gain;
                continue;
            }
            // And further the same way: a stride that is safe itself but leads where she cannot stay is not taken.
            if (!future.won && !vitals.afloat && !future.afloat) {
                const Vitals beyond = game.vitals(ahead(policy.further, way));
                if (beyond.lost || beyond.health < vitals.health - policy.harm_further) {
                    harmful = harmful || future.progress > vitals.progress + policy.gain;
                    continue;
                }
            }
            if (future.won || future.progress > furthest) furthest = future.won ? 1e9 : future.progress, best = way;
        }
        if (best) {
            hands.act(*best);
            hands.step(policy.stride);
            if (furthest > reached + 1) {
                say(std::format("frame {}: forward to {:.1f}", vitals.frame, furthest));
                reached = furthest, waits = 0;
            }
            continue;
        }
        // 2a the way is open but would hurt her, and what hurts passes: back off and let it.
        // Backing off is a step like any other, tried before it is taken. The whole way back where that leaves
        // her standing and unharmed; where it does not (water behind her, or she is in what hurts already): a
        // piece at a time, for real, asking at each place whether standing there is safe — and waiting only
        // where it is.
        if (harmful && game.passes(seen) && waits < policy.cool_tries) {
            const auto safe_to_stand = [&] {
                const Vitals later = game.vitals(ahead(policy.cool_wait, game.stay()));
                const Vitals here = game.vitals(now());
                return !later.lost && later.health >= here.health - policy.wait_cost;
            };
            const Vitals there = game.vitals(ahead(policy.cool_back, game.back()));
            int backed = 0;
            bool stands = false;
            if (!there.lost && !there.afloat && there.health >= vitals.health - policy.harm) {
                hands.act(game.back());
                hands.step(policy.cool_back);
                backed = policy.cool_back, stands = true;
            } else {
                for (int piece = 0; piece <= policy.cool_pieces && !(stands = safe_to_stand()); ++piece) {
                    if (piece == policy.cool_pieces) break;
                    hands.act(game.back());
                    hands.step(policy.cool_piece);
                    backed += policy.cool_piece;
                }
            }
            if (stands) {
                ++waits;
                hands.act(game.stay());
                hands.step(policy.cool_wait);
                say(std::format("frame {}: the way ahead of {:.1f} hurts; back off {}{} and let it cool", vitals.frame, vitals.progress, backed,
                                backed == policy.cool_back && there.health >= vitals.health - policy.harm && !there.afloat && !there.lost ? "" : ", to where she can stand,"));
                continue;
            }
            say(std::format("frame {}: the way ahead of {:.1f} hurts, and {} steps back there is still nowhere to stand", vitals.frame, vitals.progress, backed));
        }
        // 2 blocked: each tool that suits what she sees ahead, at each aim, over its own time; the first that clears the way.
        struct Try {
            Tool        tool;
            std::size_t aim = 0;
            int         left = 0;
        };
        std::optional<Try> chosen;
        const int before = game.in_the_way(seen, seen);
        std::string tried;  // what each try came to, for the report
        // A few steps back first, for real, and stand: what a tool does beside her would hurt her, and from
        // there she still sees what was in her way. Every aim is at the place she was blocked.
        hands.act(game.back());
        hands.step(policy.tool_back);
        hands.act(game.stay());
        hands.step(policy.tool_settle);
        for (const Tool& tool : std::span<const Tool>(game.tools())) {
            if (!game.suits(tool, seen)) continue;
            if (!game.ready(tool, hands)) continue;
            for (std::size_t aim = 0; aim < game.aims(); ++aim) {
                // As it would be done: used for a moment, then standing while it works.
                const View future = game.read(hands.branch(policy.tool_press + tool.window, std::string(game.use(tool, aim, seen)), policy.tool_press, game.stay()).root()["data"]);
                const Vitals after = game.vitals(future);
                const int left = game.in_the_way(future, seen);
                tried += std::format(" {}{}: {} left, health {}{};", tool.name, std::string(game.aim(aim)), left, after.health, after.lost ? ", lost" : "");
                if (after.lost || after.health < policy.tool_least) continue;
                if (left + policy.clears <= before) {
                    chosen = Try{tool, aim, left};  // the first that clears the way: no need to try the rest
                    break;
                }
            }
            if (chosen) break;  // the cheapest kind that works, in the game's order
        }
        if (chosen) {
            (void)game.ready(chosen->tool, hands);
            hands.act(std::string(game.use(chosen->tool, chosen->aim, seen)));
            hands.step(policy.tool_press);
            hands.act(game.stay());
            hands.step(chosen->tool.window);
            say(std::format("frame {}: {} at {}, in the way {} → {}", vitals.frame, chosen->tool.name, std::string(game.aim(chosen->aim)), before, chosen->left));
            waits = 0;
            continue;
        }
        // 3 nothing helps now: wait (what burns cools, she mends), a few times; then say so.
        say(std::format("frame {}: blocked at {:.1f}, {} in the way, {}; tried{}", vitals.frame, vitals.progress, before, std::string(game.has(seen)), tried.empty() ? " nothing" : tried));
        if (waits++ < policy.waits) {
            // Waiting is a step like any other: not taken where she cannot stay.
            const Vitals later = game.vitals(ahead(policy.wait, game.stay()));
            if (!later.lost && later.health >= vitals.health - policy.wait_cost) {
                hands.step(policy.wait);
                continue;
            }
            say(std::format("frame {}: and waiting here would cost her {} of health: no waiting", vitals.frame, vitals.health - later.health));
        }
        report.why = std::format("stuck at {}, frame {}: {} in the way ahead; {}; every way forward and every tool at every aim leave the way shut", std::string(game.where(seen)), vitals.frame, before,
                                 std::string(game.has(seen)));
        return report;
    }
    report.why = hands.ok() ? "out of turns" : std::format("the game did not do what was asked ({})", hands.problem());
    return report;
}

}  // namespace xengine::playtest
