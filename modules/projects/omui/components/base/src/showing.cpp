// openmui.ui.base:showing — chart, carousel.
module omui.ui.base;

import std;
import omui.core;
import omui.view;

namespace omui {

namespace {

constexpr float chart_w = 360, chart_h = 220, axis_gap = 6, x_labels_h = 20, legend_h = 20, chart_pad = 8;  // a chart
constexpr float arrow_d = 32, dot_d = 8, dot_gap = 6;                                                       // a carousel

bool primary(const PointerEvent& p) { return p.type != PointerType::mouse || p.button == MouseButton::left; }

spec::Rect in_window(const Rect& node, const Rect& part) { return {node.x + part.x, node.y + part.y, part.width, part.height}; }

// The colours a chart's series take in turn, where the theme names none.
Color palette(std::size_t index) {
    static constexpr std::array<std::uint32_t, 6> hues{0x6366f1, 0x10b981, 0xf59e0b, 0xef4444, 0x8b5cf6, 0x06b6d4};
    return Color::hex(hues[index % hues.size()]);
}

void text_at(PaintContext& ctx, std::string text, std::string_view style, Color ink, Point where, float align) {
    parts::Label label;
    label.set_style(std::string(style));
    label.set_text(std::move(text));
    const Size size = label.measure(ctx.services, ink);
    label.paint(ctx, {where.x - size.width * align, where.y}, ink);
}

}  // namespace

// ── chart ──

std::vector<Chart::Series> Chart::read(const Value& series) {
    std::vector<Series> out;
    const Value::List* list = series.list();
    if (!list) return out;
    for (const Value& one : *list) {
        Series made;
        if (const Value* name = one.get("name")) made.name = std::string(name->as_string());
        if (const Value* values = one.get("values"); values && values->list())
            for (const Value& value : *values->list())
                if (value.is_number()) made.values.push_back(value.as_double());
        if (const Value* color = one.get("color")) made.color = Color::parse(color->as_string());
        out.push_back(std::move(made));
    }
    return out;
}

double Chart::step_for(double low, double high, int count) {
    const double span = std::max(high - low, 1e-9);
    const double rough = span / std::max(1, count);
    const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
    for (const double nice : {1.0, 2.0, 2.5, 5.0, 10.0})
        if (nice * magnitude >= rough) return nice * magnitude;
    return 10 * magnitude;
}

void Chart::mounted() { series_ = read(props.series); }

void Chart::prop_changed(ChartProp p) {
    if (p == ChartProp::series) series_ = read(props.series);
    pointed_.reset();
    if (!attached()) return;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

Size Chart::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    return {width.value_or(chart_w), height.value_or(chart_h)};
}

Chart::Scale Chart::scale() const {
    double low = std::numeric_limits<double>::infinity(), high = -std::numeric_limits<double>::infinity();
    for (const Series& one : series_)
        for (const double value : one.values) low = std::min(low, value), high = std::max(high, value);
    if (!std::isfinite(low)) low = 0, high = 1;
    if (props.zero || props.kind == ChartKind::bar) low = std::min(low, 0.0), high = std::max(high, 0.0);
    if (high == low) high = low + 1;
    const double step = step_for(low, high);
    return {std::floor(low / step) * step, std::ceil(high / step) * step, step};
}

std::string Chart::number(double value) const {
    std::string out = std::abs(value - std::round(value)) < 1e-9 ? std::format("{}", static_cast<long long>(std::llround(value))) : std::format("{:.3g}", value);
    return props.unit.empty() ? out : std::format("{}{}{}", out, props.unit == "%" ? "" : " ", props.unit);
}

Rect Chart::plot(Size size) const {
    // Room for the scale's longest number on the left, the steps' labels under it, a legend over it.
    const Scale s = scale();
    const std::size_t widest = std::max(number(s.low).size(), number(s.high).size());
    const float left = 7.0f * static_cast<float>(widest) + axis_gap + chart_pad;
    const float top = chart_pad + (series_.size() > 1 ? legend_h : 0.0f);
    return {left, top, std::max(1.0f, size.width - left - chart_pad), std::max(1.0f, size.height - top - x_labels_h - chart_pad)};
}

Point Chart::at(std::size_t series, std::size_t step, Size size) const {
    const Rect p = plot(size);
    const Scale s = scale();
    const std::size_t steps = std::max<std::size_t>(1, props.labels.empty() ? (series_.empty() ? 1 : series_[0].values.size()) : props.labels.size());
    const double value = series < series_.size() && step < series_[series].values.size() ? series_[series].values[step] : 0;
    const float y = p.bottom() - static_cast<float>((value - s.low) / (s.high - s.low)) * p.height;
    if (props.kind == ChartKind::bar) {
        const float group = p.width / static_cast<float>(steps), inner = group * 0.7f;
        const float bar = inner / static_cast<float>(std::max<std::size_t>(1, series_.size()));
        return {p.x + group * static_cast<float>(step) + (group - inner) / 2 + bar * (static_cast<float>(series) + 0.5f), y};
    }
    const float x = steps == 1 ? p.x + p.width / 2 : p.x + p.width * static_cast<float>(step) / static_cast<float>(steps - 1);
    return {x, y};
}

Color Chart::color_of(const Theme& theme, std::size_t series) const {
    if (series < series_.size() && series_[series].color) return *series_[series].color;
    return theme.color(std::format("chart_{}", series + 1), palette(series));
}

void Chart::paint(PaintContext& ctx) const {
    drawn_ = ctx.size;
    const Theme& theme = ctx.theme;
    const Rect p = plot(ctx.size);
    const Scale s = scale();
    const Color muted = theme.color("text_muted"), grid = theme.color("border");
    // The scale: round numbers, a line each.
    for (double value = s.low; value <= s.high + s.step * 1e-6; value += s.step) {
        const float y = p.bottom() - static_cast<float>((value - s.low) / (s.high - s.low)) * p.height;
        ctx.list.fill_rect({p.x, std::round(y), p.width, 1}, value == 0 ? grid : grid.with_alpha(0.5f));
        text_at(ctx, number(value), "caption", muted, {p.x - axis_gap, y - 8}, 1);
    }
    // The steps, as many as fit.
    const std::size_t steps = props.labels.size();
    const std::size_t every = steps == 0 ? 1 : std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(static_cast<float>(steps) * 44 / p.width)));
    for (std::size_t step = 0; step < steps; step += every) {
        const float x = props.kind == ChartKind::bar ? p.x + p.width / static_cast<float>(steps) * (static_cast<float>(step) + 0.5f) : at(0, step, ctx.size).x;
        text_at(ctx, props.labels[step], "caption", muted, {x, p.bottom() + 4}, 0.5f);
    }
    // The series.
    for (std::size_t series = 0; series < series_.size(); ++series) {
        const Color ink = color_of(theme, series);
        const std::size_t count = series_[series].values.size();
        if (count == 0) continue;
        ctx.part({.role = "series", .reference = series_[series].name, .allocation = p}, [&] {
            if (props.kind == ChartKind::bar) {
                const float group = p.width / static_cast<float>(std::max<std::size_t>(1, steps ? steps : count));
                const float bar = group * 0.7f / static_cast<float>(series_.size());
                const float base = p.bottom() - static_cast<float>((0 - s.low) / (s.high - s.low)) * p.height;
                for (std::size_t step = 0; step < count; ++step) {
                    const Point top = at(series, step, ctx.size);
                    const Rect column{top.x - bar / 2 + 1, std::min(top.y, base), std::max(1.0f, bar - 2), std::abs(base - top.y)};
                    ctx.list.fill_round_rect(column, CornerRadii{3, 3, 0, 0}, xrender::Paint::solid(ink));
                }
                return;
            }
            xrender::Path line;
            for (std::size_t step = 0; step < count; ++step) {
                const Point point = at(series, step, ctx.size);
                if (step == 0) line.move_to(point);
                else line.line_to(point);
            }
            if (props.kind == ChartKind::area) {
                xrender::Path area = line;
                area.line_to({at(series, count - 1, ctx.size).x, p.bottom()}).line_to({at(series, 0, ctx.size).x, p.bottom()}).close();
                ctx.list.fill_path(area, xrender::FillRule::nonzero, xrender::Paint::solid(ink.with_alpha(0.16f)));
            }
            ctx.list.stroke_path(line, {.width = 2, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round}, xrender::Paint::solid(ink));
            if (count <= 24)
                for (std::size_t step = 0; step < count; ++step) {
                    xrender::Path dot;
                    dot.add_circle(at(series, step, ctx.size), 2.5f);
                    ctx.list.fill_path(dot, xrender::FillRule::nonzero, xrender::Paint::solid(ink));
                }
        });
    }
    // The legend, when there is more than one.
    if (series_.size() > 1) {
        float x = p.x;
        for (std::size_t series = 0; series < series_.size(); ++series) {
            ctx.list.fill_round_rect({x, chart_pad + 4, 10, 10}, CornerRadii::all(3), xrender::Paint::solid(color_of(theme, series)));
            parts::Label name;
            name.set_style("caption");
            name.set_text(series_[series].name);
            const Size size = name.measure(ctx.services, muted);
            name.paint(ctx, {x + 14, chart_pad + 9 - size.height / 2}, muted);
            x += 14 + size.width + 14;
        }
    }
}

