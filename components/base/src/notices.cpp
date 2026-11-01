module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

namespace {

constexpr float alert_pad = 14, alert_icon = 18, alert_gap = 12, alert_close = 16;
constexpr float step_marker = 26, step_gap = 8, step_connector = 32, step_row = 32;

spec::Rect in_window(const Rect& node, const Rect& part) { return {node.x + part.x, node.y + part.y, part.width, part.height}; }

std::optional<std::int64_t> index_key(std::string_view key) {
    std::int64_t i = 0;
    if (key.empty() || std::from_chars(key.data(), key.data() + key.size(), i).ec != std::errc{}) return std::nullopt;
    return i;
}

Color tone_colour(const Theme& th, AlertTone t) {
    switch (t) {
        case AlertTone::success: return th.color("success");
        case AlertTone::warning: return th.color("warning");
        case AlertTone::danger: return th.color("danger");
        case AlertTone::info: break;
    }
    return th.color("accent");
}

}  // namespace

// ── alert ──

void Alert::mounted() {
    title_.set_style("label");
    message_.set_style("body");
    action_.set_style("label");
    relabel();
}

void Alert::relabel() {
    title_.set_text(props.title);
    message_.set_text(props.message);
    action_.set_text(props.action);
}

void Alert::prop_changed(AlertProp p) {
    relabel();
    if (!attached()) return;
    if (p == AlertProp::open) services().restyle(node());
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

void Alert::style(const Theme&, layout::Style& s) const {
    s.display = props.open ? layout::Display::flex : layout::Display::none;
    s.padding = {12, alert_pad, 12, alert_pad};
}

float Alert::text_width(float width) const {
    float w = width - 2 * alert_pad - alert_icon - alert_gap;
    if (props.dismissible) w -= alert_gap + alert_close;
    return std::max(40.0f, w);
}

Size Alert::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available aw, layout::Available) {
    const Theme& th = m.services.theme();
    // As wide as it may be, up to a line's comfortable measure.
    const float w = width.value_or(aw.is_definite() ? aw.value : 560.0f);
    const float tw = text_width(w + 2 * alert_pad) ;
    float h = 0;
    if (!props.title.empty()) h += std::ceil(title_.measure(m.services, th.color("text"), tw).height);
    if (!props.message.empty()) h += (h > 0 ? 2 : 0) + std::ceil(message_.measure(m.services, th.color("text"), tw).height);
    if (!props.action.empty()) h += 8 + std::ceil(action_.measure(m.services, th.color("accent")).height);
    return {w, height.value_or(std::max(h, alert_icon))};
}

void Alert::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 10)).fitted(ctx.size);
    const Color tone = tone_colour(th, props.tone);
    ctx.list.fill_round_rect(box, r, render::Paint::solid(parts::mix(th.color("surface"), tone, 0.08f)));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, render::Paint::solid(tone.with_alpha(0.45f)));
    const Rect c = ctx.content;
    // Its mark: a disc in its tone, and in it what kind it is (i, ✓, !, ×).
    const Point mid{c.x + alert_icon / 2, c.y + alert_icon / 2 + 1};
    ctx.list.fill_round_rect({mid.x - alert_icon / 2, mid.y - alert_icon / 2, alert_icon, alert_icon}, CornerRadii::all(alert_icon / 2), render::Paint::solid(tone));
    const Color on = th.color("on_accent");
    render::StrokeStyle st;
    st.width = 1.75f;
    st.cap = render::LineCap::round;
    if (props.tone == AlertTone::success) parts::check_mark(ctx, mid, 9, on, 1.75f);
    else if (props.tone == AlertTone::danger) parts::cross(ctx, mid, 7, on);
    else {
        const bool info = props.tone == AlertTone::info;
        render::Path p;
        p.move_to({mid.x, mid.y + (info ? -0.5f : -4.5f)}).line_to({mid.x, mid.y + (info ? 4.5f : 0.5f)});
        ctx.list.stroke_path(p, st, render::Paint::solid(on));
        const float dot_y = info ? mid.y - 4 : mid.y + 4;
        ctx.list.fill_round_rect({mid.x - 1.1f, dot_y - 1.1f, 2.2f, 2.2f}, CornerRadii::all(1.1f), render::Paint::solid(on));
    }
    const float x = c.x + alert_icon + alert_gap, tw = text_width(ctx.size.width);
    float y = c.y;
    if (!props.title.empty()) {
        const Size t = title_.measure(ctx.services, th.color("text"), tw);
        title_.paint(ctx, {x, y}, th.color("text"));
        y += std::ceil(t.height) + 2;
    }
    if (!props.message.empty()) {
        const Size t = message_.measure(ctx.services, th.color("text"), tw);
        message_.paint(ctx, {x, y}, th.color("text"));
        y += std::ceil(t.height);
    }
    action_at_ = {};
    if (!props.action.empty()) {
        const Size t = action_.measure(ctx.services, th.color("accent"));
        y += 8;
        action_.paint(ctx, {x, y}, th.color("accent"));
        action_at_ = {x - 4, y - 4, t.width + 8, t.height + 8};
    }
    close_at_ = {};
    if (props.dismissible) {
        close_at_ = {ctx.size.width - alert_pad - alert_close, c.y + 1, alert_close, alert_close};
        parts::cross(ctx, close_at_.center(), 9, th.color("text_muted"));
    }
}

