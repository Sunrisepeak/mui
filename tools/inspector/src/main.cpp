// omui-inspector — inspect a running openmui application.
//
//   omui-inspector [--app NAME] [--inspect-dir DIR] [omui::run options: --size, --dark, --headless …]
//
// NAME is an application id as `omui agent ls` lists them; without it, the
// first application that listens (other than the Inspector).
import std;
import openmui;
import openmui.inspector;

int main(int argc, char** argv) {
    omui::inspector::Options options;
    std::vector<char*> rest{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--app" && i + 1 < argc) options.app = argv[++i];
        else if (a == "--inspect-dir" && i + 1 < argc) options.directory = argv[++i];
        else rest.push_back(argv[i]);
    }
    return omui::run(omui::inspector::app(std::move(options)), static_cast<int>(rest.size()), rest.data());
}
