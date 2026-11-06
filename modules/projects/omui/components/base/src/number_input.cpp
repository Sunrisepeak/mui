module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

using parts::mix;

namespace {

constexpr float field_h = 36, button_w = 32, default_w = 148;

void sign(PaintContext& ctx, Point c, bool plus, Color color) {
    xrender::Path p;
    p.move_to({c.x - 5, c.y}).line_to({c.x + 5, c.y});
    if (plus) p.move_to({c.x, c.y - 5}).line_to({c.x, c.y + 5});
    xrender::StrokeStyle st;
    st.width = 1.75f;
    st.cap = xrender::LineCap::round;
    ctx.list.stroke_path(p, st, xrender::Paint::solid(color));
}

}  // namespace

void NumberInput::mounted() {
    text_.set_style("label");
    text_.set_text(shown());
}

void NumberInput::prop_changed(NumberInputProp) {
    text_.set_text(draft_ ? *draft_ : shown());
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::string NumberInput::shown() const {
    const int digits = static_cast<int>(std::clamp<std::int64_t>(props.digits, 0, 12));
    std::string s = std::format("{:.{}f}", props.value, digits);
    if (s == "-0" || (s.starts_with("-0.") && std::ranges::all_of(s.substr(3), [](char c) { return c == '0'; }))) s.erase(0, 1);
    return props.suffix.empty() ? s : s + " " + props.suffix;
}

double NumberInput::kept(double v) const noexcept {
    const double lo = std::min(props.min, props.max), hi = std::max(props.min, props.max);
    if (props.step > 0) {
        const double from = std::isfinite(lo) && lo > -1e299 ? lo : 0.0;
        v = from + std::round((v - from) / props.step) * props.step;
    }
    return std::clamp(v, lo, hi);
}

void NumberInput::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.min_height = mlibs::layout::pt(theme.scaled(field_h));
    s.padding = {0, button_w, 0, button_w};
}

Size NumberInput::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    const Size t = text_.measure(m.services, m.services.theme().color("text"));
    const float w = width.value_or(aw.is_definite() ? std::min(aw.value, default_w - 2 * button_w) : default_w - 2 * button_w);
    return {std::max(w, std::ceil(t.width) + 16), height.value_or(std::max(m.services.theme().scaled(field_h), std::ceil(t.height)))};
}

float NumberInput::baseline(MeasureContext& m, Size content) {
    const Color c = m.services.theme().color("text");
    const Size t = text_.measure(m.services, c);
    return (content.height - t.height) / 2 + text_.baseline(m.services, c);
}

Rect NumberInput::minus_box(Size size) const { return {0, 0, button_w, size.height}; }
Rect NumberInput::plus_box(Size size) const { return {size.width - button_w, 0, button_w, size.height}; }

void NumberInput::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, box);
    ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("surface")));
    const bool editing = ctx.state.focused;
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, editing ? 1.5f : 1,
                               xrender::Paint::solid(editing ? th.color("accent") : th.color("border")));
    const double lo = std::min(props.min, props.max), hi = std::max(props.min, props.max);
    for (const bool plus : {false, true}) {
        const Rect b = plus ? plus_box(ctx.size) : minus_box(ctx.size);
        const bool at_end = plus ? props.value >= hi : props.value <= lo;
        if (!off && !at_end && hover_ == (plus ? 1 : -1))
            ctx.list.fill_round_rect({b.x + 3, b.y + 3, b.width - 6, b.height - 6}, CornerRadii::all(6), xrender::Paint::solid(th.color("surface_variant")));
        sign(ctx, b.center(), plus, at_end ? th.color("text_disabled") : th.color("text_muted"));
    }
    const Color c = draft_ ? th.color("text") : th.color("text");
    const Size t = text_.measure(ctx.services, c);
    const Point at{ctx.content.x + (ctx.content.width - t.width) / 2, ctx.content.y + (ctx.content.height - t.height) / 2};
    text_.paint(ctx, at, c);
    if (editing && !off) {
        // Typed digits go in at the end: a caret there, and the input method's candidates.
        const Rect caret{at.x + t.width + 1, at.y + 2, 1.5f, std::max(0.0f, t.height - 4)};
        if (draft_) ctx.list.fill_rect(caret, th.color("accent"));
        ctx.services.text_input(ctx.node, caret);
    }
    if (off) ctx.list.pop_layer();
}

