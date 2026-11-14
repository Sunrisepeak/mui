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
import std;
import mcpplibs.cmdline;
import omui.showcase.widget_breakout;

int main(int argc, char** argv) {
    auto line = mcpplibs::cmdline::App("omui-breakout-playtest")
                    .description("The agent as playtester: every room played headless by players of different skill")
                    .option(mcpplibs::cmdline::Option("rooms").takes_value().value_name("1,2,3").help("the rooms (default: all five)"))
                    .option(mcpplibs::cmdline::Option("players").value_type<int>().help("players a room (default 4)"))
                    .option(mcpplibs::cmdline::Option("minutes").value_type<double>().help("the longest a play may take (default 2)"))
                    .option(mcpplibs::cmdline::Option("out").takes_value().value_name("DIR").help("where the report goes (default target/breakout-playtest)"));
    auto parsed = line.parse(argc, argv);
    if (!parsed) {
        if (!parsed.error().is_error()) return 0;
        std::println(std::cerr, "omui-breakout-playtest: {}", parsed.error().message);
        return 2;
    }
    omui::breakout::PlaytestOptions options{.out = "target/breakout-playtest"};
    if (auto rooms = parsed->value("rooms")) {
        options.rooms.clear();
        for (const auto part : std::views::split(*rooms, ',')) {
            int room = 0;
            const std::string_view text(part.begin(), part.end());
            if (std::from_chars(text.data(), text.data() + text.size(), room).ec != std::errc{}) {
                std::println(std::cerr, "omui-breakout-playtest: --rooms takes room numbers, not `{}`", text);
                return 2;
            }
            options.rooms.push_back(room);
        }
    }
    if (auto players = parsed->value_as<int>("players")) options.players = std::max(1, *players);
    if (auto minutes = parsed->value_as<double>("minutes")) options.minutes = std::max(0.1, *minutes);
    if (auto out = parsed->value("out")) options.out = *out;
    const omui::Value report = omui::breakout::playtest(options);
    bool replay = true;
    for (const omui::Value& r : *report.get("rooms")->list())
        std::println("room {}: cleared {}/{}, out of lives {}, unfinished {}; median {:.0f} s, longest quiet {:.0f} s", r.get("room")->as_int(),
                     r.get("cleared")->as_int(), r.get("players")->as_int(), r.get("out_of_lives")->as_int(), r.get("unfinished")->as_int(),
                     r.get("median_s")->as_double(), r.get("longest_quiet_s")->as_double());
    for (const omui::Value& p : *report.get("problems")->list()) {
        replay = replay && p.get("replays")->as_bool();
        std::println("  problem: room {} player {}: {} — {} ({})", p.get("room")->as_int(), p.get("player")->as_int(), p.get("problem")->as_string(),
                     p.get("record")->as_string(), p.get("replays")->as_bool() ? "replays to the same bytes" : "DOES NOT REPLAY");
    }
    std::println("playtest: {}", omui::generic_utf8(options.out / "playtest.omui.json"));
    return replay ? 0 : 1;
}
