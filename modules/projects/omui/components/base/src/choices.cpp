module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

using parts::mix;

namespace {

constexpr float seg_h = 32, seg_pad = 14, seg_inset = 3;
constexpr float chip_h = 28, chip_pad = 12, chip_icon = 14, chip_gap = 6;
constexpr float range_track = 4, range_thumb = 18, range_w = 220, range_h = 24;

// A part's box as the semantic tree states bounds: in the window.
spec::Rect in_window(const Rect& node, const Rect& part) { return {node.x + part.x, node.y + part.y, part.width, part.height}; }

bool primary_press(const PointerEvent& p) { return p.type != PointerType::mouse || p.button == MouseButton::left; }

}  // namespace

// ── segmented ──

void Segmented::mounted() { prop_changed(SegmentedProp::options); }

void Segmented::prop_changed(SegmentedProp) {
    labels_.resize(props.options.size());
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        labels_[i].set_style("label");
        labels_[i].set_text(label_of(i));
    }
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::string Segmented::label_of(std::size_t i) const { return i < props.labels.size() ? props.labels[i] : props.options[i]; }

std::optional<std::size_t> Segmented::index_of(std::string_view value) const {
    for (std::size_t i = 0; i < props.options.size(); ++i)
        if (props.options[i] == value) return i;
    return std::nullopt;
}

Size Segmented::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    float w = 2 * seg_inset;
    for (parts::Label& l : labels_) w += std::ceil(l.measure(m.services, m.services.theme().color("text")).width) + 2 * seg_pad;
    return {width.value_or(w), height.value_or(seg_h)};
}

void Segmented::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const float radius = th.corner("medium", 8);
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, box);
    ctx.list.fill_round_rect(box, CornerRadii::all(radius).fitted(ctx.size), xrender::Paint::solid(th.color("surface_variant")));
    // The segments share the width as their labels ask (any room left, evenly).
    float natural = 0;
    std::vector<float> widths;
    for (parts::Label& l : labels_) {
        widths.push_back(std::ceil(l.measure(ctx.services, th.color("text")).width) + 2 * seg_pad);
        natural += widths.back();
    }
    const float extra = labels_.empty() ? 0 : std::max(0.0f, ctx.size.width - 2 * seg_inset - natural) / static_cast<float>(labels_.size());
    at_.clear();
    float x = seg_inset;
    const auto chosen = index_of(props.value);
    for (std::size_t i = 0; i < labels_.size(); ++i) {
        const Rect r{x, seg_inset, widths[i] + extra, ctx.size.height - 2 * seg_inset};
        at_.push_back(r);
        x = r.right();
    }
    for (std::size_t i = 0; i < at_.size(); ++i) {
        const Rect& r = at_[i];
        const CornerRadii rr = CornerRadii::all(radius - seg_inset).fitted(r.size());
        if (chosen == i) {
            ctx.part({.role="selection",.reference=props.options[i],.allocation=r,.kind=xrender::PartKind::box},[&] {
                parts::shadow(ctx, "sm", r, rr);
                ctx.list.fill_round_rect(r, rr, xrender::Paint::solid(th.color("surface")));
            });
        } else if (hover_ == static_cast<int>(i)) {
            ctx.part({.role="hover",.reference=props.options[i],.allocation=r,.kind=xrender::PartKind::box},[&] {
                ctx.list.fill_round_rect(r, rr, xrender::Paint::solid(th.color("surface_pressed").with_alpha(0.5f)));
            });
        }
        const Color c = chosen == i ? th.color("text") : th.color("text_muted");
        const Size t = labels_[i].measure(ctx.services, c);
        ctx.part({.role="label",.id=props.options[i],.reference=props.options[i],.allocation=r},[&] {
            labels_[i].paint(ctx, {r.x + (r.width - t.width) / 2, r.y + (r.height - t.height) / 2}, c);
        });
    }
    if (off) ctx.list.pop_layer();
}

void Segmented::paint_over(PaintContext& ctx) const {
    const auto chosen = index_of(props.value);
    if (!(ctx.state.focused && ctx.state.focus_visible)) return;
    parts::focus_ring(ctx, chosen && *chosen < at_.size() ? at_[*chosen] : Rect{0, 0, ctx.size.width, ctx.size.height},
                      CornerRadii::all(ctx.theme.corner("medium", 8) - seg_inset));
}

