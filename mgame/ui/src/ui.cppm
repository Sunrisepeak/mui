// mgame.ui — a game inside openmui.
//
//     // in a component:
//     void mounted() { pacer_.start(services(), 120, [this](std::int32_t n) { while (n--) timeline_.step(sampler_.sample()); invalidate(); }); }
//     bool on_event(EventContext&, const InputEvent& e) { return mgame::ui::feed(sampler_, e, services().bounds(node())); }
//
// The pacer wakes at the next step due on the runtime's clock (virtual
// headless, so a headless second is exactly 120 steps), takes every step due
// (a stall past its catch-up is not owed) and asks for frames while it runs.
// Keys reach the sampler by their code (`code(Key)` for bindings), any
// gamepad's buttons and axes by their place (`pad(GamepadButton)`); the
// pointer within the game's area (0 … 1 across and down).
export module mgame.ui;

import std;
import openmui.core;
import openmui.view;
import mgame.input;
import mgame.sim;

export namespace mgame::ui {

[[nodiscard]] constexpr std::uint32_t code(omui::Key k) noexcept { return static_cast<std::uint32_t>(k); }
// A gamepad's button and axis by their place in the standard set (spec §6.1), for bindings.
[[nodiscard]] constexpr std::int32_t pad(omui::GamepadButton b) noexcept { return static_cast<std::int32_t>(b); }
[[nodiscard]] constexpr std::int32_t pad(omui::GamepadAxis a) noexcept { return static_cast<std::int32_t>(a); }

// An openmui event for the game: a key by its code, the pointer within
// `area` (window coordinates); focus lost lets everything go. Whether it took it.
bool feed(Sampler& in, const omui::InputEvent& e, omui::Rect area);

class Pacer {
public:
    Pacer() = default;
    Pacer(const Pacer&) = delete;
    Pacer& operator=(const Pacer&) = delete;
    ~Pacer() { stop(); }

    // `run(n)`: take n steps now. `hz`: steps a second.
    void start(omui::Services& services, double hz, std::function<void(std::int32_t)> run, std::int32_t catch_up = 8);
    void stop();
    [[nodiscard]] bool running() const noexcept { return services_ != nullptr; }
    // Its clock: `scale` for slow motion, `alpha` between two steps for drawing.
    [[nodiscard]] Clock& clock() noexcept { return clock_; }

private:
    void wake();
    omui::Services*                    services_ = nullptr;
    Clock                              clock_;
    omui::FrameDemandLease             frames_;
    std::uint64_t                      timer_ = 0;
    std::function<void(std::int32_t)>  run_;
};

}  // namespace mgame::ui
