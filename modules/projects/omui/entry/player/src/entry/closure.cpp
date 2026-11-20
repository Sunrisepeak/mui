// The main of a TypeScript application whose build derives what it carries (B7b): the player over its manifest,
// with the base components its scripts create by name — the registry its build program wrote (omui.build's
// closure(): omui_application_components, a unit of the application's own), not every base component.
import omui.entry.player;

extern "C++" void omui_application_components();

int main(int argc, char** argv) { return omui::player_main(argc, argv, &omui_application_components); }
