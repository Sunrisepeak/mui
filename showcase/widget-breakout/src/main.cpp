// `omui-breakout` — the rest of the line is openmui's (--headless --png …).
// The pointer or ← → (A D) move the paddle, Space or a click launches, Tab
// opens the semantic lens.
import std;
import openmui;
import openmui.showcase.widget_breakout;

int main(int argc, char** argv) { return omui::run(omui::breakout::app(), argc, argv); }
