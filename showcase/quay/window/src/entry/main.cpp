// quay — a watchkeeper on a harbour at night, in a window.
//
//   quay [--level NAME] [--record FILE] [omui's options: --headless, --frames N, --png FILE …]
//       W A S D walk, the mouse looks (click the harbour; Escape lets the pointer go), the left button fires, Space
//       jumps, E works the beacon (or a fuse box), Shift runs; or a gamepad. --level second-night: the lookouts and
//       the fuse boxes. --record writes each step's input when the window closes:
//       `quay-play replay FILE` plays the same game with no window, bit for bit.
import std;
import mcpplibs.cmdline;
import mcpplibs.os;
import omui;
import omui.showcase.quay.window;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Option;
    auto line = App("quay")
        .description("Quay: a watchkeeper on a harbour at night has to light the beacon before the tide")
        .option(Option("level").takes_value().value_name("NAME").help("harbour (the first night) or second-night (lookouts sweep the quay; two fuse boxes to switch on first)"))
        .option(Option("record").takes_value().value_name("FILE").help("each step's input, written when the window closes (quay-play replay FILE plays it again)"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    quay::window::Options options;
    if (const auto file = command->args.value("record")) options.record = mcpplibs::os::fs::from_utf8(*file);
    if (const auto name = command->args.value("level")) {
        if (!quay::level::named(*name)) {
            std::println(std::cerr, "quay: no level is named `{}` (harbour, second-night)", *name);
            return 2;
        }
        options.level = std::string(*name);
    }
    return omui::run(quay::window::app(options), command->options);
}
