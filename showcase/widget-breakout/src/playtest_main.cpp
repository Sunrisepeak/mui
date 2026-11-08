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
import omui.showcase.widget_breakout;

int main(int argc, char** argv) {
    omui::breakout::PlaytestOptions options{.out = "target/breakout-playtest"};
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto next = [&] { return i + 1 < argc ? std::string(argv[++i]) : std::string{}; };
        if (a == "--rooms") {
            options.rooms.clear();
            for (const auto part : std::views::split(next(), ',')) options.rooms.push_back(std::atoi(std::string(std::string_view(part)).c_str()));
        } else if (a == "--players") options.players = std::max(1, std::atoi(next().c_str()));
        else if (a == "--minutes") options.minutes = std::max(0.1, std::atof(next().c_str()));
        else if (a == "--out") options.out = next();
        else {
            std::println(std::cerr, "usage: omui-breakout-playtest [--rooms 1,2,3,4,5] [--players 4] [--minutes 2] [--out DIR]");
            return 2;
        }
    }
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
