// Example 10: `space [FOLDER]` — where the folder's space went (the current
// folder by default); the rest of the line is openmui's (--dark, --headless
// --png out.png, --serve …).
import std;
import openmui;
import space;

int main(int argc, char** argv) {
    // openmui's options that take a value: the word after them is theirs.
    constexpr std::array<std::string_view, 8> valued{"--png", "--size", "--frames", "--level", "--agent-dir", "--debug", "--log", "--profile"};
    std::vector<char*> rest{argv[0]};
    std::optional<std::string> folder;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        const bool taken = i > 1 && std::ranges::find(valued, std::string_view(argv[i - 1])) != valued.end();
        if (!a.starts_with("--") && !taken && !folder) folder = std::string(a);
        else rest.push_back(argv[i]);
    }
    const std::filesystem::path where = folder ? std::filesystem::path(std::u8string(folder->begin(), folder->end())) : std::filesystem::current_path();
    space::Model model;
    auto tree = space::scan(where);
    if (!tree) {
        std::println(std::cerr, "space: {}", tree.error());
        return 1;
    }
    model.tree = std::move(*tree);
    model.folder = omui::utf8(where);
    return omui::run(space::app(model), static_cast<int>(rest.size()), rest.data());
}