void NumberInput::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible)
        parts::focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.theme.corner("medium", 8)).fitted(ctx.size));
}

bool NumberInput::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* f = std::get_if<FocusEvent>(&e)) {
        focused_ = f->gained;
        if (!focused_) {
            commit_draft();
            services().text_input(node(), std::nullopt);
        }
        ctx.repaint();
        return false;
    }
    if (props.disabled) return false;
    if (const auto* p = std::get_if<PointerEvent>(&e)) {
        const Size size = ctx.services.bounds(ctx.node).size();
        const int over = minus_box(size).contains(ctx.local) ? -1 : plus_box(size).contains(ctx.local) ? 1 : 0;
        if (p->kind == PointerKind::leave) hover_ = 0, ctx.repaint();
        else if (over != hover_) hover_ = over, ctx.repaint();
        if (p->kind == PointerKind::up && over != 0) {
            // A step by the person's hand: an action of the history, as an agent's would be.
            (void)services().perform(node(), over > 0 ? "increment" : "decrement", Value{});
            return true;
        }
        return false;
    }
    if (const auto* t = std::get_if<TextEvent>(&e)) {
        std::string d = draft_.value_or(std::string());
        for (const char c : t->text)
            if ((c >= '0' && c <= '9') || (c == '.' && !d.contains('.')) || (c == '-' && d.empty())) d += c;
        draft_ = std::move(d);
        text_.set_text(*draft_);
        services().invalidate_layout(node());
        return true;
    }
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down) {
        if (k->key == Key::backspace && draft_) {
            if (!draft_->empty()) draft_->pop_back();
            text_.set_text(*draft_);
            services().invalidate_layout(node());
            return true;
        }
        if (k->key == Key::enter) {
            commit_draft();
            return true;
        }
        if (k->key == Key::escape && draft_) {
            draft_.reset();
            text_.set_text(shown());
            services().invalidate_layout(node());
            return true;
        }
    }
    return false;
}

void NumberInput::commit_draft() {
    if (!draft_) return;
    const std::string d = std::exchange(draft_, std::nullopt).value();
    double v = props.value;
    const bool read = !d.empty() && d != "-" && d != "." && std::from_chars(d.data(), d.data() + d.size(), v).ec == std::errc{};
    if (read && kept(v) != props.value) (void)services().perform(node(), "set_value", Value{}.set("value", v));
    text_.set_text(shown());
    services().invalidate_layout(node());
}

void NumberInput::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    Value v = Value{}.set("value", props.value).set("step", props.step);
    if (props.min > -1e299) v.set("min", props.min);
    if (props.max < 1e299) v.set("max", props.max);
    node.value = std::move(v);
    node.states.set(State::disabled, props.disabled).set(State::editable, !props.disabled);
}

std::size_t NumberInput::effect_shapes(Size size, Rect, std::span<xrender::EffectShape> out) const {
    if (out.empty()) return 0;
    out[0] = {{0, 0, size.width, size.height}, CornerRadii::all(8).fitted(size)};
    return 1;
}

ActResult NumberInput::change(double v, bool exact) {
    if (props.disabled) return ActResult::fail("the field is disabled");
    const double old = props.value;
    v = exact ? v : kept(v);
    if (v == old) return ActResult::done(Value(old));
    write_value(v);
    draft_.reset();
    prop_changed(NumberInputProp::value);
    emit_changed(v);
    emit_committed(v);
    return ActResult::done(Value(v)).undo_by("set_value", Value{}.set("value", old));
}

ActResult NumberInput::increment(ActContext&) { return change(props.value + (props.step > 0 ? props.step : 1)); }
ActResult NumberInput::decrement(ActContext&) { return change(props.value - (props.step > 0 ? props.step : 1)); }

ActResult NumberInput::set_value(ActContext& ctx, const Value& value) {
    if (value.as_string() == "min") return change(std::min(props.min, props.max));
    if (value.as_string() == "max") return change(std::max(props.min, props.max));
    if (!value.is_number()) return ActResult::fail(std::format("set_value takes a number (or min, max), not {}", value.json()));
    // Taken back: the value it had, exactly (S-12).
    return change(value.as_double(), ctx.restoring);
}

}  // namespace omui
