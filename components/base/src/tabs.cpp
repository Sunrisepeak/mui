module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

using parts::keep_frames;
using parts::mix;

namespace {

constexpr float tab_h = 40, pad_x = 14, indicator_h = 2, bar_pad = 4;

}  // namespace

// ── tabs ──

void Tabs::mounted() {
    x_.attach(services(), 0);
    w_.attach(services(), 0);
}

void Tabs::prop_changed(TabsProp p) {
    if (!attached()) return;
    if (p == TabsProp::variant) services().restyle(node());
    services().invalidate_paint(node());
}

void Tabs::style(const Theme&, layout::Style& s) const {
    s.direction = layout::Direction::row;
    s.align_items = layout::Align::stretch;
    if (props.variant == TabsVariant::bar) s.padding = {bar_pad, bar_pad, bar_pad, bar_pad};
}

NodeId Tabs::current() const {
    if (!attached()) return {};
    NodeId first;
    for (NodeId c : services().children(node())) {
        const std::string id = parts::segment(services(), c);
        if (!first) first = c;
        if (!props.selected.empty() && id == props.selected) return c;
    }
    return props.selected.empty() ? first : NodeId{};
}

void Tabs::choose(NodeId tab, bool focus) {
    const std::string id = parts::segment(services(), tab);
    if (id != props.selected) {
        services().set(node(), "selected", Value(id));  // a bound signal learns it
        emit_changed(id);
    }
    if (focus) services().focus(tab);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

ActResult Tabs::step(int by) {
    std::vector<NodeId> tabs;
    for (NodeId c : services().children(node()))
        if (!services().get(c, "disabled").as_bool()) tabs.push_back(c);
    if (tabs.empty()) return ActResult::fail("no tab to select");
    const NodeId now = current();
    auto it = std::ranges::find(tabs, now);
    std::size_t i = it == tabs.end() ? 0 : static_cast<std::size_t>(it - tabs.begin());
    i = (i + tabs.size() + static_cast<std::size_t>(by + static_cast<int>(tabs.size()))) % tabs.size();
    choose(tabs[i], true);
    return ActResult::done(Value(parts::segment(services(), tabs[i])));
}

ActResult Tabs::next(ActContext&) { return step(1); }
ActResult Tabs::previous(ActContext&) { return step(-1); }

void Tabs::follow(PaintContext& ctx) const {
    const NodeId tab = current();
    if (!tab) return;
    const Rect mine = ctx.services.bounds(ctx.node);
    const Rect b = ctx.services.bounds(tab);
    const double x = b.x - mine.x, w = b.width;
    if (!shown_) {
        x_.jump(x);
        w_.jump(w);
        shown_ = true;
    } else {
        x_.to(x, ctx.theme.transition("motion.spring.snappy"));
        w_.to(w, ctx.theme.transition("motion.spring.snappy"));
    }
    keep_frames(ctx, {&x_, &w_});
}

Rect Tabs::lens(Size size) const {
    // Sliding, it stretches along its way and thins a little — a drop pulled, not a box moved — and rests round.
    const double left = std::abs(x_.target() - x_.value());
    const float stretch = static_cast<float>(std::min(left * 0.35, w_.value() * 0.4));
    const float h = size.height - 2 * bar_pad, thin = std::min(stretch * 0.08f, h * 0.12f);
    const float x = static_cast<float>(x_.value()) - (x_.target() > x_.value() ? 0.0f : stretch);
    return {x, bar_pad + thin / 2, static_cast<float>(w_.value()) + stretch, h - thin};
}

// A bar: a capsule under the tabs, and under the selected one a lens that slides to it.
void Tabs::paint(PaintContext& ctx) const {
    if (props.variant != TabsVariant::bar) return;
    const Theme& th = ctx.theme;
    follow(ctx);
    ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.size.height / 2),
                             render::Paint::solid(th.color("bar", th.color("surface_variant"))));
    if (!current()) return;
    const Rect l = lens(ctx.size);
    ctx.list.fill_round_rect(l, CornerRadii::all(l.height / 2), render::Paint::solid(th.color("tab_lens", th.color("accent_subtle"))));
}

