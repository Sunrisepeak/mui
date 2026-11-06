module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

using parts::mix;
using parts::shadow;

namespace {

constexpr float card_pad = 16, card_gap = 12;
constexpr float accordion_pad = 16, chevron = 10;
constexpr float toast_bar = 4, toast_close = 16, toast_gap = 12;
constexpr float arrow = 7;

// A chevron pointing right, or down (turned a quarter), centred at c.
void draw_chevron(PaintContext& ctx, Point c, float size, bool down, Color color) {
    auto at = [&](float x, float y) { return down ? Point{c.x - y, c.y + x} : Point{c.x + x, c.y + y}; };
    xrender::Path p;
    p.move_to(at(-size / 4, -size / 2)).line_to(at(size / 4, 0)).line_to(at(-size / 4, size / 2));
    xrender::StrokeStyle st;
    st.width = 1.75f;
    st.cap = xrender::LineCap::round;
    st.join = xrender::LineJoin::round;
    ctx.list.stroke_path(p, st, xrender::Paint::solid(color));
}

}  // namespace

// ── card ──

void Card::mounted() {
    title_.set_style("heading");
    title_.set_text(props.title);
    subtitle_.set_style("caption");
    subtitle_.set_text(props.subtitle);
    services().set(node(), "focusable", Value(props.interactive));
}

void Card::prop_changed(CardProp p) {
    if (p == CardProp::title) title_.set_text(props.title);
    if (p == CardProp::subtitle) subtitle_.set_text(props.subtitle);
    if (!attached()) return;
    if (p == CardProp::interactive) services().set(node(), "focusable", Value(props.interactive));
    if (p == CardProp::title || p == CardProp::subtitle) services().restyle(node());
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

float Card::header(const Theme& theme) const {
    float h = 0;
    if (!props.title.empty()) {
        const TextToken& t = theme.text_style("heading");
        h += std::ceil(t.size * t.line_height);
    }
    if (!props.subtitle.empty()) {
        const TextToken& t = theme.text_style("caption");
        h += std::ceil(t.size * t.line_height) + 2;
    }
    return h;
}

void Card::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.direction = mlibs::layout::Direction::column;
    s.row_gap = s.column_gap = 8;
    const float h = header(theme);
    s.padding = {card_pad + (h > 0 ? h + card_gap : 0), card_pad, card_pad, card_pad};
}

void Card::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("large", 12)).fitted(ctx.size);
    const bool live = props.interactive && !ctx.state.disabled;
    const bool lifted = live && (ctx.state.hovered || ctx.state.focused);
    switch (props.variant) {
        case CardVariant::elevated:
            shadow(ctx, lifted ? "lg" : "md", box, r);
            ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("surface")));
            break;
        case CardVariant::outlined:
            ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("surface")));
            ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1,
                                       xrender::Paint::solid(lifted ? th.color("border_strong") : th.color("border")));
            break;
        case CardVariant::filled:
            ctx.list.fill_round_rect(box, r, xrender::Paint::solid(lifted ? th.color("surface_pressed") : th.color("surface_variant")));
            break;
    }
    if (live && ctx.state.pressed) ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("text").with_alpha(0.04f)));
    if (props.selected) ctx.list.stroke_round_rect({1, 1, box.width - 2, box.height - 2}, r, 2, xrender::Paint::solid(th.color("accent")));
    float y = card_pad;
    if (!title_.text().empty()) {
        title_.paint(ctx, {card_pad, y}, th.color("text"));
        const TextToken& t = th.text_style("heading");
        y += std::ceil(t.size * t.line_height);
    }
    if (!subtitle_.text().empty()) subtitle_.paint(ctx, {card_pad, y + 2}, th.color("text_muted"));
}

void Card::paint_over(PaintContext& ctx) const {
    if (props.interactive && ctx.state.focused && ctx.state.focus_visible)
        parts::focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.theme.corner("large", 12)).fitted(ctx.size));
}

void Card::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.title;
    if (node.description.empty()) node.description = props.subtitle;
    node.states.set(State::selected, props.selected);
    // Only an interactive card can be activated: a still one offers nothing a reader could press.
    if (!props.interactive) std::erase_if(node.actions, [](const spec::ActionDesc& a) { return a.name == "activate"; });
    else node.role = Role::button;
}

