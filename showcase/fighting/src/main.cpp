// `omui-fight [--training] [--quiet] [--fight P1,P2] [--arena NAME] [--computer 0|1] [--computer-level 1-3] [--turns N]`
// — the rest of the line is openmui's (--headless --png …); headless or
// --quiet, nothing is heard. --training is training alone (its fighters --fight's). --fight goes straight into a fight (fighters by
// their files' names: kata,mo …); --computer has the computer play a side;
// --turns has the match played by turns of N frames (agents by
// correspondence: the stage's `turn` action). Keys: player 1 WASD and U I O (punches) J K L (kicks); player 2
// the arrows and Insert Home PageUp (punches) Delete End PageDown (kicks). Gamepads: the
// first two connected, beside the keyboard (controls/pad.controls.omui.json).
import std;
import mcpplibs.cmdline;
import omui;
import xengine.audio;
import xengine.audio.device;
import omui.showcase.fighting;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("omui-fight")
        .description("A 2D fighting game: menus, a fight, training")
        .option(Option("training").help("training alone (its fighters --fight's, its stage --arena's)"))
        .option(Option("quiet").help("nothing is heard (so too headless)"))
        .option(Option("fight").takes_value().value_name("P1,P2").help("straight into a fight: fighters by their files' names (kata,mo …)"))
        .option(Option("arena").takes_value().value_name("NAME").help("the stage"))
        .option(Option("computer").value_type<std::int32_t>().value_name("SIDE").help("the computer plays this side: 0 or 1"))
        .option(Option("computer-level").value_type<std::int32_t>().value_name("1-3").help("how well it plays"))
        .option(Option("turns").value_type<std::int32_t>().help("played by turns of N frames (agents by correspondence)"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    const auto& args = command->args;
    omui::fighting::Start start;
    if (auto fight = args.value("fight")) {
        const auto comma = fight->find(',');
        if (comma == std::string::npos) {
            std::println(std::cerr, "omui-fight: --fight takes two fighters, P1,P2");
            return 2;
        }
        start.player1 = fight->substr(0, comma), start.player2 = fight->substr(comma + 1);
    }
    if (auto arena = args.value("arena")) start.arena = *arena;
    if (auto side = args.value_as<std::int32_t>("computer")) start.cpu = *side;
    if (auto level = args.value_as<std::int32_t>("computer-level")) start.cpu_level = *level;
    if (auto turns = args.value_as<std::int32_t>("turns")) start.turn_frames = *turns;
    const bool training = args.is_flag_set("training");
    const bool quiet = args.is_flag_set("quiet") || command->options.headless;
    // The fight heard through the system's output (none headless, or with --quiet).
    if (!quiet)
        omui::fighting::hear_with([](omui::fighting::Sounds& s) -> std::shared_ptr<void> {
            auto device = xengine::audio::Device::open(s.mixer());
            if (!device) {
                std::println(std::cerr, "omui-fight: no sound ({})", device.error());
                return nullptr;
            }
            s.live(true);
            return std::make_shared<xengine::audio::Device>(std::move(*device));
        });
    omui::AppSpec a = omui::fighting::app(start);
    if (training)  // training alone (with --fight's fighters and --arena's stage, if given)
        a.view = [start] {
            return omui::fighting::stage({.id = "stage",
                                          .arena = start.arena,
                                          .mode = "training",
                                          .player1 = start.player1,
                                          .player2 = start.player2,
                                          .width = omui::Dim(mlibs::layout::pct(100)),
                                          .height = omui::Dim(mlibs::layout::pct(100))});
        };
    return omui::run(std::move(a), command->options);
}
