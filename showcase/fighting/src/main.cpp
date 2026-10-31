// `omui-fight [--training] [--quiet] [--fight P1 P2] [--arena NAME] [--cpu 0|1] [--level 1-3] [--turns N]`
// — the rest of the line is openmui's (--headless --png …); headless or
// --quiet, nothing is heard. --fight goes straight into a fight (fighters by
// their files' names: kata, mo …); --cpu has the computer play a side;
// --turns has the match played by turns of N frames (agents by
// correspondence: the stage's `turn` action). Keys: player 1 WASD and U I O (punches) J K L (kicks); player 2
// the arrows and Insert Home PageUp (punches) Delete End PageDown (kicks). Gamepads: the
// first two connected, beside the keyboard (controls/pad.controls.omui.json).
import std;
import openmui;
import openmui.audio;
import openmui.audio.device;
import openmui.showcase.fighting;

int main(int argc, char** argv) {
    std::vector<char*> rest{argv[0]};
    bool training = false, quiet = false;
    omui::fighting::Start start;
    auto number = [&](int& i, std::int32_t& out) {
        if (i + 1 < argc) std::from_chars(argv[i + 1], argv[i + 1] + std::strlen(argv[i + 1]), out), ++i;
    };
    for (int i = 1; i < argc; ++i) {
        const std::string_view a = argv[i];
        if (a == "--training") training = true;
        else if (a == "--quiet") quiet = true;
        else if (a == "--fight" && i + 2 < argc) start.player1 = argv[++i], start.player2 = argv[++i];
        else if (a == "--arena" && i + 1 < argc) start.arena = argv[++i];
        else if (a == "--cpu") number(i, start.cpu);
        else if (a == "--level") number(i, start.cpu_level);
        else if (a == "--turns") number(i, start.turn_frames);
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
    omui::AppSpec a = omui::fighting::app(start);
    if (training)
        a.view = [] {
            return omui::fighting::stage(
                {.id = "stage", .mode = "training", .width = omui::Dim(omui::layout::pct(100)), .height = omui::Dim(omui::layout::pct(100))});
        };
    return omui::run(std::move(a), static_cast<int>(rest.size()), rest.data());
}
