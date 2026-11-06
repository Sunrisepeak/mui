module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

namespace {

constexpr float split_handle = 8, split_grip = 28;
constexpr float drawer_pad = 20, drawer_head = 60, drawer_close = 32;
constexpr double split_step = 0.05;

}  // namespace

// ── split view ──

SplitView::~SplitView() {
    if (attached() && timer_) services().cancel(timer_);
}

void SplitView::mounted() {
    apply();
    // Its panes may be mounted after it: given their shares once the tree around it is complete.
    timer_ = services().after(seconds(0), [this] {
        timer_ = 0;
        apply();
    });
}

std::vector<NodeId> SplitView::panes() const {
    std::vector<NodeId> out;
    for (const NodeId c : services().children(node()))
        if (out.size() < 2) out.push_back(c);
    return out;
}

double SplitView::kept(double ratio) const noexcept {
    const double lo = std::clamp(props.min, 0.0, 0.5);
    return std::clamp(ratio, lo, 1 - lo);
}

void SplitView::apply() {
    if (!attached()) return;
    const auto p = panes();
    const double r = kept(props.ratio);
    for (std::size_t i = 0; i < p.size(); ++i) {
        // Their shares of what is left after the handle: grown from nothing, in proportion.
        services().set(p[i], "grow", Value(i == 0 ? r : 1 - r));
        services().set(p[i], "shrink", Value(1.0));
        services().set(p[i], "basis", Value(0.0));
        services().set(p[i], props.vertical ? "min_height" : "min_width", Value(0.0));
    }
    services().invalidate_layout(node());
}

void SplitView::prop_changed(SplitViewProp p) {
    if (!attached()) return;
    if (p == SplitViewProp::vertical) services().restyle(node());
    apply();
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void SplitView::style(const Theme&, mlibs::layout::Style& s) const {
    s.direction = props.vertical ? mlibs::layout::Direction::column : mlibs::layout::Direction::row;
    s.row_gap = s.column_gap = split_handle;
}

Rect SplitView::handle() const {
    const auto p = panes();
    const Rect me = services().bounds(node());
    if (p.empty()) return {};
    const Rect first = services().bounds(p[0]);
    if (props.vertical) return {0, first.bottom() - me.y, me.width, split_handle};
    return {first.right() - me.x, 0, split_handle, me.height};
}

void SplitView::paint(PaintContext&) const {}  // the handle is drawn over its panes' edges (paint_over)

void SplitView::paint_over(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect h = handle();
    if (h.width <= 0 && h.height <= 0) return;
    const bool lit = hover_ || dragging_;
    const Color line = lit ? th.color("accent") : th.color("border");
    // A line down its middle, and a grip on it.
    if (props.vertical) {
        ctx.list.fill_rect({h.x, h.center().y - (lit ? 1.0f : 0.5f), h.width, lit ? 2.0f : 1.0f}, line);
        const Rect grip{h.center().x - split_grip / 2, h.center().y - 3, split_grip, 6};
        ctx.list.fill_round_rect(grip, CornerRadii::all(3), xrender::Paint::solid(lit ? th.color("accent") : th.color("border_strong")));
    } else {
        ctx.list.fill_rect({h.center().x - (lit ? 1.0f : 0.5f), h.y, lit ? 2.0f : 1.0f, h.height}, line);
        const Rect grip{h.center().x - 3, h.center().y - split_grip / 2, 6, split_grip};
        ctx.list.fill_round_rect(grip, CornerRadii::all(3), xrender::Paint::solid(lit ? th.color("accent") : th.color("border_strong")));
    }
    if (ctx.state.focused && ctx.state.focus_visible) parts::focus_ring(ctx, h, CornerRadii::all(3));
}

void SplitView::change(double ratio) {
    ratio = kept(ratio);
    if (ratio != props.ratio) {
        write_ratio(ratio);
        emit_changed(ratio);
        apply();
        services().invalidate_semantics(node());
    }
}

bool SplitView::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    const Rect h = handle(), me = services().bounds(node());
    const Rect grab = props.vertical ? Rect{h.x, h.y - 3, h.width, h.height + 6} : Rect{h.x - 3, h.y, h.width + 6, h.height};
    const bool over = grab.contains(ctx.local);
    if (!dragging_ && over != hover_) {
        hover_ = over;
        services().set(node(), "cursor", over ? Value(props.vertical ? "row-resize" : "col-resize") : Value{});
        services().invalidate_paint(node());
    }
    auto ratio_at = [&](Point at) {
        const float room = (props.vertical ? me.height : me.width) - split_handle;
        return room <= 0 ? props.ratio : static_cast<double>(((props.vertical ? at.y : at.x) - split_handle / 2) / room);
    };
    switch (p->kind) {
        case PointerKind::down:
            if (!over || (p->type == PointerType::mouse && p->button != MouseButton::left)) return false;
            dragging_ = true;
            from_ = props.ratio;
            services().capture_pointer(node());
            services().focus(node());
            return true;
        case PointerKind::move:
            if (!dragging_) return false;
            change(ratio_at(ctx.local));
            return true;
        case PointerKind::up:
        case PointerKind::cancel:
            if (!dragging_) return false;
            dragging_ = false;
            services().release_pointer();
            // The drag as one action of the person's (S-12).
            if (props.ratio != from_) services().performed(node(), "set_value", Value{}.set("value", props.ratio), {{"set_value", Value{}.set("value", from_)}});
            services().invalidate_paint(node());
            return true;
        default:
            return false;
    }
}