void Chart::paint_over(PaintContext& ctx) const {
    if (!pointed_ || series_.empty()) return;
    const Theme& theme = ctx.theme;
    const Rect p = plot(ctx.size);
    const std::size_t step = *pointed_;
    const float x = props.kind == ChartKind::bar && !props.labels.empty() ? p.x + p.width / static_cast<float>(props.labels.size()) * (static_cast<float>(step) + 0.5f)
                                                                       : at(0, step, ctx.size).x;
    ctx.list.fill_rect({std::round(x), p.y, 1, p.height}, theme.color("text_muted").with_alpha(0.5f));
    // What the step holds: its label, then each series' number.
    std::vector<std::string> lines;
    if (step < props.labels.size()) lines.push_back(props.labels[step]);
    for (const Series& one : series_)
        if (step < one.values.size()) lines.push_back(std::format("{}{}{}", one.name, one.name.empty() ? "" : ": ", number(one.values[step])));
    std::vector<parts::Label> labels(lines.size());
    float wide = 0, tall = 8;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        labels[index].set_style(index == 0 ? "label" : "caption");
        labels[index].set_text(lines[index]);
        const Size size = labels[index].measure(ctx.services, theme.color("text"));
        wide = std::max(wide, size.width);
        tall += size.height + 2;
    }
    Rect card{x + 10, p.y + 4, wide + 16, tall};
    if (card.right() > ctx.size.width) card.x = x - 10 - card.width;
    parts::shadow(ctx, "md", card, CornerRadii::all(8));
    ctx.list.fill_round_rect(card, CornerRadii::all(8), xrender::Paint::solid(theme.color("surface")));
    float y = card.y + 4;
    for (std::size_t index = 0; index < labels.size(); ++index) {
        const Size size = labels[index].measure(ctx.services, theme.color("text"));
        labels[index].paint(ctx, {card.x + 8, y}, index == 0 ? theme.color("text") : theme.color("text_muted"));
        y += size.height + 2;
    }
}

