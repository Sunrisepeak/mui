// The application: a greeting, a name field, and a button that counts.
export module hello;

import std;
import openmui;
export import :hello_source;  // this file's text (build.mcpp)

export namespace hello {

struct State {
    std::string name;
    int         clicks = 0;
};

[[nodiscard]] inline omui::AppSpec app(State& s) {
    return {
        .id = "org.openmui.examples.hello-im",
        .title = "Hello, openmui",
        .size = {420, 280},
        .view = [&s] {
            return omui::immediate([&s](omui::Ui& ui) {
                ui.column({.padding = 24, .gap = 12}, [&] {
                    ui.text("Hello, openmui", {.style = "title"});
                    ui.text_box("Name", s.name, {.id = "name", .placeholder = "Your name"});
                    ui.text(s.name.empty() ? std::string("Type your name above.") : std::format("Nice to meet you, {}!", s.name));
                    if (ui.button(s.clicks == 0 ? std::string("Click me") : s.clicks == 1 ? std::string("Clicked once") : std::format("Clicked {} times", s.clicks), {.id = "click"}))
                        ++s.clicks;
                });
            });
        },
    };
}

}  // namespace hello
