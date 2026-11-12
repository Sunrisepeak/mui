# xengine.ui

A game shown in an interface: the bridge between the game layer and omui, and the only xengine package that knows omui (D-162; lint `project-dag`).

The game is the engine's and runs on its own ([xengine.runtime](../runtime)); an interface is one way to show it. The component that shows it holds the same `Runtime` a headless program holds, steps it on the interface's clock and gives each step the person's input — the owner's stepping, the owner's seat:

```cpp
import xengine.ui;
import xengine.runtime;

// in a game's component:
void mounted() {
    game_.emplace(first_state(), {.stepping = xengine::runtime::Stepping::owner, .seat = xengine::runtime::Seat::owner});
    pacer_.start(services(), 120, [this](std::int32_t n) {
        while (n--) game_->step(input());          // the person's keys and pointer, sampled; an agent player's through game_->lane()
        services().invalidate_paint(node());
    });
}
bool on_event(omui::EventContext&, const omui::InputEvent& e) { return xengine::ui::feed(sampler_, e, services().bounds(node())); }
```

What the window played is the game with no window: each step's input, on a `Runtime` begun the same with no omui in the program, gives the same bits (the breakout's `agent_player_test` plays twenty seconds in the window and again alone).

- **The pacer:** it wakes at the next step due on the runtime's clock, takes every step due, and asks for frames while it runs. The clock is virtual under the headless host, so a headless second is exactly 120 steps. A stall past its catch-up is not owed afterwards. Its clock's `scale` is slow motion; `alpha` is the fraction between two steps, for drawing.
- **Events:** a key reaches `xengine.input` by its code (`code(Key)` for bindings). The pointer arrives within the game's area, 0 to 1 across and down, and a click as a mouse button. Losing focus lets go of everything. What the game does not take (text, an input method's composition) stays the interface's (D-76).