bool Segmented::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || props.disabled) return false;
    int over = -1;
    for (std::size_t i = 0; i < at_.size(); ++i)
        if (at_[i].contains(ctx.local)) over = static_cast<int>(i);
    if (p->kind == PointerKind::leave) over = -1;
    if (over != hover_) hover_ = over, ctx.repaint();
    if (p->kind == PointerKind::up && over >= 0 && primary_press(*p)) {
        // As the person's action: journaled, undone as an agent's is.
        (void)services().perform(node(), "set_value", Value{}.set("value", props.options[static_cast<std::size_t>(over)]));
        return true;
    }
    return p->kind == PointerKind::down && over >= 0;
}

ActResult Segmented::choose(std::string value) {
    if (!index_of(value)) {
        std::string known;
        for (const std::string& o : props.options) known += known.empty() ? o : ", " + o;
        return ActResult::fail(std::format("`{}` is not one of its choices ({})", value, known));
    }
    const std::string old = props.value;
    if (value != props.value) {
        write_value(value);
        emit_changed(value);
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", old));
}

ActResult Segmented::set_value(ActContext&, const Value& value) { return choose(std::string(value.as_string())); }

ActResult Segmented::next(ActContext&) {
    if (props.options.empty()) return ActResult::done();
    const auto i = index_of(props.value);
    return choose(props.options[i ? std::min(*i + 1, props.options.size() - 1) : 0]);
}

ActResult Segmented::previous(ActContext&) {
    if (props.options.empty()) return ActResult::done();
    const auto i = index_of(props.value);
    return choose(props.options[i && *i > 0 ? *i - 1 : 0]);
}

spec::SemanticNode Segmented::item(std::string_view path, std::size_t i) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, props.options[i]);
    n.role = Role::radio;
    n.name = label_of(i);
    const bool chosen = props.options[i] == props.value;
    n.states.set(State::checked, chosen);
    if (props.disabled) n.states.set(State::disabled, true);
    if (i < at_.size() && attached()) n.bounds = in_window(services().bounds(node()), at_[i]);
    if (!chosen && !props.disabled) n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

void Segmented::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(props.value);
}

std::vector<spec::SemanticNode> Segmented::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (std::size_t i = 0; i < props.options.size(); ++i) out.push_back(item(path, i));
    return out;
}

std::optional<spec::SemanticNode> Segmented::virtual_item(std::string_view path, std::string_view key) const {
    const auto i = index_of(key);
    return i ? std::optional(item(path, *i)) : std::nullopt;
}

ActResult Segmented::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    // Its inverse taken back on the item (S-12) is the group's own action.
    if (action != "activate") return act(ctx, action);
    if (props.disabled) return ActResult::fail("it is disabled");
    return choose(std::string(key));
}

// ── chip ──

void Chip::mounted() {
    label_.set_style("label");
    label_.set_max_lines(1);
    label_.set_text(props.text);
}

void Chip::prop_changed(ChipProp p) {
    if (p == ChipProp::text) label_.set_text(props.text);
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

Size Chip::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const Size t = label_.measure(m.services, m.services.theme().color("text"));
    float w = std::ceil(t.width) + 2 * chip_pad;
    if (props.selectable && props.selected) w += chip_icon + chip_gap;
    if (props.removable) w += chip_icon + chip_gap - 4;
    return {width.value_or(w), height.value_or(chip_h)};
}

Rect Chip::remove_box(Size size) const { return {size.width - chip_pad + 4 - chip_icon, (size.height - chip_icon) / 2, chip_icon, chip_icon}; }