std::size_t Card::effect_shapes(Size size, Rect, std::span<xrender::EffectShape> out) const {
    if (out.empty()) return 0;
    out[0] = {{0, 0, size.width, size.height}, CornerRadii::all(12).fitted(size)};
    return 1;
}

ActResult Card::activate(ActContext&) {
    if (!props.interactive) return ActResult::fail("this card is not interactive");
    emit_activated();
    return ActResult::done();
}

// ── accordion ──

void Accordion::mounted() {
    title_.set_style("label");
    title_.set_text(props.title);
    show_content();
    // Its content may be mounted after it: shown or not as it is, once the tree around it is complete.
    services().after(seconds(0), [this] { show_content(); });
}

void Accordion::show_content() {
    if (!attached()) return;
    for (const NodeId c : services().children(node())) services().set(c, "display", props.expanded ? Value{} : Value("none"));
}

void Accordion::prop_changed(AccordionProp p) {
    if (p == AccordionProp::title) title_.set_text(props.title);
    if (!attached()) return;
    if (p == AccordionProp::expanded) show_content();
    services().restyle(node());
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

float Accordion::header_height(const Theme& theme) const {
    const TextToken& t = theme.text_style("label");
    return std::max(theme.scaled(44), std::ceil(t.size * t.line_height) + 24);
}

void Accordion::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.direction = mlibs::layout::Direction::column;
    s.row_gap = s.column_gap = 8;
    s.padding = {header_height(theme), accordion_pad, props.expanded ? accordion_pad : 0, accordion_pad};
}

void Accordion::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    const float hh = header_height(th);
    const Rect head{0, 0, ctx.size.width, hh};
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});
    if (!off && (ctx.state.hovered || ctx.state.pressed))
        ctx.list.fill_round_rect(head, CornerRadii::all(th.corner("medium", 8)).fitted(head.size()),
                                 xrender::Paint::solid(ctx.state.pressed ? th.color("surface_pressed") : th.color("surface_variant")));
    const Size t = title_.measure(ctx.services, th.color("text"));
    title_.paint(ctx, {accordion_pad, (hh - t.height) / 2}, th.color("text"));
    draw_chevron(ctx, {ctx.size.width - accordion_pad - chevron / 2, hh / 2}, chevron, props.expanded, th.color("text_muted"));
    // The line under it: between it and the next.
    ctx.list.fill_rect({0, ctx.size.height - 1, ctx.size.width, 1}, th.color("border"));
    if (off) ctx.list.pop_layer();
}

void Accordion::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible)
        parts::focus_ring(ctx, {0, 0, ctx.size.width, header_height(ctx.theme)}, CornerRadii::all(ctx.theme.corner("medium", 8)));
}

bool Accordion::on_event(EventContext& ctx, const InputEvent& e) {
    // Only its header opens and closes it: a press on its content is the content's.
    if (const auto* p = std::get_if<PointerEvent>(&e); p && p->kind == PointerKind::up)
        return ctx.local.y > header_height(ctx.services.theme());
    return false;
}

void Accordion::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.title;
    node.states.set(State::expanded, props.expanded).set(State::collapsible, true).set(State::disabled, props.disabled);
    // What can be done now: open it when closed, close it when open.
    std::erase_if(node.actions, [&](const spec::ActionDesc& a) { return (a.name == "expand" && props.expanded) || (a.name == "collapse" && !props.expanded); });
}

ActResult Accordion::open(bool expanded) {
    if (props.disabled) return ActResult::fail("the section is disabled");
    if (props.expanded == expanded) return ActResult::done(Value(expanded));
    write_expanded(expanded);
    prop_changed(AccordionProp::expanded);
    emit_toggled(expanded);
    return ActResult::done(Value(expanded)).undo_by(expanded ? "collapse" : "expand");
}

ActResult Accordion::expand(ActContext&) { return open(true); }
ActResult Accordion::collapse(ActContext&) { return open(false); }
ActResult Accordion::activate(ActContext&) { return open(!props.expanded); }

// ── toast ──

Placement Toast::placement() const {
    static constexpr std::array<Placement::Side, 6> sides{Placement::Side::bottom_end, Placement::Side::bottom_start, Placement::Side::top_end,
                                                         Placement::Side::top_start,  Placement::Side::bottom,       Placement::Side::top};
    return {sides[static_cast<std::size_t>(props.corner)], Placement::Anchor::parent, 16, false, false, false, false};
}

