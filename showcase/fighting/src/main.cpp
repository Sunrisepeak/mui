// `omui-fight [--training]` — the rest of the line is openmui's (--headless
// --png …). Keys: player 1 WASD and U I O (punches) J K L (kicks); player 2
// the arrows and Insert Home PageUp (punches) Delete End PageDown (kicks).
import std;
import openmui;
import openmui.showcase.fighting;

int main(int argc, char** argv) {
    std::vector<char*> rest{argv[0]};
    bool training = false;
    for (int i = 1; i < argc; ++i) {
        if (std::string_view(argv[i]) == "--training") training = true;
        else rest.push_back(argv[i]);
    }
    omui::AppSpec a = omui::fighting::app();
    if (training)
        a.view = [] {
            return omui::fighting::stage(
                {.id = "stage", .mode = "training", .width = omui::Dim(omui::layout::pct(100)), .height = omui::Dim(omui::layout::pct(100))});
        };
    return omui::run(std::move(a), static_cast<int>(rest.size()), rest.data());
}
