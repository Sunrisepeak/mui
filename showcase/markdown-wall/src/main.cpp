// S1: `omui-wall FOLDER` (the current folder by default), or `omui-wall
// --synthetic N` (the benchmark's documents); `--stream N` writes into N
// windows. The rest of the line is openmui's (--dark, --headless --png …).
import std;
import mcpplibs.cmdline;
import omui;
import omui.showcase.markdown_wall;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("omui-wall")
        .description("A folder's Markdown as a wall of documents you can zoom")
        .arg(Arg("folder").help("the folder (default: here)"))
        .option(Option("synthetic").value_type<std::size_t>().help("the benchmark's N documents instead of a folder"))
        .option(Option("stream").value_type<std::size_t>().help("write into N windows as a stream"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    const std::size_t synthetic = command->args.value_as<std::size_t>("synthetic").value_or(0);
    const std::size_t stream = command->args.value_as<std::size_t>("stream").value_or(0);
    const std::string folder = command->args.positional(0);
    omui::wall::Model model;
    if (synthetic) {
        model.docs = omui::wall::synthetic(synthetic);
        model.title = std::format("{} documents", synthetic);
    } else {
        const std::filesystem::path where = folder.empty() ? std::filesystem::current_path() : std::filesystem::path(std::u8string(folder.begin(), folder.end()));
        auto docs = omui::wall::read_folder(where);
        if (!docs) {
            std::println(std::cerr, "omui-wall: {}", docs.error());
            return 1;
        }
        model.docs = std::move(*docs);
        model.folder = where;  // its arrangement and camera kept in it (.openmui/), its tasks' ticks written back
        model.title = omui::utf8(std::filesystem::absolute(where).filename());
    }
    omui::AppSpec spec = omui::wall::app(model);
    if (stream) {
        auto setup = spec.setup;
        spec.setup = [setup, stream, &model](omui::Runtime& rt) {
            setup(rt);
            // Once the view is placed: the windows nearest its centre.
            rt.after(omui::seconds(0.1), [&model, stream] { model.stream(stream); });
        };
    }
    return omui::run(std::move(spec), command->options);
}
