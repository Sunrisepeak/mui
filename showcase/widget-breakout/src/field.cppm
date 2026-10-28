// The field: the breakout over a level of real components. Its children
// are the level's rows of bricks; on its first step it reads where layout put
// each brick into the simulation, and from then on every hit changes the
// brick for real (a toggle flips, a slider loses a third, a broken brick goes
// from the screen and the semantic tree), drawing the paddles, balls and
// capsules over them. The pointer or the arrows move the paddle, Space or a
// click launches, Tab opens the semantic lens (each brick's role and id, the
// hidden ones too, and the tree's latest changes); an agent does all of it
// through the actions. A rewind puts every brick back as the simulation had it.
export module openmui.showcase.widget_breakout:field;

import std;
import openmui.core;
import openmui.view;
import mgame.input;
import mgame.ui;
import openmui.showcase.widget_breakout.sim;
import :field_contract;
import :levels;

export namespace omui::breakout {

class Field : public FieldContract<Field> {
public:
    static constexpr bool focusable = true;

    Field();
    Field(const Field&) = delete;
    Field& operator=(const Field&) = delete;
    ~Field();

    void mounted();
    void prop_changed(FieldProp p);
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult launch(ActContext& ctx);
    ActResult move_to(ActContext& ctx, const Value& x);
    ActResult release(ActContext& ctx);
    ActResult pause(ActContext& ctx);
    ActResult resume(ActContext& ctx);
    ActResult step(ActContext& ctx);
    ActResult restart(ActContext& ctx);
    ActResult rewind(ActContext& ctx, const Value& frames);

    // The simulation as it stands (tests and tools read it); none before its first step.
    [[nodiscard]] const State* state() const noexcept { return timeline_ ? &timeline_->state() : nullptr; }

private:
    void run(std::int32_t steps);
    bool begin();                  // the simulation from the bricks' places; false until layout has placed them
    void play(const Input& in);    // one step, and its events on the bricks
    void show_bricks();            // every brick as the simulation has it (after a rewind)
    [[nodiscard]] Input input();
    [[nodiscard]] std::vector<NodeId> bricks() const;  // the field's bricks, in order
    void note(std::string change);  // a change the semantic tree saw, for the lens

    static constexpr std::size_t log_size = 8;

    std::optional<mgame::Timeline<Game>> timeline_;
    mgame::Sampler                       sampler_;
    mgame::ui::Pacer                     pacer_;
    std::optional<double>                agent_x_;     // an agent's paddle (0 … 1), while it holds it
    bool                                 agent_launch_ = false;
    bool                                 discovered_ = false;  // the lens has shown the hidden bricks
    std::deque<std::string>              log_;                 // the lens's latest changes
    mutable std::map<std::string, typeset::Paragraph, std::less<>> words_;  // the lens's labels, shaped once
};

}  // namespace omui::breakout
