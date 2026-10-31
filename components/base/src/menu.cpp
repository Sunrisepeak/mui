module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

using parts::keep_frames;
using parts::mix;

namespace {

constexpr float item_h = 32, item_pad = 10, shortcut_gap = 24;

}  // namespace

// ── menu ──

void Menu::mounted() {
    open_.attach(services(), props.open ? 1 : 0);
    was_open_ = props.open;
}

void Menu::style(const Theme&, layout::Style& s) const {
    s.display = props.open ? layout::Display::flex : layout::Display::none;
    s.direction = layout::Direction::column;
    s.padding = Insets::all(4);
    s.min_width = layout::pt(180);
    s.border = Insets::all(1);
}

void Menu::opened() {
    // The first item that can take focus takes it (menus are keyboard-first).
    for (NodeId c : services().children(node()))
        if (!services().get(c, "disabled").as_bool()) {
            services().focus(c);
            break;
        }
}

NodeId Menu::trigger() const {
    const NodeId parent = services().parent(node());
    NodeId before = parent;
    for (NodeId c : services().children(parent)) {
        if (c == node()) break;
        before = c;
    }
    return before;
}

void Menu::prop_changed(MenuProp p) {
    if (p != MenuProp::open || !attached()) return;
    services().restyle(node());
    services().invalidate_semantics(node());
    if (props.open == was_open_) return;
    was_open_ = props.open;
    if (props.open) {
        open_.jump(0);
        open_.to(1, services().theme().transition("motion.enter"));
        opened();
    } else {
        // Focus goes back to the menu's button.
        const NodeId anchor = trigger();
        if (!services().state(anchor).focused) services().focus(anchor);
        emit_closed();
    }
}

void Menu::close() {
    if (!props.open) return;
    services().set(node(), "open", Value(false));  // the bound signal learns it; prop_changed does the rest
    if (props.open) {
        props.open = false;  // unbound: it is closed all the same
        prop_changed(MenuProp::open);
    }
}

ActResult Menu::dismiss(ActContext&) {
    close();
    return ActResult::done();
}

void Menu::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const float k = static_cast<float>(std::clamp(open_.value(), 0.0, 1.0));
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    if (auto it = th.shadows.find("lg"); it != th.shadows.end())
        ctx.list.box_shadow(box, r, it->second.offset, it->second.blur, it->second.spread, it->second.color.with_alpha(it->second.color.a * k));
    ctx.list.fill_round_rect(box, r, render::Paint::solid(th.color("surface")));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, render::Paint::solid(th.color("border")));
    keep_frames(ctx, {&open_});
}

void Menu::semantics(SemanticsContext&, spec::SemanticNode& node) const { node.states.set(State::expanded, props.open); }

// ── item ──

void MenuItem::mounted() {
    label_.set_style("body");
    label_.set_text(props.text);
    shortcut_.set_style("caption");
    shortcut_.set_text(props.shortcut);
}

void MenuItem::prop_changed(MenuItemProp p) {
    if (p == MenuItemProp::text) label_.set_text(props.text);
    if (p == MenuItemProp::shortcut) shortcut_.set_text(props.shortcut);
    if (attached() && (p == MenuItemProp::text || p == MenuItemProp::shortcut)) services().invalidate_layout(node());
}

void MenuItem::style(const Theme& theme, layout::Style& s) const {
    s.padding = {0, theme.scaled(item_pad), 0, theme.scaled(item_pad)};
    s.min_height = layout::pt(theme.scaled(item_h));
}

Size MenuItem::measure(MeasureContext& m, std::optional<float>, std::optional<float>, layout::Available, layout::Available) {
    const Size t = label_.measure(m.services, m.services.theme().color("text"));
    float w = std::ceil(t.width);
    if (!props.shortcut.empty()) w += shortcut_gap + std::ceil(shortcut_.measure(m.services, m.services.theme().color("text_muted")).width);
    return {w, std::ceil(t.height)};
}

void MenuItem::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    const bool lit = !off && (ctx.state.hovered || ctx.state.focused || ctx.state.pressed);
    if (lit)
        ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.theme.corner("small", 4)),
                                 render::Paint::solid(ctx.state.pressed ? th.color("surface_pressed") : th.color("surface_variant")));
    const Color c = off ? th.color("text_disabled") : th.color("text");
    const Size t = label_.measure(ctx.services, c);
    label_.paint(ctx, {ctx.content.x, ctx.content.y + (ctx.content.height - t.height) / 2}, c);
    if (!props.shortcut.empty()) {
        const Color sc = th.color("text_muted");
        const Size s = shortcut_.measure(ctx.services, sc);
        shortcut_.paint(ctx, {ctx.content.right() - s.width, ctx.content.y + (ctx.content.height - s.height) / 2}, sc);
    }
}

void MenuItem::paint_over(PaintContext&) const {}  // the highlight shows focus

bool MenuItem::on_event(EventContext&, const InputEvent& e) {
    const auto* k = std::get_if<KeyEvent>(&e);
    if (!k || k->kind != KeyKind::down || (k->key != Key::down && k->key != Key::up && k->key != Key::home && k->key != Key::end))
        return false;
    // Up and Down walk the items, skipping disabled ones, round the ends.
    std::vector<NodeId> items;
    for (NodeId c : services().children(services().parent(node())))
        if (!services().get(c, "disabled").as_bool()) items.push_back(c);
    if (items.empty()) return true;
    auto it = std::ranges::find(items, node());
    std::size_t i = it == items.end() ? 0 : static_cast<std::size_t>(it - items.begin());
    if (k->key == Key::down) i = (i + 1) % items.size();
    else if (k->key == Key::up) i = (i + items.size() - 1) % items.size();
    else if (k->key == Key::home) i = 0;
    else i = items.size() - 1;
    services().focus(items[i]);
    return true;
}

void MenuItem::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.text;
    if (!props.shortcut.empty() && node.description.empty()) node.description = props.shortcut;
    if (props.disabled) node.states.set(State::disabled);
}

ActResult MenuItem::activate(ActContext&) {
    emit_activated();
    // Choosing closes the menu (its verb, if any, runs after this).
    services().set(services().parent(node()), "open", Value(false));
    return ActResult::done();
}

}  // namespace omui
