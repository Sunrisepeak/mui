// `omui-fight [--training] [--quiet]` — the rest of the line is openmui's
// (--headless --png …); headless or --quiet, nothing is heard. Keys: player 1 WASD and U I O (punches) J K L (kicks); player 2
// the arrows and Insert Home PageUp (punches) Delete End PageDown (kicks).
import std;
import openmui;
import openmui.audio;
import openmui.audio.device;
import openmui.showcase.fighting;

int main(int argc, char** argv) {
    std::vector<char*> rest{argv[0]};
    bool training = false, quiet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--training") training = true;
        else if (a == "--quiet") quiet = true;
        else rest.push_back(argv[i]), quiet = quiet || a == "--headless";
    }
    // The fight heard through the system's output (none headless, or with --quiet).
    if (!quiet)
        omui::fighting::hear_with([](omui::fighting::Sounds& s) -> std::shared_ptr<void> {
            auto device = omui::audio::Device::open(s.mixer());
            if (!device) {
                std::println(std::cerr, "omui-fight: no sound ({})", device.error());
                return nullptr;
            }
            s.live(true);
            return std::make_shared<omui::audio::Device>(std::move(*device));
        });
    omui::AppSpec a = omui::fighting::app();
    if (training)
        a.view = [] {
            return omui::fighting::stage(
                {.id = "stage", .mode = "training", .width = omui::Dim(omui::layout::pct(100)), .height = omui::Dim(omui::layout::pct(100))});
        };
    return omui::run(std::move(a), static_cast<int>(rest.size()), rest.data());
}