void Tabs::paint_over(PaintContext& ctx) const {
    if (props.variant == TabsVariant::bar) return;
    const Theme& th = ctx.theme;
    ctx.list.fill_rect({0, ctx.size.height - 1, ctx.size.width, 1}, th.color("border"));
    follow(ctx);
    if (!current()) return;
    ctx.list.fill_round_rect({static_cast<float>(x_.value()), ctx.size.height - indicator_h, static_cast<float>(w_.value()), indicator_h},
                             CornerRadii::all(indicator_h / 2), render::Paint::solid(th.color("accent")));
}

std::size_t Tabs::effect_shapes(Size size, Rect, std::span<render::EffectShape> out) const {
    if (props.variant != TabsVariant::bar || out.size() < 2) return 0;
    out[0] = {{0, 0, size.width, size.height}, CornerRadii::all(size.height / 2)};
    if (!current()) return 1;
    const Rect l = lens(size);
    out[1] = {l, CornerRadii::all(l.height / 2)};
    return 2;
}

// ── tab ──

void Tab::mounted() {
    hover_.attach(services(), 0);
    label_.set_style("label");
    label_.set_text(props.text);
}

void Tab::prop_changed(TabProp p) {
    if (p == TabProp::text) {
        label_.set_text(props.text);
        if (attached()) services().invalidate_layout(node());
    }
}

void Tab::style(const Theme& theme, layout::Style& s) const {
    s.padding = {0, theme.scaled(pad_x), 0, theme.scaled(pad_x)};
    s.min_height = layout::pt(theme.scaled(tab_h));
}

Size Tab::measure(MeasureContext& m, std::optional<float>, std::optional<float>, layout::Available, layout::Available) {
    const Size t = label_.measure(m.services, m.services.theme().color("text"));
    return {std::ceil(t.width), std::ceil(t.height)};
}

bool Tab::selected() const {
    if (!attached()) return false;
    const NodeId strip = services().parent(node());
    const std::string chosen(services().get(strip, "selected").as_string());
    if (!chosen.empty()) return chosen == parts::segment(services(), node());
    const auto kids = services().children(strip);
    return !kids.empty() && kids.front() == node();
}

void Tab::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    hover_.to(!off && ctx.state.hovered ? 1.0 : 0.0, th.transition("motion.fast"));
    const float h = static_cast<float>(hover_.value());
    if (h > 0)
        ctx.list.fill_round_rect({0, 4, ctx.size.width, ctx.size.height - 8}, CornerRadii::all(th.corner("small", 4)),
                                 render::Paint::solid(th.color("surface_variant").with_alpha(h)));
    const Color c = off ? th.color("text_disabled") : selected() ? th.color("accent") : mix(th.color("text_muted"), th.color("text"), h);
    const Size t = label_.measure(ctx.services, c);
    label_.paint(ctx, {ctx.content.x + (ctx.content.width - t.width) / 2, ctx.content.y + (ctx.content.height - t.height) / 2}, c);
    parts::keep_frames(ctx, {&hover_});
}

void Tab::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible)
        parts::focus_ring(ctx, {2, 4, ctx.size.width - 4, ctx.size.height - 8}, CornerRadii::all(ctx.theme.corner("small", 4)));
}

void Tab::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.text;
    node.states.set(State::selected, selected());
}

ActResult Tab::select(ActContext&) {
    const NodeId strip = services().parent(node());
    const std::string id = parts::segment(services(), node());
    if (std::string(services().get(strip, "selected").as_string()) != id) {
        services().set(strip, "selected", Value(id));
        services().emit(strip, "changed", Value{}.set("selected", id));
    }
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return ActResult::done(Value(id));
}

}  // namespace omui