Toast::~Toast() {
    if (attached() && timer_) services().cancel(timer_);
}

void Toast::mounted() {
    message_.set_style("body");
    message_.set_text(props.message);
    action_.set_style("label");
    action_.set_text(props.action);
    was_open_ = props.open;
    arm();
}

void Toast::arm() {
    if (timer_) services().cancel(timer_), timer_ = 0;
    if (!props.open || props.duration <= 0) return;
    timer_ = services().after(seconds(props.duration), [this] {
        timer_ = 0;
        close();
    });
}

void Toast::prop_changed(ToastProp p) {
    if (p == ToastProp::message) message_.set_text(props.message);
    if (p == ToastProp::action) action_.set_text(props.action);
    if (!attached()) return;
    if (p == ToastProp::open || p == ToastProp::duration) {
        if (props.open != was_open_) {
            was_open_ = props.open;
            if (!props.open) emit_dismissed();
        }
        arm();
    }
    services().restyle(node());
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

void Toast::close() {
    if (!props.open) return;
    write_open(false);
    prop_changed(ToastProp::open);
}

void Toast::style(const Theme&, mlibs::layout::Style& s) const {
    s.display = props.open ? mlibs::layout::Display::flex : mlibs::layout::Display::none;
    s.padding = {12, 14, 12, 14};
}

Size Toast::measure(MeasureContext& m, std::optional<float>, std::optional<float>, mlibs::layout::Available, mlibs::layout::Available) {
    const Theme& th = m.services.theme();
    const Size msg = message_.measure(m.services, th.color("text"), 320);
    const Size act = props.action.empty() ? Size{} : action_.measure(m.services, th.color("accent"));
    const float w = toast_bar + toast_gap + std::ceil(msg.width) + (props.action.empty() ? 0 : toast_gap + std::ceil(act.width)) + toast_gap + toast_close;
    return {w, std::max({std::ceil(msg.height), std::ceil(act.height), toast_close})};
}

Rect Toast::close_box(Size size) const { return {size.width - 14 - toast_close, (size.height - toast_close) / 2, toast_close, toast_close}; }


void Toast::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 10)).fitted(ctx.size);
    shadow(ctx, "lg", box, r);
    ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("surface")));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, xrender::Paint::solid(th.color("border")));
    const Color tone = props.tone == ToastTone::success ? th.color("success")
                       : props.tone == ToastTone::warning ? th.color("warning")
                       : props.tone == ToastTone::danger  ? th.color("danger")
                                                          : th.color("accent");
    const Rect c = ctx.content;
    ctx.list.fill_round_rect({c.x, c.y + 2, toast_bar, std::max(0.0f, c.height - 4)}, CornerRadii::all(toast_bar / 2), xrender::Paint::solid(tone));
    const Size msg = message_.measure(ctx.services, th.color("text"), 320);
    message_.paint(ctx, {c.x + toast_bar + toast_gap, c.y + (c.height - msg.height) / 2}, th.color("text"));
    if (!props.action.empty()) {
        const Size a = action_.measure(ctx.services, th.color("accent"));
        const float x = close_box(ctx.size).x - toast_gap - a.width;
        action_.paint(ctx, {x, c.y + (c.height - a.height) / 2}, th.color("accent"));
        action_at_ = {x - toast_gap / 2, 0, a.width + toast_gap, ctx.size.height};  // where a press takes it
    }
    parts::cross(ctx, close_box(ctx.size).center(), 9, th.color("text_muted"));
}

bool Toast::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || p->kind != PointerKind::up) return false;
    const Size size = ctx.services.bounds(ctx.node).size();
    const Rect x = close_box(size);
    if (ctx.local.x >= x.x - 6 && ctx.local.x <= x.right() + 6) {
        close();
        return true;
    }
    const Rect a = action_at_;
    if (!props.action.empty() && ctx.local.x >= a.x && ctx.local.x <= a.right()) {
        emit_acted();
        close();
        return true;
    }
    return true;  // a press elsewhere on it does nothing (it is read, not pressed)
}

void Toast::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.message;
    if (!props.action.empty() && node.description.empty()) node.description = props.action;
    // Its action only when it has one.
    if (props.action.empty()) std::erase_if(node.actions, [](const spec::ActionDesc& a) { return a.name == "activate"; });
}

