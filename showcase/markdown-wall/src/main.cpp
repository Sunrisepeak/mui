// S1: `omui-wall FOLDER` (the current folder by default), or `omui-wall
// --synthetic N` (the benchmark's documents); `--stream N` writes into N
// windows. The rest of the line is openmui's (--dark, --headless --png …).
import std;
import openmui;
import wall;

int main(int argc, char** argv) {
    constexpr std::array<std::string_view, 8> valued{"--png", "--size", "--frames", "--level", "--agent-dir", "--debug", "--log", "--profile"};
    std::vector<char*> rest{argv[0]};
    std::optional<std::string> folder;
    std::size_t synthetic = 0, stream = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        auto number = [&](std::size_t& out) {
            if (i + 1 < argc) out = static_cast<std::size_t>(std::strtoull(argv[++i], nullptr, 10));
        };
        if (a == "--synthetic") number(synthetic);
        else if (a == "--stream") number(stream);
        else if (!a.starts_with("--") && !(i > 1 && std::ranges::find(valued, std::string_view(argv[i - 1])) != valued.end()) && !folder) folder = std::string(a);
        else rest.push_back(argv[i]);
    }
    wall::Model model;
    if (synthetic) {
        model.docs = wall::synthetic(synthetic);
        model.title = std::format("{} documents", synthetic);
    } else {
        const std::filesystem::path where = folder ? std::filesystem::path(std::u8string(folder->begin(), folder->end())) : std::filesystem::current_path();
        auto docs = wall::read_folder(where);
        if (!docs) {
            std::println(std::cerr, "omui-wall: {}", docs.error());
            return 1;
        }
        model.docs = std::move(*docs);
        model.title = omui::utf8(std::filesystem::absolute(where).filename());
    }
    omui::AppSpec spec = wall::app(model);
    if (stream) {
        auto setup = spec.setup;
        spec.setup = [setup, stream, &model](omui::Runtime& rt) {
            setup(rt);
            // Once the view is placed: the windows nearest its centre.
            rt.after(omui::seconds(0.1), [&model, stream] { model.stream(stream); });
        };
    }
    return omui::run(std::move(spec), static_cast<int>(rest.size()), rest.data());
}