void Chip::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    size_ = ctx.size;
    const bool off = props.disabled || ctx.state.disabled;
    const bool on = props.selectable && props.selected;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(ctx.size.height / 2);
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, box);
    Color fill = on ? th.color("accent_subtle") : th.color("surface");
    if (ctx.state.hovered && !off) fill = mix(fill, th.color("surface_pressed"), on ? 0.25f : 0.5f);
    ctx.list.fill_round_rect(box, r, xrender::Paint::solid(fill));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, xrender::Paint::solid(on ? th.color("accent") : th.color("border")));
    const Color text = on ? th.color("accent") : th.color("text");
    float x = chip_pad;
    if (on) {
        parts::check_mark(ctx, {x + chip_icon / 2, ctx.size.height / 2}, chip_icon - 2, text);
        x += chip_icon + chip_gap;
    }
    const float right = props.removable ? remove_box(ctx.size).x - chip_gap + 2 : ctx.size.width - chip_pad;
    const Size t = label_.measure(ctx.services, text, std::max(0.0f, right - x));
    label_.paint(ctx, {x, (ctx.size.height - t.height) / 2}, text);
    if (props.removable) {
        const Rect rb = remove_box(ctx.size);
        if (hover_remove_) ctx.list.fill_round_rect(rb, CornerRadii::all(chip_icon / 2), xrender::Paint::solid(th.color("surface_pressed")));
        parts::cross(ctx, rb.center(), 7, th.color("text_muted"));
    }
    if (off) ctx.list.pop_layer();
}

void Chip::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible) parts::focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.size.height / 2));
}

bool Chip::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || props.disabled) return false;
    const bool over_remove = props.removable && remove_box(size_).contains(ctx.local);
    if (over_remove != hover_remove_) hover_remove_ = over_remove, ctx.repaint();
    if (p->kind == PointerKind::up && primary_press(*p)) {
        (void)services().perform(node(), over_remove ? "remove" : "activate", {});
        return true;
    }
    return p->kind == PointerKind::down;
}

ActResult Chip::activate(ActContext&) {
    if (props.disabled) return ActResult::fail("it is disabled");
    emit_activated();
    if (!props.selectable) return ActResult::done();
    const bool now = !props.selected;
    write_selected(now);
    emit_toggled(now);
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
    // Pressed again, it is as it was.
    return ActResult::done(Value(now)).undo_by("activate", {});
}

ActResult Chip::remove(ActContext&) {
    if (!props.removable) return ActResult::fail("this chip is not removable");
    emit_removed();
    return ActResult::done();
}

void Chip::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.text;
    if (props.selectable) {
        node.role = Role::toggle_button;
        node.states.set(State::checked, props.selected);
    }
    if (!props.removable) std::erase_if(node.actions, [](const spec::ActionDesc& a) { return a.name == "remove"; });
}

std::vector<spec::SemanticNode> Chip::virtual_items(std::string_view path) const {
    if (!props.removable) return {};
    return {*virtual_item(path, "remove")};
}

std::optional<spec::SemanticNode> Chip::virtual_item(std::string_view path, std::string_view key) const {
    if (!props.removable || key != "remove") return std::nullopt;
    spec::SemanticNode n;
    n.id = std::format("{}/remove", path);
    n.role = Role::button;
    n.name = std::format("Remove {}", props.text);
    if (attached() && size_.width > 0) n.bounds = in_window(services().bounds(node()), remove_box(size_));
    n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

ActResult Chip::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (key != "remove" || action != "activate") return ActResult::fail(std::format("its remove button is activated, not '{}'", action));
    return remove(ctx);
}

// ── range slider ──

void RangeSlider::mounted() {}

void RangeSlider::prop_changed(RangeSliderProp) {
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

double RangeSlider::snap(double v) const noexcept {
    const double lo = std::min(props.min, props.max), hi = std::max(props.min, props.max);
    v = std::clamp(v, lo, hi);
    if (props.step > 0) v = std::clamp(lo + std::round((v - lo) / props.step) * props.step, lo, hi);
    return v;
}

Rect RangeSlider::track(Size size) const noexcept { return {range_thumb / 2, (size.height - range_track) / 2, std::max(0.0f, size.width - range_thumb), range_track}; }

float RangeSlider::x_of(double v, Size size) const noexcept {
    const Rect t = track(size);
    const double span = props.max - props.min;
    return t.x + t.width * static_cast<float>(span == 0 ? 0 : std::clamp((v - props.min) / span, 0.0, 1.0));
}

Size RangeSlider::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    return {width.value_or(aw.is_definite() ? std::min(aw.value, range_w) : range_w), height.value_or(range_h)};
}

