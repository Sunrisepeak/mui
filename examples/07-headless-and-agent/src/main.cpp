// Example 07: a notes app agents can drive (`mcpp run -- --serve`).
import std;
import openmui;
import notes;

int main(int argc, char** argv) {
    notes::Model model;
    return omui::run(notes::app(model), argc, argv);
}
