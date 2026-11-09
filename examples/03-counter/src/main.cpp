// Example 03: a tally counter — `tally` (the C++ view) or
// `tally --doc ui/tally.view.omui.json` (the document, reloaded as you save
// it); the rest of the line is openmui's (--dark, --headless --png out.png,
// --serve …).
import std;
import mcpplibs.cmdline;
import omui;
import tally;

int main(int argc, char** argv) {
    using mcpplibs::cmdline::App, mcpplibs::cmdline::Arg, mcpplibs::cmdline::Option;
    auto line = App("tally")
        .description("A tally counter, its view in C++ or a view document")
        .option(Option("doc").takes_value().value_name("FILE").help("the view from this document, reloaded as you save it"));
    auto command = omui::command_line(std::move(line), argc, argv);
    if (!command) return command.error();
    std::optional<std::filesystem::path> doc;
    if (auto file = command->args.value("doc")) doc = std::filesystem::path(std::u8string(file->begin(), file->end()));
    tally::State state;
    std::optional<tally::Document> document;
    omui::AppSpec app{.id = "org.openmui.tally", .title = "Tally", .size = {360, 300}};
    if (doc) {
        app.setup = [&](omui::Runtime& rt) {
            auto d = tally::Document::open(rt, *doc);
            if (!d) {
                std::print(std::cerr, "{}", d.error());
                std::exit(1);
            }
            document = std::move(*d);
            document->start(rt);
        };
    } else {
        app.view = [&] { return tally::view(state); };
    }
    return omui::run(std::move(app), command->options);
}
