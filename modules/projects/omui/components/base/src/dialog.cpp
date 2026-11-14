module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {


namespace {

constexpr float pad = 24, title_gap = 12;

}  // namespace

// ── dialog ──

void Dialog::mounted() {
    open_ = props.open ? 1 : 0;
    was_open_ = props.open;
    title_.set_style("heading");
    title_.set_text(props.title);
}

void Dialog::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.display = props.open ? mlibs::layout::Display::flex : mlibs::layout::Display::none;
    s.direction = mlibs::layout::Direction::column;
    s.row_gap = s.column_gap = 16;
    const TextToken& t = theme.text_style("heading");
    const float title = props.title.empty() ? 0.0f : t.size * t.line_height + title_gap;
    s.padding = {pad + title, pad, pad, pad};
    s.min_width = mlibs::layout::pt(320);
    s.max_width = mlibs::layout::pt(560);
}

void Dialog::prop_changed(DialogProp p) {
    if (!attached()) return;
    if (p == DialogProp::title) {
        title_.set_text(props.title);
        services().restyle(node());
    }
    if (p == DialogProp::modal) services().invalidate_paint(node());
    if (p != DialogProp::open) return;
    services().restyle(node());
    services().invalidate_semantics(node());
    if (props.open == was_open_) return;
    was_open_ = props.open;
    if (props.open) {
        result_ = "dismissed";
        open_ = 0;
        open_ = 1;
    } else {
        emit_closed(result_);
    }
}

void Dialog::close(std::string result) {
    if (!props.open) return;
    result_ = std::move(result);
    services().set(node(), "open", Value(false));
    if (props.open) {
        props.open = false;
        prop_changed(DialogProp::open);
    }
}

ActResult Dialog::dismiss(ActContext&) {
    close("dismissed");
    return ActResult::done();
}

void Dialog::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const float k = static_cast<float>(open_);
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("large", 12)).fitted(ctx.size);
    // It grows into place on a gentle spring.
    const float s = 0.96f + 0.04f * std::clamp(k, 0.0f, 1.1f);
    const Point c = box.center();
    ctx.list.push_transform(Affine::translate(c.x, c.y) * Affine::scale(s, s) * Affine::translate(-c.x, -c.y));
    if (auto it = th.shadows.find("lg"); it != th.shadows.end())
        paint_shadow(ctx.list, box, r, it->second);
    ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("surface")));
    ctx.list.pop_transform();
    if (!title_.text().empty()) title_.paint(ctx, {pad, pad}, th.color("text"));
}

void Dialog::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.title;
    node.states.set(State::modal, props.modal);
}

// ── tooltip ──

Tooltip::~Tooltip() {
    if (!attached()) return;
    if (timer_) services().cancel(timer_);
    for (std::uint64_t t : listening_) services().off(t);
}

void Tooltip::mounted() {
    fade_ = 0;
    label_.set_style("caption");
    label_.set_text(props.text);
    // The tree around it is complete at the next frame: find the target then
    // (the element before it, else its parent) and listen to it.
    timer_ = services().after(seconds(0), [this] {
        timer_ = 0;
        const NodeId parent = services().parent(node());
        target_ = parent;
        for (NodeId c : services().children(parent)) {
            if (c == node()) break;
            target_ = c;
        }
        listening_.push_back(services().on(target_, "pointer_enter", [this](const Value&) { target_entered(); }));
        listening_.push_back(services().on(target_, "focus", [this](const Value&) { target_entered(); }));
        listening_.push_back(services().on(target_, "pointer_leave", [this](const Value&) { target_left(); }));
        listening_.push_back(services().on(target_, "blur", [this](const Value&) { target_left(); }));
        listening_.push_back(services().on(target_, "pointer_down", [this](const Value&) { target_left(); }));
    });
}

void Tooltip::prop_changed(TooltipProp p) {
    if (p == TooltipProp::text) {
        label_.set_text(props.text);
        if (attached()) services().invalidate_layout(node());
    }
}

void Tooltip::target_entered() {
    if (shown_ || timer_) return;
    timer_ = services().after(seconds(props.delay), [this] {
        timer_ = 0;
        show(true);
    });
}

void Tooltip::target_left() {
    if (timer_) {
        services().cancel(timer_);
        timer_ = 0;
    }
    show(false);
}

void Tooltip::show(bool on) {
    if (on == shown_) return;
    shown_ = on;
    fade_ = on ? 1 : 0;
    services().restyle(node());
    services().invalidate_semantics(node());
}

void Tooltip::style(const Theme&, mlibs::layout::Style& s) const {
    s.display = shown_ ? mlibs::layout::Display::flex : mlibs::layout::Display::none;
    s.padding = {4, 8, 4, 8};
}

Size Tooltip::measure(MeasureContext& m, std::optional<float>, std::optional<float>, mlibs::layout::Available, mlibs::layout::Available) {
    const Size t = label_.measure(m.services, m.services.theme().color("on_tooltip"), 280);
    return {std::ceil(t.width), std::ceil(t.height)};
}

void Tooltip::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const float k = static_cast<float>(std::clamp(fade_, 0.0, 1.0));
    ctx.list.push_layer(k, xrender::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});
    ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(th.corner("small", 4)),
                             xrender::Paint::solid(th.color("tooltip")));
    label_.paint(ctx, {ctx.content.x, ctx.content.y}, th.color("on_tooltip"));
    ctx.list.pop_layer();
}

void Tooltip::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.text;
}

ActResult Tooltip::dismiss(ActContext&) {
    target_left();
    return ActResult::done();
}

}  // namespace omui
