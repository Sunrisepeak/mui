// openmui.showcase.widget_breakout:copilot — level 5's teammate: an agent
// that plays the top paddle the way any agent would, from outside the field.
// Thirty times a second it reads the field's semantic node (the balls: where,
// how fast), works out where the one rising nearest will reach the top
// paddle's line (off the side walls), and calls the field's `guard_to`
// action with what it saw — as the actor `agent:copilot`, through the
// runtime's actions, journaled like any other.
export module openmui.showcase.widget_breakout:copilot;

import std;
import openmui;

export namespace omui::breakout {

class Copilot : public std::enable_shared_from_this<Copilot> {
public:
    // Plays the field's top paddle until stopped (or the field is gone).
    void start(Runtime& runtime, NodeId field);
    void stop();
    [[nodiscard]] bool playing() const noexcept { return runtime_ != nullptr; }

    // Where a ball at (x, y), moving (vx, vy) pixels a step, meets the line
    // y = line, off the walls at 0 and width (its radius in); none when it
    // is not moving toward it.
    [[nodiscard]] static std::optional<double> meets(double x, double y, double vx, double vy, double line, double width, double radius);

private:
    void tick();
    Runtime*      runtime_ = nullptr;
    NodeId        field_{};
    std::uint64_t timer_ = 0;
    double        last_ = -1;  // where it last sent the paddle
};

}  // namespace omui::breakout
