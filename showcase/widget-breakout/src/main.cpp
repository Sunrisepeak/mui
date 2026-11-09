// `omui-breakout [--quiet]` — the rest of the line is openmui's (--headless
// --png …); headless or --quiet, nothing is heard. The pointer or ← → (A D)
// move the paddle, Space or a click launches along the aim, R held rewinds
// (100 sparks), X scans, Z held shows a tall room whole, 1–4 direct the
// copilot (room 5) or, between rooms, 1 and 2 take a contraption, Tab opens
// the inspector, Enter goes on from a room over.
import std;
import mcpplibs.cmdline;
import omui;
import xengine.audio;
import xengine.audio.device;
import omui.showcase.widget_breakout;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("omui-breakout")
        .description("Breakout in a settings panel's widgets")
        .option(Option("quiet").help("nothing is heard (so too headless)"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    const bool quiet = command->args.is_flag_set("quiet") || command->options.headless;
    // The game heard through the system's output (none headless, or with --quiet).
    if (!quiet)
        omui::breakout::hear_with([](omui::breakout::Sounds& s) -> std::shared_ptr<void> {
            auto device = xengine::audio::Device::open(s.mixer());
            if (!device) {
                std::println(std::cerr, "omui-breakout: no sound ({})", device.error());
                return nullptr;
            }
            s.live(true);
            return std::make_shared<xengine::audio::Device>(std::move(*device));
        });
    return omui::run(omui::breakout::app(), command->options);
}