bool Chart::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    std::optional<std::size_t> now;
    const std::size_t steps = props.labels.empty() ? (series_.empty() ? 0 : series_[0].values.size()) : props.labels.size();
    if (p->kind != PointerKind::leave && steps > 0 && drawn_.width > 0) {
        const Rect area = plot(drawn_);
        if (area.contains(ctx.local)) {
            // The nearest step.
            float best = std::numeric_limits<float>::infinity();
            for (std::size_t step = 0; step < steps; ++step) {
                const float x = props.kind == ChartKind::bar ? area.x + area.width / static_cast<float>(steps) * (static_cast<float>(step) + 0.5f) : at(0, step, drawn_).x;
                if (std::abs(x - ctx.local.x) < best) best = std::abs(x - ctx.local.x), now = step;
            }
        }
    }
    if (now != pointed_) pointed_ = now, ctx.repaint();
    return false;
}

spec::SemanticNode Chart::point(std::string_view path, std::size_t series, std::size_t step) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}/{}", path, series, step);
    n.role = Role::cell;
    const std::string step_name = step < props.labels.size() ? props.labels[step] : std::format("{}", step + 1);
    n.name = series_[series].name.empty() ? step_name : std::format("{}, {}", series_[series].name, step_name);
    n.value = Value(series_[series].values[step]);
    if (attached() && drawn_.width > 0) {
        const Point at_point = at(series, step, drawn_);
        n.bounds = in_window(services().bounds(node()), {at_point.x - 4, at_point.y - 4, 8, 8});
    }
    return n;
}

void Chart::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    // What it shows, said: how many series over how many steps, from what to what.
    double low = std::numeric_limits<double>::infinity(), high = -low;
    std::size_t points = 0;
    for (const Series& one : series_)
        for (const double value : one.values) low = std::min(low, value), high = std::max(high, value), ++points;
    const std::size_t steps = props.labels.empty() ? (series_.empty() ? 0 : series_[0].values.size()) : props.labels.size();
    node.description = points == 0 ? std::string("no data")
                                   : std::format("{} {} over {} steps, from {} to {}", series_.size(), series_.size() == 1 ? "series" : "series", steps,
                                                 number(low), number(high));
}