ActResult SplitView::to(double ratio, bool exact) {
    const double old = props.ratio;
    if (exact) {
        write_ratio(ratio);
        emit_changed(ratio);
        apply();
        services().invalidate_semantics(node());
    } else {
        change(ratio);
    }
    services().invalidate_paint(node());
    return ActResult::done(Value(props.ratio)).undo_by("set_value", Value{}.set("value", old));
}

ActResult SplitView::set_value(ActContext& ctx, const Value& value) {
    if (!value.is_number()) return ActResult::fail(std::format("set_value takes the first pane's share, 0–1, not {}", value.json()));
    return to(value.as_double(), ctx.restoring);
}

ActResult SplitView::increment(ActContext&) { return to(props.ratio + split_step); }
ActResult SplitView::decrement(ActContext&) { return to(props.ratio - split_step); }

void SplitView::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("ratio", props.ratio);
}

std::vector<spec::SemanticNode> SplitView::virtual_items(std::string_view path) const { return {*virtual_item(path, "handle")}; }

std::optional<spec::SemanticNode> SplitView::virtual_item(std::string_view path, std::string_view key) const {
    if (key != "handle") return std::nullopt;
    spec::SemanticNode n;
    n.id = std::format("{}/handle", path);
    n.role = Role::splitter;
    n.name = props.label.empty() ? std::string("Resize") : std::format("Resize {}", props.label);
    const double lo = std::clamp(props.min, 0.0, 0.5);
    n.value = Value{}.set("value", std::round(props.ratio * 100)).set("min", std::round(lo * 100)).set("max", std::round((1 - lo) * 100));
    if (attached()) {
        const Rect me = services().bounds(node()), h = handle();
        n.bounds = spec::Rect{me.x + h.x, me.y + h.y, h.width, h.height};
    }
    for (const char* a : {"increment", "decrement"}) n.actions.push_back({a, Effect::reversible, {}, {}, {}});
    n.actions.push_back({"set_value", Effect::reversible, {}, {}, {{"value", "f64"}}});
    return n;
}

ActResult SplitView::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (key != "handle") return ActResult::fail(std::format("no item '{}' (handle)", key));
    // The handle's value is a percentage. Taken back (S-12), it is the view's own share, 0–1: its set_value.
    if (action == "set_value" && !ctx.restoring) {
        const Value* v = ctx.args.get("value");
        if (!v || !v->is_number()) return ActResult::fail("set_value takes the first pane's percentage");
        return to(v->as_double() / 100);
    }
    return act(ctx, action);
}

// ── drawer ──

Placement Drawer::placement() const {
    return {props.side == DrawerSide::start ? Placement::Side::left : Placement::Side::right, Placement::Anchor::parent, 0, props.modal, props.modal, true, false};
}

void Drawer::mounted() {
    title_.set_style("heading");
    title_.set_text(props.title);
    was_open_ = props.open;
}

void Drawer::prop_changed(DrawerProp p) {
    if (p == DrawerProp::title) title_.set_text(props.title);
    if (!attached()) return;
    if (p == DrawerProp::open && props.open != was_open_) {
        was_open_ = props.open;
        if (!props.open) emit_closed();
    }
    services().restyle(node());
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

void Drawer::style(const Theme&, mlibs::layout::Style& s) const {
    s.display = props.open ? mlibs::layout::Display::flex : mlibs::layout::Display::none;
    s.direction = mlibs::layout::Direction::column;
    s.width = mlibs::layout::pt(static_cast<float>(props.size));
    s.padding = {drawer_head, drawer_pad, drawer_pad, drawer_pad};
    s.row_gap = s.column_gap = 12;
}

Rect Drawer::close_box(Size size) const { return {size.width - drawer_pad + 6 - drawer_close, (drawer_head - drawer_close) / 2, drawer_close, drawer_close}; }

void Drawer::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    parts::shadow(ctx, "lg", box, {});
    ctx.list.fill_rect(box, th.color("surface"));
    // The edge it rises from: a line on its inner side.
    const float edge = props.side == DrawerSide::start ? box.width - 1 : 0;
    ctx.list.fill_rect({edge, 0, 1, box.height}, th.color("border"));
    const Size t = title_.measure(ctx.services, th.color("text"), box.width - 2 * drawer_pad - drawer_close);
    title_.paint(ctx, {drawer_pad, (drawer_head - t.height) / 2 + 4}, th.color("text"));
    parts::cross(ctx, close_box(ctx.size).center() + Point{0, 4}, 11, th.color("text_muted"));
}

bool Drawer::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || p->kind != PointerKind::up) return false;
    const Rect x = close_box(services().bounds(node()).size());
    if (Rect{x.x - 4, x.y, x.width + 8, x.height + 8}.contains(ctx.local)) {
        (void)services().perform(node(), "dismiss", {});
        return true;
    }
    return false;
}

void Drawer::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.title;
}

ActResult Drawer::dismiss(ActContext&) {
    if (!props.open) return ActResult::done();
    write_open(false);
    prop_changed(DrawerProp::open);
    return ActResult::done();
}

}  // namespace omui