void RangeSlider::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    size_ = ctx.size;
    const bool off = props.disabled || ctx.state.disabled;
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});
    const Rect t = track(ctx.size);
    const CornerRadii round = CornerRadii::all(range_track / 2);
    ctx.list.fill_round_rect(t, round, xrender::Paint::solid(th.color("track")));
    const float lo = x_of(props.low, ctx.size), hi = x_of(props.high, ctx.size);
    ctx.list.fill_round_rect({lo, t.y, hi - lo, t.height}, round, xrender::Paint::solid(th.color("accent")));
    for (int i = 0; i < 2; ++i) {
        const Point c{i == 0 ? lo : hi, ctx.size.height / 2};
        if (!off && ((ctx.state.focused && ctx.state.focus_visible && focus_ == i) || dragging_ == i)) {
            const float r = range_thumb / 2 + 6;
            ctx.list.fill_round_rect({c.x - r, c.y - r, 2 * r, 2 * r}, CornerRadii::all(r), xrender::Paint::solid(th.color("accent").with_alpha(0.18f)));
        }
        const Rect thumb{c.x - range_thumb / 2, c.y - range_thumb / 2, range_thumb, range_thumb};
        ctx.list.box_shadow(thumb, CornerRadii::all(range_thumb / 2), {0, 1}, 3, 0, th.color("scrim").with_alpha(0.3f));
        ctx.list.fill_round_rect(thumb, CornerRadii::all(range_thumb / 2), xrender::Paint::solid(th.color("thumb")));
        ctx.list.stroke_round_rect({thumb.x + 1, thumb.y + 1, range_thumb - 2, range_thumb - 2}, CornerRadii::all(range_thumb / 2 - 1), 2,
                                   xrender::Paint::solid(th.color("accent")));
    }
    if (off) ctx.list.pop_layer();
}

bool RangeSlider::move(int thumb, double v, bool commit) {
    v = snap(v);
    double low = props.low, high = props.high;
    if (thumb == 0) low = std::min(v, high);
    else high = std::max(v, low);
    const bool moved = low != props.low || high != props.high;
    if (moved) {
        if (low != props.low) write_low(low);
        if (high != props.high) write_high(high);
        emit_changed(high, low);
    }
    if (commit && moved) emit_committed(high, low);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return moved;
}

bool RangeSlider::on_event(EventContext& ctx, const InputEvent& e) {
    if (props.disabled) return false;
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down && k->key == Key::tab) {
        // Tab goes from the low thumb to the high one, and Shift+Tab back, before it leaves.
        const bool back = k->mods.has(Modifier::shift);
        if ((!back && focus_ == 0) || (back && focus_ == 1)) {
            focus_ = back ? 0 : 1;
            ctx.repaint();
            return true;
        }
        return false;
    }
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    const Rect t = track(size_.width > 0 ? size_ : Size{range_w, range_h});
    auto value_at = [&](float x) { return props.min + (props.max - props.min) * std::clamp((x - t.x) / std::max(1.0f, t.width), 0.0f, 1.0f); };
    switch (p->kind) {
        case PointerKind::down: {
            if (!primary_press(*p)) return false;
            // The nearer thumb (on the same spot, the one the press is past).
            const float lo = x_of(props.low, size_), hi = x_of(props.high, size_);
            const float dl = std::abs(ctx.local.x - lo), dh = std::abs(ctx.local.x - hi);
            dragging_ = dl < dh || (dl == dh && ctx.local.x < lo) ? 0 : 1;
            focus_ = dragging_;
            from_low_ = props.low, from_high_ = props.high;
            services().capture_pointer(node());
            move(dragging_, value_at(ctx.local.x), false);
            return true;
        }
        case PointerKind::move:
            if (dragging_ < 0) return false;
            move(dragging_, value_at(ctx.local.x), false);
            return true;
        case PointerKind::up:
        case PointerKind::cancel:
            if (dragging_ < 0) return false;
            dragging_ = -1;
            services().release_pointer();
            // The drag as one action of the person's, taken back whole (S-12).
            if (props.low != from_low_ || props.high != from_high_)
                services().performed(node(), "set_range", Value{}.set("low", props.low).set("high", props.high),
                                     {{"set_range", Value{}.set("low", from_low_).set("high", from_high_)}});
            emit_committed(props.high, props.low);
            services().invalidate_paint(node());
            return true;
        default:
            return false;
    }
}

