// openmui.showcase.widget_breakout:playtest — the agent as playtester
// (Part 2 §4.5, G): the game played headless at a person's tier (the field's
// `human` tier, through the same protocol an external agent speaks), once
// per room and player — players of different skill from a seed — and a
// report a designer reads: how often each room is cleared, how long, the
// lives it costs, its longest stretch with nothing scored, where the paddle
// misses; each problem (out of lives, out of time, too long quiet) with its
// record, replayed to the same bytes. omui-breakout-playtest is its command.
export module omui.showcase.widget_breakout:playtest;

import std;
import omui;

export namespace omui::breakout {

struct PlaytestOptions {
    std::vector<std::int32_t> rooms{1, 2, 3, 4, 5};
    std::int32_t              players = 4;   // of the four skills, in turn
    double                    minutes = 2;   // of play, at most, a game
    std::filesystem::path     out;           // the report and the problems' records written here (empty: none)
};

// The report (kind breakout.playtest): a row a room, the problems with their records.
[[nodiscard]] Value playtest(const PlaytestOptions& options);

}  // namespace omui::breakout
