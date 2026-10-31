// openmui.ui.base:dialog — windows over the application: a dialog (modal by
// default: a scrim, focus kept inside, Escape or a press outside dismisses
// it) and a tooltip (a short description of the element before it, shown
// after a delay while it is hovered or focused).
export module openmui.ui.base:dialog;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :dialog_contract;
import :tooltip_contract;

export namespace omui {

class Dialog : public DialogContract<Dialog> {
public:
    static constexpr bool overlay = true;
    [[nodiscard]] Placement placement() const {
        return {Placement::Side::center, Placement::Anchor::parent, 0, props.modal, props.modal, true, false};
    }

    void mounted();
    void prop_changed(DialogProp p);
    void style(const Theme& theme, layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult dismiss(ActContext& ctx);
    // Closes it with a result (`closed` carries it): "dismissed" for Escape and the scrim.
    void close(std::string result);

private:
    mutable parts::Label  title_;
    mutable parts::Motion open_;
    bool                  was_open_ = false;
    std::string           result_ = "dismissed";
};

class Tooltip : public TooltipContract<Tooltip> {
public:
    static constexpr bool overlay = true;
    [[nodiscard]] Placement placement() const {
        return {Placement::Side::above, Placement::Anchor::previous, 6, false, false, false, false};
    }

    Tooltip() = default;
    Tooltip(const Tooltip&) = delete;
    Tooltip& operator=(const Tooltip&) = delete;
    ~Tooltip();

    void mounted();
    void prop_changed(TooltipProp p);
    void style(const Theme& theme, layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw,
                 layout::Available ah);
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult dismiss(ActContext& ctx);

    [[nodiscard]] bool showing() const noexcept { return shown_; }

private:
    void target_entered();
    void target_left();
    void show(bool on);

    mutable parts::Label       label_;
    mutable parts::Motion      fade_;
    NodeId                     target_;
    std::vector<std::uint64_t> listening_;
    std::uint64_t              timer_ = 0;
    bool                       shown_ = false;
};

}  // namespace omui