bool Alert::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || p->kind != PointerKind::up) return false;
    const Rect x{close_at_.x - 6, close_at_.y - 6, close_at_.width + 12, close_at_.height + 12};
    if (props.dismissible && x.contains(ctx.local)) {
        (void)services().perform(node(), "dismiss", {});
        return true;
    }
    if (!props.action.empty() && action_at_.contains(ctx.local)) {
        (void)services().perform(node(), "activate", {});
        return true;
    }
    return false;
}

void Alert::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.title.empty() ? props.message : props.message.empty() ? props.title : props.title + ": " + props.message;
    static constexpr std::array<std::string_view, 4> tones{"info", "success", "warning", "danger"};
    node.value = Value{}.set("tone", std::string(tones[static_cast<std::size_t>(props.tone)]));
    if (!props.action.empty() && node.description.empty()) node.description = props.action;
    std::erase_if(node.actions, [&](const spec::ActionDesc& a) {
        return (a.name == "activate" && props.action.empty()) || (a.name == "dismiss" && !props.dismissible);
    });
}

ActResult Alert::activate(ActContext&) {
    if (props.action.empty()) return ActResult::fail("this alert has no action");
    emit_acted();
    return ActResult::done();
}

ActResult Alert::dismiss(ActContext&) {
    if (!props.dismissible) return ActResult::fail("this alert is not dismissible");
    if (!props.open) return ActResult::done();
    write_open(false);
    emit_dismissed();
    services().restyle(node());
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
    return ActResult::done();
}

// ── stepper ──

void Stepper::mounted() { relabel(); }

void Stepper::relabel() {
    labels_.resize(props.steps.size());
    digits_.resize(props.steps.size());
    for (std::size_t i = 0; i < props.steps.size(); ++i) {
        labels_[i].set_style("label");
        labels_[i].set_text(props.steps[i]);
        digits_[i].set_style("caption");
        digits_[i].set_text(std::to_string(i + 1));
    }
}

void Stepper::prop_changed(StepperProp) {
    relabel();
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

bool Stepper::reachable(std::size_t i) const noexcept {
    const auto cur = static_cast<std::size_t>(std::max<std::int64_t>(0, props.value));
    return i < props.steps.size() && i != cur && (props.free || i < cur);
}

Size Stepper::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, layout::Available, layout::Available) {
    const Theme& th = m.services.theme();
    float w = 0, h = 0;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const Size t = labels_[i].measure(m.services, th.color("text"));
        if (props.vertical) {
            w = std::max(w, step_marker + step_gap + std::ceil(t.width));
            h += step_row + (i ? step_connector / 2 : 0);
        } else {
            w += step_marker + step_gap + std::ceil(t.width) + (i ? step_connector : 0);
            h = step_row;
        }
    }
    return {width.value_or(w), height.value_or(h)};
}

void Stepper::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const auto cur = static_cast<std::size_t>(std::max<std::int64_t>(0, props.value));
    // The steps' own widths; in a row, the room left goes to the connectors between them.
    std::vector<float> own;
    float natural = 0;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        own.push_back(step_marker + step_gap + std::ceil(labels_[i].measure(ctx.services, th.color("text")).width));
        natural += own.back();
    }
    const float link = props.vertical || labels_.size() < 2 ? step_connector
                                                            : std::max(step_connector, (ctx.size.width - natural) / static_cast<float>(labels_.size() - 1));
    at_.clear();
    float x = 0, y = 0;
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const bool done = i < cur, now = i == cur;
        const Rect r{x, y, own[i], step_row};
        at_.push_back(r);
        if (hover_ == static_cast<int>(i) && reachable(i))
            ctx.list.fill_round_rect({r.x - 4, r.y, r.width + 8, r.height}, CornerRadii::all(6), render::Paint::solid(th.color("surface_variant")));
        const Point m{r.x + step_marker / 2, r.y + step_row / 2};
        const Rect disc{m.x - step_marker / 2, m.y - step_marker / 2, step_marker, step_marker};
        if (done || now) ctx.list.fill_round_rect(disc, CornerRadii::all(step_marker / 2), render::Paint::solid(th.color("accent")));
        else {
            ctx.list.fill_round_rect(disc, CornerRadii::all(step_marker / 2), render::Paint::solid(th.color("surface")));
            ctx.list.stroke_round_rect({disc.x + 0.75f, disc.y + 0.75f, disc.width - 1.5f, disc.height - 1.5f}, CornerRadii::all(step_marker / 2), 1.5f,
                                       render::Paint::solid(th.color("border_strong")));
        }
        if (done) parts::check_mark(ctx, m, 11, th.color("on_accent"), 2);
        else {
            const Color dc = now ? th.color("on_accent") : th.color("text_muted");
            const Size d = digits_[i].measure(ctx.services, dc);
            digits_[i].paint(ctx, {m.x - d.width / 2, m.y - d.height / 2}, dc);
        }
        const Color lc = now ? th.color("text") : done ? th.color("text") : th.color("text_muted");
        const Size t = labels_[i].measure(ctx.services, lc);
        labels_[i].paint(ctx, {r.x + step_marker + step_gap, r.y + (step_row - t.height) / 2}, lc);
        // The connector to the next: filled once this step is done.
        if (i + 1 < labels_.size()) {
            const Color cc = done ? th.color("accent") : th.color("border");
            if (props.vertical) {
                ctx.list.fill_rect({m.x - 1, r.bottom() + 2, 2, step_connector / 2 - 4}, cc);
                y = r.bottom() + step_connector / 2;
            } else {
                ctx.list.fill_rect({r.right() + 8, m.y - 1, link - 16, 2}, cc);
                x = r.right() + link;
            }
        }
    }
}

