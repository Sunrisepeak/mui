// kiln — Mui the pixel cat and her wand, in a cavern where everything is matter, in a window.
//
//   kiln [--stage NAME] [--record FILE] [omui's options: --headless, --frames N, --png FILE …]
//       A D (or the arrows) walk, W or Space jumps and swims up, the left button casts toward the pointer, the right
//       button or Q chooses the next spell; or a gamepad. --record writes each step's input when the window closes:
//       the headless game played from it is the same bits.
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import omui;
import omui.showcase.kiln.window;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Option;
    auto line = App("kiln")
        .description("Kiln: Mui the pixel cat crosses a cavern of living matter with fire, water and bombs")
        .option(Option("stage").takes_value().value_name("NAME").help("first (default) or flawed"))
        .option(Option("record").takes_value().value_name("FILE").help("each step's input, written when the window closes"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    kiln::window::Options options;
    if (const auto file = command->args.value("record")) options.record = mcpplibs::os::fs::from_utf8(*file);
    if (const auto name = command->args.value("stage")) {
        if (!kiln::level::named(*name)) {
            std::println(std::cerr, "kiln: no stage is named `{}` (first, flawed)", *name);
            return 2;
        }
        options.level = std::string(*name);
    }
    return omui::run(kiln::window::app(options), command->options);
}
