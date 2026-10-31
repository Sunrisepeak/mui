# mgame.ui

A game inside openmui. It is the only mgame package that knows openmui.

```cpp
import mgame.ui;

// in a game's component:
void mounted() {
    pacer_.start(services(), 120, [this](std::int32_t n) {
        while (n--) timeline_.step(sampler_.sample());
        services().invalidate_paint(node());
    });
}
bool on_event(omui::EventContext&, const omui::InputEvent& e) { return mgame::ui::feed(sampler_, e, services().bounds(node())); }
```

- **The pacer:** it wakes at the next step due on the runtime's clock, takes every step due, and asks for frames while it runs. The clock is virtual under the headless host, so a headless second is exactly 120 steps. A stall past its catch-up is not owed afterwards. Its clock's `scale` is slow motion; `alpha` is the fraction between two steps, for drawing.
- **Events:** a key reaches `mgame.input` by its code (`code(Key)` for bindings). The pointer arrives within the game's area, 0 to 1 across and down, and a click as a mouse button. Losing focus lets go of everything. What the game does not take (text, an input method's composition) stays the interface's (D-76).