void Stepper::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible)) return;
    const auto cur = static_cast<std::size_t>(std::max<std::int64_t>(0, props.value));
    if (cur < at_.size()) parts::focus_ring(ctx, {at_[cur].x - 4, at_[cur].y, at_[cur].width + 8, at_[cur].height}, CornerRadii::all(6));
}

bool Stepper::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    int over = -1;
    for (std::size_t i = 0; i < at_.size(); ++i)
        if (at_[i].contains(ctx.local)) over = static_cast<int>(i);
    if (p->kind == PointerKind::leave) over = -1;
    if (over != hover_) hover_ = over, ctx.repaint();
    if (p->kind == PointerKind::up && over >= 0 && reachable(static_cast<std::size_t>(over))) {
        (void)services().perform(node(), "set_value", Value{}.set("value", static_cast<std::int64_t>(over)));
        return true;
    }
    return false;
}

ActResult Stepper::go(std::int64_t to) {
    const std::int64_t n = static_cast<std::int64_t>(props.steps.size());
    if (to < 0 || to >= n) return ActResult::fail(std::format("there is no step {} (0–{})", to, n - 1));
    const std::int64_t old = props.value;
    if (to != old) {
        if (!props.free && to > old) return ActResult::fail(std::format("step {} is ahead: the steps before it come first", to));
        write_value(to);
        emit_changed(to);
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(to)).undo_by("set_value", Value{}.set("value", old));
}

ActResult Stepper::set_value(ActContext& ctx, const Value& value) {
    if (!value.is_number()) return ActResult::fail(std::format("set_value takes a step's index, not {}", value.json()));
    const std::int64_t to = value.as_int();
    // Taken back: the step it was on, wherever that is.
    if (ctx.restoring && to >= 0 && to < static_cast<std::int64_t>(props.steps.size())) {
        const std::int64_t old = props.value;
        write_value(to);
        emit_changed(to);
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
        return ActResult::done(Value(to)).undo_by("set_value", Value{}.set("value", old));
    }
    return go(to);
}

ActResult Stepper::increment(ActContext&) { return go(props.value + 1); }
ActResult Stepper::decrement(ActContext&) { return go(props.value - 1); }

spec::SemanticNode Stepper::item(std::string_view path, std::size_t i) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, i);
    const auto cur = static_cast<std::size_t>(std::max<std::int64_t>(0, props.value));
    n.role = reachable(i) ? Role::link : Role::text;
    n.name = props.steps[i];
    n.value = Value(std::string(i < cur ? "done" : i == cur ? "current" : "ahead"));
    n.states.set(State::selected, i == cur);
    if (i < at_.size() && attached()) n.bounds = in_window(services().bounds(node()), at_[i]);
    if (reachable(i)) n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

void Stepper::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("value", props.value).set("steps", static_cast<std::int64_t>(props.steps.size()));
}

std::vector<spec::SemanticNode> Stepper::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (std::size_t i = 0; i < props.steps.size(); ++i) out.push_back(item(path, i));
    return out;
}

std::optional<spec::SemanticNode> Stepper::virtual_item(std::string_view path, std::string_view key) const {
    const auto i = index_key(key);
    if (!i || *i < 0 || static_cast<std::size_t>(*i) >= props.steps.size()) return std::nullopt;
    return item(path, static_cast<std::size_t>(*i));
}

ActResult Stepper::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    // Its inverse taken back on the item (S-12) is the stepper's own set_value.
    if (action != "activate") return act(ctx, action);
    const auto i = index_key(key);
    if (!i) return ActResult::fail(std::format("there is no step '{}'", key));
    if (!reachable(static_cast<std::size_t>(*i))) return ActResult::fail(std::format("step {} is not one to go to now", *i));
    return go(*i);
}

}  // namespace omui
