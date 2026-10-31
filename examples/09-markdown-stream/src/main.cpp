// Example 09: Markdown as it streams in. `md-stream` reads standard input
// (pipe a model's answer into it); `md-stream --demo` plays a built-in answer;
// `--rate N` its pieces per second; `--no-fade`; the rest is openmui's
// (--dark, --headless --png out.png, --serve …).
import std;
import openmui;
import stream;

int main(int argc, char** argv) {
    stream::Model model;
    bool demo = false;
    double rate = 30;
    std::vector<char*> rest{argv[0]};
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--demo") demo = true;
        else if (a == "--no-fade") model.fade = false;
        else if (a == "--rate" && i + 1 < argc) rate = std::max(1.0, std::atof(argv[++i]));
        else rest.push_back(argv[i]);
    }
    omui::AppSpec spec = stream::app(model);
    spec.setup = [&model, demo, rate](omui::Runtime& rt) {
        model.runtime = &rt;
        if (demo) stream::play(model, stream::demo_tokens(), rate);
        else stream::read_stdin(model);
    };
    return omui::run(std::move(spec), static_cast<int>(rest.size()), rest.data());
}
