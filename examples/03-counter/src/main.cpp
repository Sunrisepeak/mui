// Example 03: a tally counter — `tally` (the C++ view) or
// `tally --doc ui/tally.view.omui.json` (the document, reloaded as you save
// it); the rest of the line is openmui's (--dark, --headless --png out.png,
// --serve …).
import std;
import openmui;
import tally;

int main(int argc, char** argv) {
    std::vector<char*> rest{argv[0]};
    std::optional<std::filesystem::path> doc;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--doc" && i + 1 < argc) {
            const std::string_view f = argv[++i];
            doc = std::filesystem::path(std::u8string(f.begin(), f.end()));
        } else {
            rest.push_back(argv[i]);
        }
    }
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
    return omui::run(std::move(app), static_cast<int>(rest.size()), rest.data());
}
