// Example 08: a Markdown viewer — `md-view FILE.md` or `md-view FOLDER`
// (index.md or README.md in it); the rest of the line is openmui's (--dark,
// --headless --png out.png, --serve …).
import std;
import openmui;
import viewer;

int main(int argc, char** argv) {
    viewer::Model model;
    std::vector<char*> rest{argv[0]};
    std::optional<std::string> document;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (!a.starts_with("--") && !document && (i == 1 || std::string_view(argv[i - 1]) != "--png")) document = std::string(a);
        else rest.push_back(argv[i]);
    }
    if (!model.open(document ? std::filesystem::path(std::u8string(document->begin(), document->end())) : std::filesystem::current_path()))
        std::println(std::cerr, "{}", model.status.peek());
    return omui::run(viewer::app(model), static_cast<int>(rest.size()), rest.data());
}
