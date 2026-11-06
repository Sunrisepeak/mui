// openmui.ui.base:surfaces — what holds content: a card (a surface with a
// title, a subtitle and its content; activated whole when interactive), an
// accordion (a header always shown, its content while expanded), a toast (a
// short message over a corner of the window, gone after a while) and a
// popover (content beside what opened it, not modal). They draw what they
// are; how they open, close and change is their slots' (universal motion).
export module openmui.ui.base:surfaces;

import std;
import openmui.core;
import openmui.view;
import :parts;
import :card_contract;
import :accordion_contract;
import :toast_contract;
import :popover_contract;

export namespace omui {

class Card : public CardContract<Card> {
public:
    void mounted();
    void prop_changed(CardProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;
    std::size_t effect_shapes(Size size, Rect content, std::span<xrender::EffectShape> out) const;

    ActResult activate(ActContext& ctx);

private:
    [[nodiscard]] float header(const Theme& theme) const;  // the title's and the subtitle's height

    mutable parts::Label title_, subtitle_;
};

class Accordion : public AccordionContract<Accordion> {
public:
    void mounted();
    void prop_changed(AccordionProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void paint_over(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult expand(ActContext& ctx);
    ActResult collapse(ActContext& ctx);
    ActResult activate(ActContext& ctx);

    [[nodiscard]] float header_height(const Theme& theme) const;

private:
    ActResult open(bool expanded);
    void show_content();

    mutable parts::Label title_;
};

class Toast : public ToastContract<Toast> {
public:
    static constexpr bool overlay = true;
    [[nodiscard]] Placement placement() const;

    Toast() = default;
    Toast(const Toast&) = delete;
    Toast& operator=(const Toast&) = delete;
    ~Toast();

    void mounted();
    void prop_changed(ToastProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    Size measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available ah);
    void paint(PaintContext& ctx) const;
    bool on_event(EventContext& ctx, const InputEvent& e);
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult dismiss(ActContext& ctx);
    ActResult activate(ActContext& ctx);

private:
    void close();
    void arm();  // its timer, while open

    [[nodiscard]] Rect close_box(Size size) const;

    mutable parts::Label message_, action_;
    mutable Rect         action_at_;  // its action's label as last painted
    std::uint64_t        timer_ = 0;
    bool                 was_open_ = false;
};

class Popover : public PopoverContract<Popover> {
public:
    static constexpr bool overlay = true;
    [[nodiscard]] Placement placement() const;

    void mounted();
    void prop_changed(PopoverProp p);
    void style(const Theme& theme, mlibs::layout::Style& s) const;
    void paint(PaintContext& ctx) const;
    void semantics(SemanticsContext& ctx, spec::SemanticNode& node) const;

    ActResult dismiss(ActContext& ctx);

private:
    bool was_open_ = false;
};

}  // namespace omui
