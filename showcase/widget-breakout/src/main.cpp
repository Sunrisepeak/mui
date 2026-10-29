// `omui-breakout [--quiet]` — the rest of the line is openmui's (--headless
// --png …); headless or --quiet, nothing is heard. The pointer or ← → (A D)
// move the paddle, Space or a click launches along the aim, R held rewinds
// (100 sparks), X scans, Z held shows a tall room whole, 1–4 direct the
// copilot (room 5) or, between rooms, 1 and 2 take a contraption, Tab opens
// the inspector, Enter goes on from a room over.
import std;
import openmui;
import openmui.audio;
import openmui.audio.device;
import openmui.showcase.widget_breakout;

int main(int argc, char** argv) {
    std::vector<char*> rest{argv[0]};
    bool quiet = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--quiet") quiet = true;
        else rest.push_back(argv[i]), quiet = quiet || a == "--headless";
    }
    // The game heard through the system's output (none headless, or with --quiet).
    if (!quiet)
        omui::breakout::hear_with([](omui::breakout::Sounds& s) -> std::shared_ptr<void> {
            auto device = omui::audio::Device::open(s.mixer());
            if (!device) {
                std::println(std::cerr, "omui-breakout: no sound ({})", device.error());
                return nullptr;
            }
            s.live(true);
            return std::make_shared<omui::audio::Device>(std::move(*device));
        });
    return omui::run(omui::breakout::app(), static_cast<int>(rest.size()), rest.data());
}
