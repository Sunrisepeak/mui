// The least application in a terminal with its agent endpoint (terminal design §5, the second tier): terminal's
// count and buttons, and the agent surface installed — `ui.*` and `terminal.frame` at the endpoint named after it.
import std;
import omui.agent;
import omui.host.terminal;
import omui.ui.base;

using namespace omui;

int main() {
    omui::agent::install_endpoint();  // the endpoint (a terminal captures no pixels): this program is its root
    omui::terminal::install_agent();  // and the terminal's face at it: its picture as a subject
    // By name, only what it builds — the catalog an agent reads (`describe`) lists those, as an application's closure
    // registers what its scripts name (B7b); the other primitives and base components are not carried.
    register_base_named<"button", "column", "row", "text">();
    const Signal<int> count = signal(0);
    Terminal terminal({.app = "omui-size-terminal-agent"});
    terminal.mount("main", column({.padding = 16, .gap = 16}, {
        text({.id = "count", .text = derive([=] { return "Count " + std::to_string(count()); })}),
        row({.id = "controls", .gap = 8}, {
            button({.id = "minus", .text = "-1", .on_activated = [=] { count.set(std::max(count.peek() - 1, 0)); }}),
            button({.id = "plus", .text = "+1", .variant = ButtonVariant::primary, .on_activated = [=] { count.set(count.peek() + 1); }}),
        }),
    }));
    return terminal.run();
}
