// The contrast checker: the player runs logic/main.ts; this program adds the
// application's own native module before it does.
import colors;
import openmui.entry.player;

int main(int argc, char** argv) {
    if (auto r = colors::contrast::define(); !r) return 2;
    return omui::player_main(argc, argv);
}