ActResult Toast::dismiss(ActContext&) {
    if (!props.open) return ActResult::done();
    close();
    return ActResult::done();
}

ActResult Toast::activate(ActContext&) {
    if (props.action.empty()) return ActResult::fail("this message has no action");
    emit_acted();
    close();
    return ActResult::done();
}

// ── popover ──

Placement Popover::placement() const {
    static constexpr std::array<Placement::Side, 4> sides{Placement::Side::below, Placement::Side::above, Placement::Side::end, Placement::Side::start};
    return {sides[static_cast<std::size_t>(props.side)], Placement::Anchor::previous, props.arrow ? arrow + 4 : 6, false, false, true, false};
}

void Popover::mounted() { was_open_ = props.open; }

void Popover::prop_changed(PopoverProp p) {
    if (!attached()) return;
    if (p == PopoverProp::open && props.open != was_open_) {
        was_open_ = props.open;
        if (!props.open) emit_closed();
    }
    services().restyle(node());
    services().invalidate_semantics(node());
}

void Popover::style(const Theme&, mlibs::layout::Style& s) const {
    s.display = props.open ? mlibs::layout::Display::flex : mlibs::layout::Display::none;
    s.direction = mlibs::layout::Direction::column;
    s.row_gap = s.column_gap = 8;
    s.padding = {12, 14, 12, 14};
    s.min_width = mlibs::layout::pt(160);
    s.max_width = mlibs::layout::pt(360);
}

void Popover::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 10)).fitted(ctx.size);
    shadow(ctx, "lg", box, r);
    ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("surface")));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, xrender::Paint::solid(th.color("border")));
    if (!props.arrow) return;
    // The point toward what opened it: on the edge facing it, across from its middle.
    const Rect me = ctx.services.bounds(ctx.node);
    Rect anchor = me;
    const NodeId parent = ctx.services.parent(ctx.node);
    for (const NodeId c : ctx.services.children(parent)) {
        if (c == ctx.node) break;
        anchor = ctx.services.bounds(c);
    }
    const bool vertical = props.side == PopoverSide::below || props.side == PopoverSide::above;
    const bool before = (vertical && anchor.center().y < me.y) || (!vertical && anchor.center().x < me.x);
    xrender::Path p;
    if (vertical) {
        const float x = std::clamp(anchor.center().x - me.x, 16.0f, box.width - 16);
        const float y = before ? 0.5f : box.height - 0.5f, tip = before ? -arrow : arrow;
        p.move_to({x - arrow, y}).line_to({x, y + tip}).line_to({x + arrow, y}).close();
    } else {
        const float y = std::clamp(anchor.center().y - me.y, 16.0f, box.height - 16);
        const float x = before ? 0.5f : box.width - 0.5f, tip = before ? -arrow : arrow;
        p.move_to({x, y - arrow}).line_to({x + tip, y}).line_to({x, y + arrow}).close();
    }
    ctx.list.fill_path(p, xrender::FillRule::nonzero, xrender::Paint::solid(th.color("surface")));
    xrender::StrokeStyle st;
    st.width = 1;
    st.join = xrender::LineJoin::round;
    xrender::Path edge;  // its two outer sides only: where it meets the surface, no line
    if (vertical) {
        const float x = std::clamp(anchor.center().x - me.x, 16.0f, box.width - 16);
        const float y = before ? 0.5f : box.height - 0.5f, tip = before ? -arrow : arrow;
        edge.move_to({x - arrow, y}).line_to({x, y + tip}).line_to({x + arrow, y});
    } else {
        const float y = std::clamp(anchor.center().y - me.y, 16.0f, box.height - 16);
        const float x = before ? 0.5f : box.width - 0.5f, tip = before ? -arrow : arrow;
        edge.move_to({x, y - arrow}).line_to({x + tip, y}).line_to({x, y + arrow});
    }
    ctx.list.stroke_path(edge, st, xrender::Paint::solid(th.color("border")));
}

void Popover::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
}

ActResult Popover::dismiss(ActContext&) {
    if (!props.open) return ActResult::done();
    write_open(false);
    prop_changed(PopoverProp::open);
    return ActResult::done();
}

}  // namespace omui
