// openmui — one import for an application: the runtime (signals, views,
// immediate mode, semantics, the agent endpoint), expressions and view
// documents, the Core components, the hosts, and `omui::run`, which shows an
// application on the desktop or runs it headless for tests and agents.
//
//     import omui;
//
//     int main(int argc, char** argv) {
//         return omui::run({.id = "org.example.hello", .title = "Hello",
//                           .view = [] { return omui::text({.text = "Hello, openmui"}); }},
//                          argc, argv);
//     }
export module omui;

export import omui.core;
export import omui.view;
export import omui.semantic;
export import omui.motion;
export import omui.agent;
export import omui.expr;
export import omui.viewdoc;
export import omui.ui.base;
export import omui.ui.window;
export import omui.host.headless;
export import :host;  // the desktop's window (openmui.host.sdl3), or the page's canvas (openmui.host.web)
export import mlibs.i18n;
export import :app;
export import :agent_guide;  // agent.md for every application (--agent-guide)