ActResult RangeSlider::set_range(ActContext& ctx, const SetRangeArgs& args) {
    if (!args.low.is_number() && !args.high.is_number()) return ActResult::fail("set_range takes low and high, numbers (one may be left out)");
    const double old_low = props.low, old_high = props.high;
    double low = args.low.is_number() ? args.low.as_double() : props.low, high = args.high.is_number() ? args.high.as_double() : props.high;
    if (!ctx.restoring) low = snap(low), high = snap(high);
    if (low > high) return ActResult::fail(std::format("the low end ({}) is past the high one ({})", low, high));
    if (low != props.low) write_low(low);
    if (high != props.high) write_high(high);
    if (low != old_low || high != old_high) emit_changed(high, low), emit_committed(high, low);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return ActResult::done(Value{}.set("low", props.low).set("high", props.high)).undo_by("set_range", Value{}.set("low", old_low).set("high", old_high));
}

ActResult RangeSlider::step(int thumb, double by) {
    const double old_low = props.low, old_high = props.high;
    move(thumb, (thumb == 0 ? props.low : props.high) + by, true);
    return ActResult::done(Value{}.set("low", props.low).set("high", props.high)).undo_by("set_range", Value{}.set("low", old_low).set("high", old_high));
}

ActResult RangeSlider::increment(ActContext&) { return step(focus_, props.step > 0 ? props.step : (props.max - props.min) / 100); }
ActResult RangeSlider::decrement(ActContext&) { return step(focus_, -(props.step > 0 ? props.step : (props.max - props.min) / 100)); }

spec::SemanticNode RangeSlider::item(std::string_view path, int thumb) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, thumb == 0 ? "low" : "high");
    n.role = Role::slider;
    n.name = std::format("{}{}", props.label.empty() ? std::string() : props.label + " ", thumb == 0 ? "from" : "to");
    n.value = Value{}.set("value", thumb == 0 ? props.low : props.high).set("min", thumb == 0 ? props.min : props.low).set("max", thumb == 0 ? props.high : props.max);
    if (attached() && size_.width > 0) {
        const float x = x_of(thumb == 0 ? props.low : props.high, size_);
        n.bounds = in_window(services().bounds(node()), {x - range_thumb / 2, (size_.height - range_thumb) / 2, range_thumb, range_thumb});
    }
    if (!props.disabled)
        for (const char* a : {"increment", "decrement"}) n.actions.push_back({a, Effect::reversible, {}, {}, {}});
    if (!props.disabled) n.actions.push_back({"set_value", Effect::reversible, {}, {}, {{"value", "f64"}}});
    return n;
}

void RangeSlider::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("low", props.low).set("high", props.high).set("min", props.min).set("max", props.max).set("step", props.step);
}

std::vector<spec::SemanticNode> RangeSlider::virtual_items(std::string_view path) const { return {item(path, 0), item(path, 1)}; }

std::optional<spec::SemanticNode> RangeSlider::virtual_item(std::string_view path, std::string_view key) const {
    if (key == "low") return item(path, 0);
    if (key == "high") return item(path, 1);
    return std::nullopt;
}

ActResult RangeSlider::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    const int thumb = key == "low" ? 0 : key == "high" ? 1 : -1;
    if (thumb < 0) return ActResult::fail(std::format("no thumb '{}' (low, high)", key));
    if (props.disabled) return ActResult::fail("it is disabled");
    const double by = props.step > 0 ? props.step : (props.max - props.min) / 100;
    if (action == "increment") return step(thumb, by);
    if (action == "decrement") return step(thumb, -by);
    if (action == "set_value") {
        const Value* v = ctx.args.get("value");
        if (!v || !v->is_number()) return ActResult::fail("set_value takes a number");
        SetRangeArgs a;
        (thumb == 0 ? a.low : a.high) = *v;
        // Kept on its side of the other thumb.
        if (thumb == 0 && v->as_double() > props.high) a.low = Value(props.high);
        if (thumb == 1 && v->as_double() < props.low) a.high = Value(props.low);
        return set_range(ctx, a);
    }
    return act(ctx, action);  // an inverse taken back on the item (S-12): the slider's own set_range
}

}  // namespace omui
