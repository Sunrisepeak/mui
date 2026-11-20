// swarm — a small top-down shooter, in a window.
//
//   swarm [--seed N] [--record FILE] [omui's options: --headless, --frames N, --png FILE …]
//       W A S D (or the arrows) move, the pointer aims, the left button or Space shoots; or a gamepad (the left stick
//       moves, A shoots). --record writes each step's input when the window closes: the headless game played from it
//       is the same bits.
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import omui;
import omui.showcase.swarm.window;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Option;
    auto line = App("swarm")
        .description("Swarm: waves of enemies close in from one side of the arena; move, aim and shoot")
        .option(Option("seed").takes_value().value_name("N").help("which game: the sides and places of the waves after the first (default 1)"))
        .option(Option("record").takes_value().value_name("FILE").help("each step's input, written when the window closes"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    swarm::window::Options options;
    if (const auto file = command->args.value("record")) options.record = mcpplibs::os::fs::from_utf8(*file);
    if (const auto seed = command->args.value("seed")) {
        const std::string_view text = *seed;
        if (const auto read = std::from_chars(text.data(), text.data() + text.size(), options.seed); read.ec != std::errc{} || read.ptr != text.data() + text.size()) {
            std::println(std::cerr, "swarm: --seed takes a whole number, not `{}`", text);
            return 2;
        }
    }
    return omui::run(swarm::window::app(options), command->options);
}
