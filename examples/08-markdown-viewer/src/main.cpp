// Example 08: a Markdown viewer — `md-view FILE.md` or `md-view FOLDER`
// (index.md or README.md in it); the rest of the line is openmui's (--dark,
// --headless --png out.png, --serve …).
import std;
import mcpplibs.cmdline;
import omui;
import viewer;

int main(int argc, char** argv) {
    // Its document beside openmui's switches, on one command line (mcpplibs.cmdline).
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("md-view")
        .description("A Markdown viewer: a file, or a folder's index.md or README.md")
        .arg(Arg("document").help("FILE.md or FOLDER (default: here)"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    viewer::Model model;
    const std::string document = command->args.positional(0);
    if (!model.open(document.empty() ? std::filesystem::current_path() : std::filesystem::path(std::u8string(document.begin(), document.end()))))
        std::println(std::cerr, "{}", model.status.peek());
    return omui::run(viewer::app(model), command->options);
}
