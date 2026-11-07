// The reading list: the player runs logic/main.ts; this program registers
// the application's own component before it does.
import shelf;
import omui.entry.player;

int main(int argc, char** argv) {
    shelf::register_components();
    return omui::player_main(argc, argv);
}
