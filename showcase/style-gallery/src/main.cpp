// S2: `omui-styles [--style NAME] [--export-dir DIR]` — the rest of the line
// is openmui's (--dark, --headless --png …).
import std;
import mcpplibs.cmdline;
import omui;
import omui.showcase.style_gallery;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("omui-styles")
        .description("A settings panel and a dashboard in eight style packs")
        .option(Option("style").takes_value().value_name("NAME").help("the pack shown first (default: glass)"))
        .option(Option("export-dir").takes_value().value_name("DIR").help("where the workshop exports a pack"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    omui::styles::Studio studio;
    if (auto dir = command->args.value("export-dir")) studio.export_dir = std::filesystem::path(std::u8string(dir->begin(), dir->end()));
    return omui::run(omui::styles::app(studio, command->args.value("style").value_or("glass")), command->options);
}
