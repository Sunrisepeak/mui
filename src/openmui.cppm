// openmui — one import for an application: the runtime (signals, views,
// immediate mode, semantics, the agent endpoint), expressions and view
// documents, the Core components, the hosts, and `omui::run`, which shows an
// application on the desktop or runs it headless for tests and agents.
//
//     import openmui;
//
//     int main(int argc, char** argv) {
//         return omui::run({.id = "org.example.hello", .title = "Hello",
//                           .view = [] { return omui::text({.text = "Hello, openmui"}); }},
//                          argc, argv);
//     }
export module openmui;

export import openmui.core;
export import openmui.view;
export import openmui.semantic;
export import openmui.agent;
export import openmui.expr;
export import openmui.viewdoc;
export import openmui.ui.base;
export import openmui.host.headless;
export import openmui.host.sdl3;
export import :app;
