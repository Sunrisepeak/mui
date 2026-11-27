// The counter of the terminal comparison (F4; ../../README.md): a count and two buttons; the mouse clicks them, Tab
// and the arrows move between them, Enter presses, Ctrl+C ends — FTXUI's own button example reduced to this. FTXUI is
// written in headers, so its side is a native unit (lint headers).
#include <algorithm>
#include <string>

#include "ftxui/component/component.hpp"
#include "ftxui/component/screen_interactive.hpp"

int main() {
    using namespace ftxui;
    int count = 0;
    auto buttons = Container::Horizontal({
        Button("-1", [&] { count = std::max(count - 1, 0); }, ButtonOption::Ascii()),
        Button("+1", [&] { ++count; }, ButtonOption::Simple()),
    });
    auto shown = Renderer(buttons, [&] { return vbox({text("Count " + std::to_string(count)), separatorEmpty(), buttons->Render()}); });
    auto screen = ScreenInteractive::Fullscreen();
    screen.Loop(shown);
}
