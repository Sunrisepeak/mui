// S2: `omui-styles [--style NAME] [--export-dir DIR]` — the rest of the line
// is openmui's (--dark, --headless --png …).
import std;
import openmui;
import openmui.showcase.style_gallery;

int main(int argc, char** argv) {
    std::vector<char*> rest{argv[0]};
    std::string first = "glass";
    std::filesystem::path out;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--style" && i + 1 < argc) first = argv[++i];
        else if (a == "--export-dir" && i + 1 < argc) out = std::filesystem::path(std::u8string(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1]))), ++i;
        else rest.push_back(argv[i]);
    }
    omui::styles::Studio studio;
    studio.export_dir = out;
    return omui::run(omui::styles::app(studio, first), static_cast<int>(rest.size()), rest.data());
}
