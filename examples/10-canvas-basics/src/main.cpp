// Example 10: `space [FOLDER]` — where the folder's space went (the current
// folder by default); the rest of the line is openmui's (--dark, --headless
// --png out.png, --serve …).
import std;
import mcpplibs.cmdline;
import omui;
import space;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("space")
        .description("Where a folder's space went, as a map you can zoom")
        .arg(Arg("folder").help("the folder (default: here)"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    const std::string folder = command->args.positional(0);
    const std::filesystem::path where = folder.empty() ? std::filesystem::current_path() : std::filesystem::path(std::u8string(folder.begin(), folder.end()));
    space::Model model;
    auto tree = space::scan(where);
    if (!tree) {
        std::println(std::cerr, "space: {}", tree.error());
        return 1;
    }
    model.tree = std::move(*tree);
    model.folder = omui::utf8(where);
    return omui::run(space::app(model), command->options);
}
