// The least application in a terminal (terminal design §5, the first tier): a count and the two buttons that change
// it, on this program's terminal until Ctrl+C — the window's components, unchanged, in cells; no agent endpoint.
import std;
import omui.host.terminal;
import omui.ui.base;

using namespace omui;

int main() {
    const Signal<int> count = signal(0);
    Terminal terminal({.app = "omui-size-terminal", .agent = false});
    terminal.mount("main", column({.padding = 16, .gap = 16}, {
        text({.id = "count", .text = derive([=] { return "Count " + std::to_string(count()); })}),
        row({.id = "controls", .gap = 8}, {
            button({.id = "minus", .text = "-1", .on_activated = [=] { count.set(std::max(count.peek() - 1, 0)); }}),
            button({.id = "plus", .text = "+1", .variant = ButtonVariant::primary, .on_activated = [=] { count.set(count.peek() + 1); }}),
        }),
    }));
    return terminal.run();
}
