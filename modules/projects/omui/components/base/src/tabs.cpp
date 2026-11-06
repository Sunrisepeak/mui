module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

using parts::mix;

namespace {

constexpr float tab_h = 40, pad_x = 14, indicator_h = 2, bar_pad = 4;

}  // namespace

// ── tabs ──

void Tabs::mounted() {}

void Tabs::prop_changed(TabsProp p) {
    if (!attached()) return;
    if (p == TabsProp::variant) services().restyle(node());
    services().invalidate_paint(node());
}

void Tabs::style(const Theme&, mlibs::layout::Style& s) const {
    s.direction = mlibs::layout::Direction::row;
    s.align_items = mlibs::layout::Align::stretch;
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

// Where the selected tab is, in the tab list's own space (empty: none); asked whenever it is drawn.
Rect Tabs::under() const {
    const NodeId tab = current();
    if (!tab) return {};
    const Rect mine = services().bounds(node());
    const Rect b = services().bounds(tab);
    return {b.x - mine.x, b.y - mine.y, b.width, b.height};
}

// Under the selected tab; its slide to another (drawn out along its way) is its tab list's update slot's.
Rect Tabs::lens(Size size) const {
    const Rect u = under();
    return {u.x, bar_pad, u.width, size.height - 2 * bar_pad};
}

// A bar: a capsule under the tabs, and under the selected one a lens that slides to it.
void Tabs::paint(PaintContext& ctx) const {
    if (props.variant != TabsVariant::bar) return;
    const Theme& th = ctx.theme;
    ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.size.height / 2),
                             xrender::Paint::solid(th.color("bar", th.color("surface_variant"))));
    if (!current()) return;
    const Rect l = lens(ctx.size);
    ctx.list.fill_round_rect(l, CornerRadii::all(l.height / 2), xrender::Paint::solid(th.color("tab_lens", th.color("accent_subtle"))));
}

void Tabs::paint_over(PaintContext& ctx) const {
    if (props.variant == TabsVariant::bar) return;
    const Theme& th = ctx.theme;
    ctx.list.fill_rect({0, ctx.size.height - 1, ctx.size.width, 1}, th.color("border"));
    if (!current()) return;
    const Rect u = under();
    ctx.list.fill_round_rect({u.x, ctx.size.height - indicator_h, u.width, indicator_h},
                             CornerRadii::all(indicator_h / 2), xrender::Paint::solid(th.color("accent")));
}

std::size_t Tabs::effect_shapes(Size size, Rect, std::span<xrender::EffectShape> out) const {
    if (props.variant != TabsVariant::bar || out.size() < 2) return 0;
    out[0] = {{0, 0, size.width, size.height}, CornerRadii::all(size.height / 2)};
    if (!current()) return 1;
    const Rect l = lens(size);
    out[1] = {l, CornerRadii::all(l.height / 2)};
    return 2;
}

// ── tab ──

void Tab::mounted() {
    hover_ = 0;
    label_.set_style("label");
    label_.set_text(props.text);
}

void Tab::prop_changed(TabProp p) {
    if (p == TabProp::text) {
        label_.set_text(props.text);
        if (attached()) services().invalidate_layout(node());
    }
}

void Tab::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.padding = {0, theme.scaled(pad_x), 0, theme.scaled(pad_x)};
    s.min_height = mlibs::layout::pt(theme.scaled(tab_h));
}

Size Tab::measure(MeasureContext& m, std::optional<float>, std::optional<float>, mlibs::layout::Available, mlibs::layout::Available) {
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
    hover_ = !off && ctx.state.hovered ? 1.0 : 0.0;
    const float h = static_cast<float>(hover_);
    // Hovered: the hover's own colour faded in (its alpha kept: a glass pack's is a breath of white, not a slab).
    // On a bar the lens marks the selected one; a hover is lighter still, never a second lens.
    if (const Color hover = th.color("surface_variant"); h > 0)
        ctx.list.fill_round_rect({0, 4, ctx.size.width, ctx.size.height - 8}, CornerRadii::all(th.corner("small", 4)),
                                 xrender::Paint::solid(hover.with_alpha(hover.a * h * (selected() ? 0.0f : 1.0f))));
    const Color c = off ? th.color("text_disabled") : selected() ? th.color("accent") : mix(th.color("text_muted"), th.color("text"), h);
    const Size t = label_.measure(ctx.services, c);
    label_.paint(ctx, {ctx.content.x + (ctx.content.width - t.width) / 2, ctx.content.y + (ctx.content.height - t.height) / 2}, c);
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
