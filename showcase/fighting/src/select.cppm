// The select screen: the title's modes (versus, training), then each
// player's cursor over the roster with a big preview of whom it rests on —
// their idle playing, lit — their name in calligraphy, and behind it all
// the home stage of whom player 1 is on, panning slowly. Both ready, it
// says whom (the `chosen` event). The stage's keys and gamepads move it, as
// do an agent's actions. The main theme plays under it, rising as the
// players choose, with a cue for each move, choice and step back.
export module openmui.showcase.fighting:select;

import std;
import openmui.core;
import openmui.view;
import gongfu.present2d;
import :select_contract;
import :roster;
import :sound;

export namespace omui::fighting {

class Select : public SelectContract<Select> {
public:
    static constexpr bool focusable = true;

    Select() = default;
    Select(const Select&) = delete;
    Select& operator=(const Select&) = delete;
    ~Select();

    void mounted();
    void prop_changed(SelectProp p);
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult move(ActContext& ctx, const MoveArgs& args);
    ActResult confirm(ActContext& ctx, const Value& player);
    ActResult back(ActContext& ctx, const Value& player);

    // The fighters a player can choose (by their files' name), in the screen's order.
    [[nodiscard]] static std::span<const std::string> choices();
    [[nodiscard]] std::string_view phase() const noexcept { return phase_; }
    [[nodiscard]] std::int32_t cursor(std::int32_t player) const noexcept { return cursor_[static_cast<std::size_t>(player & 1)]; }
    [[nodiscard]] bool ready(std::int32_t player) const noexcept { return ready_[static_cast<std::size_t>(player & 1)]; }
    [[nodiscard]] Sounds& sounds() noexcept { return sounds_; }

private:
    void schedule();
    void changed();
    void music();
    void cue(std::string_view name, float pan = 0);
    void begin(std::string_view phase);
    void step(std::int32_t player, std::int32_t by);
    void press(std::int32_t player);
    void unpress(std::int32_t player);
    [[nodiscard]] std::optional<std::int32_t> seat_of(std::uint32_t device, bool take);

    std::string                       phase_ = "title";
    std::string                       mode_ = "versus";
    std::int32_t                      item_ = 0;              // the title's mode under the cursor
    std::array<std::int32_t, 2>       cursor_{0, 1};
    std::array<bool, 2>               ready_{};
    bool                              cpu_ = false;           // versus: the computer plays player 2 until they join
    struct CpuWords;
    mutable std::shared_ptr<CpuWords> cpu_words_;             // its plate's words, shaped once
    void join();
    std::int32_t                      frame_ = 0;             // frames shown (the previews' and the backdrop's clock)
    std::int32_t                      both_ready_ = -1;       // the frame both became ready (the fight is chosen a moment later)
    std::shared_ptr<const Registered> art_;
    std::vector<gongfu::present2d::Look> looks_;             // the choices' looks, two palettes each, their pictures bound
    std::map<std::string, gongfu::present2d::Stage, std::less<>> stages_;  // the homes' art, bound
    std::array<std::optional<std::uint32_t>, 2> pads_{};     // the gamepad in each seat
    std::array<float, 2>              stick_{};               // each pad's left stick across (a move on a push past half)
    Sounds                            sounds_;                // the main theme and the cues (sounds/menu.sound.omui.toml)
    std::shared_ptr<void>             heard_;                 // the program's device on sounds_ (let go first)
    std::int32_t                      heard_from_ = 0;        // the frame the theme was last started on (the cues' frame 0)
    mutable gongfu::present2d::Painter painter_;
    mutable render::DisplayList       list_;
    FrameDemandLease                  pace_;
    std::uint64_t                     timer_ = 0;
};

}  // namespace omui::fighting