std::vector<spec::SemanticNode> Chart::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (std::size_t series = 0; series < series_.size(); ++series)
        for (std::size_t step = 0; step < series_[series].values.size(); ++step) out.push_back(point(path, series, step));
    return out;
}

std::optional<spec::SemanticNode> Chart::virtual_item(std::string_view path, std::string_view key) const {
    const auto slash = key.find('/');
    std::size_t series = 0, step = 0;
    if (slash == std::string_view::npos || std::from_chars(key.data(), key.data() + slash, series).ec != std::errc{} ||
        std::from_chars(key.data() + slash + 1, key.data() + key.size(), step).ec != std::errc{})
        return std::nullopt;
    if (series >= series_.size() || step >= series_[series].values.size()) return std::nullopt;
    return point(path, series, step);
}

// ── carousel ──

std::size_t Carousel::slides() const { return attached() ? services().children(node()).size() : 0; }

void Carousel::style(const Theme&, mlibs::layout::Style& s) const {
    // Its slides side by side, one in view: each as wide as it (laid_out), the row moved by its offset.
    s.direction = mlibs::layout::Direction::row;
    s.overflow_x = mlibs::layout::Overflow::clip;
    s.overflow_y = mlibs::layout::Overflow::clip;
}

void Carousel::laid_out() {
    const Rect box = services().bounds(node());
    const Insets in = services().content_insets(node());
    const float wide = std::max(0.0f, box.width - in.horizontal());
    if (std::abs(wide - width_) > 0.25f) width_ = wide;
    for (NodeId slide : services().children(node())) {
        if (std::abs(services().get(slide, "width").as_double(-1) - width_) > 0.25)
            services().set(slide, "width", Value(static_cast<double>(width_)));
        if (services().get(slide, "shrink").as_double(1) != 0) services().set(slide, "shrink", Value(0.0));
    }
}

Point Carousel::scroll_offset() const {
    const std::size_t count = slides();
    const auto at = count == 0 ? 0 : std::clamp<std::int64_t>(props.index, 0, static_cast<std::int64_t>(count) - 1);
    return {static_cast<float>(at) * width_ - dragged_, 0};
}

Rect Carousel::arrow(bool forward, Size size) const { return {forward ? size.width - arrow_d - 8 : 8, (size.height - arrow_d) / 2, arrow_d, arrow_d}; }

Rect Carousel::dot(std::size_t index, Size size) const {
    const auto count = static_cast<float>(slides());
    const float row = count * dot_d + (count - 1) * dot_gap;
    return {(size.width - row) / 2 + static_cast<float>(index) * (dot_d + dot_gap), size.height - dot_d - 10, dot_d, dot_d};
}

void Carousel::paint_over(PaintContext& ctx) const {
    const std::size_t count = slides();
    if (count < 2) return;
    const Theme& theme = ctx.theme;
    const auto at = static_cast<std::size_t>(std::clamp<std::int64_t>(props.index, 0, static_cast<std::int64_t>(count) - 1));
    // Its arrows, where there is somewhere to go.
    for (const bool forward : {false, true}) {
        if (!props.wrap && (forward ? at + 1 >= count : at == 0)) continue;
        const Rect r = arrow(forward, ctx.size);
        parts::shadow(ctx, "sm", r, CornerRadii::all(arrow_d / 2));
        ctx.list.fill_round_rect(r, CornerRadii::all(arrow_d / 2), xrender::Paint::solid(theme.color("surface").with_alpha(0.92f)));
        const Point mid{r.x + r.width / 2, r.y + r.height / 2};
        // A chevron pointing the way it goes.
        const float back = forward ? -2.0f : 2.0f, tip = forward ? 3.0f : -3.0f;
        xrender::Path chevron;
        chevron.move_to({mid.x + back, mid.y - 5}).line_to({mid.x + tip, mid.y}).line_to({mid.x + back, mid.y + 5});
        ctx.list.stroke_path(chevron, {.width = 2, .cap = xrender::LineCap::round, .join = xrender::LineJoin::round}, xrender::Paint::solid(theme.color("text")));
    }
    // Its dots: the current one in the accent.
    for (std::size_t index = 0; index < count; ++index) {
        const Rect r = dot(index, ctx.size);
        ctx.list.fill_round_rect(r, CornerRadii::all(dot_d / 2),
                                 xrender::Paint::solid(index == at ? theme.color("accent") : theme.color("text_muted").with_alpha(0.4f)));
    }
    if (ctx.state.focused && ctx.state.focus_visible) parts::focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(theme.corner("medium", 8)));
}

