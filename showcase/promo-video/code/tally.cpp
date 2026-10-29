// The film's tally counter in C++ (after example 03), as the code shot shows
// it and draws it: the same tree as tally.ts, tally.py and tally.lua beside it.
module openmui.showcase.promo_video;

import std;
import openmui;

namespace omui::promo {

View tally(Signal<int> count, Signal<int> goal) {
    auto share = derive([=] {
        return std::clamp(double(count()) / std::max(goal(), 1), 0.0, 1.0);
    });
    return column({.padding = 24, .gap = 12}, {
        text({.id = "count", .text = derive([=] { return std::to_string(count()); }),
              .style = "display"}),
        progress({.id = "progress", .label = "Towards the goal", .value = share}),
        text({.id = "status", .text = derive([=] {
                  return count() >= goal() ? std::format("Goal of {} reached", goal())
                                           : std::format("{} to go", goal() - count());
              })}),
        row({.id = "controls", .gap = 8}, {
            button({.id = "minus", .disabled = derive([=] { return count() == 0; }),
                    .text = "−1",
                    .on_activated = [=] { count.set(std::max(count.peek() - 1, 0)); }}),
            button({.id = "plus", .text = "+1", .variant = ButtonVariant::primary,
                    .on_activated = [=] { count.set(count.peek() + 1); }}),
            button({.id = "reset", .text = "Reset", .variant = ButtonVariant::ghost,
                    .on_activated = [=] { count.set(0); }}),
        }),
    });
}

}  // namespace omui::promo