bool Carousel::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || !primary(*p)) return false;
    const Size size = services().bounds(node()).size();
    const auto perform_go = [&](std::int64_t index) { (void)services().perform(node(), "set_index", Value{}.set("index", index)); };
    switch (p->kind) {
        case PointerKind::down:
            pressed_at_ = ctx.local.x;
            swiping_ = false;
            return true;
        case PointerKind::move:
            if (!pressed_at_) return false;
            // A swipe: the slides follow the finger past a little way, within the first and the last.
            if (!swiping_ && std::abs(ctx.local.x - *pressed_at_) > 8) {
                swiping_ = true;
                services().capture_pointer(node());
            }
            if (swiping_) {
                dragged_ = std::clamp(ctx.local.x - *pressed_at_, -width_, width_);
                services().invalidate_layout(node());
            }
            return swiping_;
        case PointerKind::up: {
            const bool was_swipe = swiping_;
            pressed_at_.reset();
            swiping_ = false;
            if (was_swipe) {
                services().release_pointer();
                const float moved = dragged_;
                dragged_ = 0;
                services().invalidate_layout(node());
                if (moved < -width_ / 5) perform_go(props.index + 1);
                else if (moved > width_ / 5) perform_go(props.index - 1);
                return true;
            }
            if (arrow(false, size).contains(ctx.local)) return perform_go(props.index - 1), true;
            if (arrow(true, size).contains(ctx.local)) return perform_go(props.index + 1), true;
            for (std::size_t index = 0; index < slides(); ++index)
                if (dot(index, size).contains(ctx.local)) return perform_go(static_cast<std::int64_t>(index)), true;
            return false;
        }
        default: return false;
    }
}

ActResult Carousel::go(std::int64_t index) {
    const auto count = static_cast<std::int64_t>(slides());
    if (count == 0) return ActResult::fail("it has no slides");
    if (props.wrap) index = ((index % count) + count) % count;
    else if (index < 0 || index >= count) return ActResult::fail(std::format("a slide from 0 to {}", count - 1));
    const std::int64_t old = props.index;
    if (index != props.index) {
        write_index(index);
        emit_changed(index);
        services().invalidate_layout(node());
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(props.index)).undo_by("set_index", Value{}.set("index", old));
}

ActResult Carousel::set_index(ActContext&, const Value& index) { return go(index.as_int()); }

// The arrow keys: as the person's move (journaled, taken back), as a press is.
ActResult Carousel::next(ActContext&) {
    const auto count = static_cast<std::int64_t>(slides());
    if (count > 0 && (props.wrap || props.index + 1 < count)) (void)services().perform(node(), "set_index", Value{}.set("index", props.index + 1));
    return ActResult::done(Value(props.index));
}

ActResult Carousel::previous(ActContext&) {
    if (props.wrap || props.index > 0) (void)services().perform(node(), "set_index", Value{}.set("index", props.index - 1));
    return ActResult::done(Value(props.index));
}

void Carousel::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(props.index);
    node.description = std::format("slide {} of {}", props.index + 1, slides());
}

std::vector<spec::SemanticNode> Carousel::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (std::size_t index = 0; index < slides(); ++index)
        if (auto one = virtual_item(path, std::to_string(index))) out.push_back(std::move(*one));
    return out;
}

std::optional<spec::SemanticNode> Carousel::virtual_item(std::string_view path, std::string_view key) const {
    std::size_t index = 0;
    if (std::from_chars(key.data(), key.data() + key.size(), index).ec != std::errc{} || index >= slides()) return std::nullopt;
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, index);
    n.role = Role::tab;
    n.name = std::format("Slide {} of {}", index + 1, slides());
    const bool here = static_cast<std::int64_t>(index) == props.index;
    n.states.set(State::selected, here);
    if (attached()) n.bounds = in_window(services().bounds(node()), dot(index, services().bounds(node()).size()));
    if (!here) n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

ActResult Carousel::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (action != "activate") return act(ctx, action);
    std::int64_t index = 0;
    (void)std::from_chars(key.data(), key.data() + key.size(), index);
    return go(index);
}

}  // namespace omui
