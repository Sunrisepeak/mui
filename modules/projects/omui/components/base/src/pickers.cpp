module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

using parts::mix;
namespace chr = std::chrono;

namespace {

constexpr float field_h = 36, field_pad = 12;
constexpr float cal_pad = 12, cal_cell_w = 36, cal_cell_h = 34, cal_head = 36, cal_week = 24;
constexpr float panel_pad = 12, panel_w = 216, panel_field_h = 140, panel_hue_h = 12, panel_gap = 10, swatch = 22;
constexpr std::size_t swatches_per_row = 8;

constexpr std::array<std::string_view, 12> month_names{"January", "February", "March",     "April",   "May",      "June",
                                                       "July",    "August",   "September", "October", "November", "December"};
constexpr std::array<std::string_view, 7> weekday_names{"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};  // Monday first

spec::Rect window_rect(const Rect& r) { return {r.x, r.y, r.width, r.height}; }

bool primary(const PointerEvent& p) { return p.type != PointerType::mouse || p.button == MouseButton::left; }

// A field as the select draws it: a surface, its border (the accent while open), a focus ring.
void field(PaintContext& ctx, bool open, bool off) {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    ctx.list.fill_round_rect(box, r, xrender::Paint::solid(mix(th.color("surface"), th.color("surface_variant"), !off && ctx.state.hovered ? 1.0f : 0.0f)));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, xrender::Paint::solid(open ? th.color("accent") : th.color("border")));
}

void popup_surface(PaintContext& ctx) {
    const Theme& th = ctx.theme;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 10)).fitted(ctx.size);
    parts::shadow(ctx, "lg", box, r);
    ctx.list.fill_round_rect(box, r, xrender::Paint::solid(th.color("surface")));
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, xrender::Paint::solid(th.color("border")));
}

// An arrow pointing left or right, centred at c (a calendar's months).
void arrow(PaintContext& ctx, Point c, bool right, Color color) {
    const float s = 5, d = right ? 1.0f : -1.0f;
    xrender::Path p;
    p.move_to({c.x - d * s / 2, c.y - s}).line_to({c.x + d * s / 2, c.y}).line_to({c.x - d * s / 2, c.y + s});
    xrender::StrokeStyle st;
    st.width = 1.75f;
    st.cap = xrender::LineCap::round;
    st.join = xrender::LineJoin::round;
    ctx.list.stroke_path(p, st, xrender::Paint::solid(color));
}

chr::year_month_day shifted(chr::year_month_day d, int days) { return chr::year_month_day{chr::sys_days{d} + chr::days{days}}; }

chr::year_month_day clamped_day(chr::year_month ym, unsigned day) {
    const unsigned last = static_cast<unsigned>((ym / chr::last).day());
    return ym / chr::day{std::min(day, last)};
}

std::optional<chr::year_month> parse_month(std::string_view s) {
    if (auto d = picking::parse_date(std::string(s) + "-01")) return d->year() / d->month();
    return std::nullopt;
}

std::string month_iso(chr::year_month ym) { return std::format("{:04}-{:02}", static_cast<int>(ym.year()), static_cast<unsigned>(ym.month())); }

}  // namespace

// ── dates and colours ──

namespace picking {

std::optional<chr::year_month_day> parse_date(std::string_view s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return std::nullopt;
    int y = 0;
    unsigned m = 0, d = 0;
    auto num = [&](std::size_t from, std::size_t to, auto& out) { return std::from_chars(s.data() + from, s.data() + to, out).ec == std::errc{}; };
    if (!num(0, 4, y) || !num(5, 7, m) || !num(8, 10, d)) return std::nullopt;
    const chr::year_month_day ymd{chr::year{y}, chr::month{m}, chr::day{d}};
    if (!ymd.ok()) return std::nullopt;
    return ymd;
}

std::string iso(chr::year_month_day d) {
    return std::format("{:04}-{:02}-{:02}", static_cast<int>(d.year()), static_cast<unsigned>(d.month()), static_cast<unsigned>(d.day()));
}

std::string spoken(chr::year_month_day d) {
    return std::format("{} {} {}", static_cast<unsigned>(d.day()), month_names[static_cast<unsigned>(d.month()) - 1], static_cast<int>(d.year()));
}

std::optional<Color> parse_hex(std::string_view s) {
    if (s.starts_with('#')) s.remove_prefix(1);
    if (s.size() != 6) return std::nullopt;
    unsigned v = 0;
    if (std::from_chars(s.data(), s.data() + 6, v, 16).ec != std::errc{}) return std::nullopt;
    return Color::hex(v);
}

std::string hex(Color c) {
    auto byte = [](float x) { return static_cast<unsigned>(std::lround(std::clamp(x, 0.0f, 1.0f) * 255)); };
    return std::format("#{:02x}{:02x}{:02x}", byte(c.r), byte(c.g), byte(c.b));
}

Hsv to_hsv(Color c) {
    const double r = c.r, g = c.g, b = c.b;
    const double hi = std::max({r, g, b}), lo = std::min({r, g, b}), d = hi - lo;
    double h = 0;
    if (d > 0) {
        if (hi == r) h = 60 * std::fmod((g - b) / d + 6, 6.0);
        else if (hi == g) h = 60 * ((b - r) / d + 2);
        else h = 60 * ((r - g) / d + 4);
    }
    return {h, hi == 0 ? 0 : d / hi, hi};
}

Color from_hsv(Hsv x) {
    const double h = std::fmod(std::fmod(x.h, 360.0) + 360.0, 360.0) / 60, s = std::clamp(x.s, 0.0, 1.0), v = std::clamp(x.v, 0.0, 1.0);
    const double c = v * s, k = c * (1 - std::abs(std::fmod(h, 2.0) - 1)), m = v - c;
    double r = 0, g = 0, b = 0;
    switch (static_cast<int>(h)) {
        case 0: r = c, g = k; break;
        case 1: r = k, g = c; break;
        case 2: g = c, b = k; break;
        case 3: g = k, b = c; break;
        case 4: r = k, b = c; break;
        default: r = c, b = k; break;
    }
    return {static_cast<float>(r + m), static_cast<float>(g + m), static_cast<float>(b + m), 1};
}

}  // namespace picking

// ── date picker ──

void DatePicker::mounted() {
    label_.set_style("body");
    label_.set_max_lines(1);
    prop_changed(DatePickerProp::value);
}

void DatePicker::prop_changed(DatePickerProp) {
    const auto d = picking::parse_date(props.value);
    label_.set_text(d ? picking::spoken(*d) : props.placeholder);
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::vector<Element> DatePicker::compose() {
    Element cal;
    cal.type = &component_type<DateCalendar>();
    cal.id = "calendar";
    return {std::move(cal)};
}

NodeId DatePicker::calendar() const {
    for (NodeId c : services().children(node()))
        if (parts::segment(services(), c) == "calendar") return c;
    return {};
}

bool DatePicker::expanded() const {
    const NodeId c = attached() ? calendar() : NodeId{};
    return c && services().get(c, "open").as_bool();
}

bool DatePicker::allowed(chr::year_month_day d) const {
    if (const auto lo = picking::parse_date(props.min); lo && chr::sys_days{d} < chr::sys_days{*lo}) return false;
    if (const auto hi = picking::parse_date(props.max); hi && chr::sys_days{d} > chr::sys_days{*hi}) return false;
    return true;
}

void DatePicker::open(bool on) {
    const NodeId c = calendar();
    if (!c) return;
    if (on) {
        // The chosen date's month; else the earliest or latest allowed; else this one.
        chr::year_month_day at{};
        if (auto v = picking::parse_date(props.value)) at = *v;
        else if (auto lo = picking::parse_date(props.min)) at = *lo;
        else if (auto hi = picking::parse_date(props.max)) at = *hi;
        else at = chr::year_month_day{chr::floor<chr::days>(chr::system_clock::now())};
        services().set(c, "focus", Value(picking::iso(at)));
    }
    services().set(c, "open", Value(on));
    services().focus(on ? c : node());
    services().invalidate_semantics(node());
    services().invalidate_paint(node());
}

ActResult DatePicker::choose(std::string iso, bool restoring) {
    const std::string old = props.value;
    if (!iso.empty()) {
        const auto d = picking::parse_date(iso);
        if (!d) return ActResult::fail(std::format("`{}` is not a date (YYYY-MM-DD)", iso));
        if (!restoring && !allowed(*d))
            return ActResult::fail(std::format("{} is outside {}–{}", iso, props.min.empty() ? "…" : props.min, props.max.empty() ? "…" : props.max));
        iso = picking::iso(*d);
    }
    if (iso != props.value) {
        write_value(iso);
        prop_changed(DatePickerProp::value);
        emit_changed(iso);
    }
    if (expanded()) open(false);
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", old));
}

ActResult DatePicker::set_value(ActContext& ctx, const Value& value) { return choose(std::string(value.as_string()), ctx.restoring); }

ActResult DatePicker::expand(ActContext&) {
    if (props.disabled) return ActResult::fail("it is disabled");
    open(true);
    return ActResult::done();
}

ActResult DatePicker::collapse(ActContext&) {
    open(false);
    return ActResult::done();
}

void DatePicker::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.padding = {0, theme.scaled(field_pad) + 24, 0, theme.scaled(field_pad)};
    s.min_height = mlibs::layout::pt(theme.scaled(field_h));
    s.border = Insets::all(1);
}

Size DatePicker::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    parts::Label probe;
    probe.set_style("body");
    probe.set_text("30 September 2026");
    const Size longest = probe.measure(m.services, m.services.theme().color("text"));
    const Size t = label_.measure(m.services, m.services.theme().color("text"));
    return {width.value_or(std::ceil(std::max(longest.width, t.width))), height.value_or(std::ceil(t.height))};
}

void DatePicker::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});
    field(ctx, expanded(), off);
    const Color c = props.value.empty() ? th.color("text_muted") : th.color("text");
    const Size t = label_.measure(ctx.services, c, ctx.content.width);
    label_.paint(ctx, {ctx.content.x, ctx.content.y + (ctx.content.height - t.height) / 2}, c);
    // A small calendar: its page, its two rings.
    const Rect cal{ctx.size.width - th.scaled(field_pad) - 14, ctx.size.height / 2 - 6, 14, 13};
    ctx.list.stroke_round_rect(cal, CornerRadii::all(2.5f), 1.25f, xrender::Paint::solid(th.color("text_muted")));
    ctx.list.fill_rect({cal.x, cal.y + 3.5f, cal.width, 1.25f}, th.color("text_muted"));
    ctx.list.fill_rect({cal.x + 3.5f, cal.y - 2, 1.25f, 3}, th.color("text_muted"));
    ctx.list.fill_rect({cal.right() - 4.75f, cal.y - 2, 1.25f, 3}, th.color("text_muted"));
    if (off) ctx.list.pop_layer();
}

void DatePicker::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible)
        parts::focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.theme.corner("medium", 8)).fitted(ctx.size));
}

bool DatePicker::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || props.disabled || p->kind != PointerKind::up || !primary(*p)) return false;
    const Rect b = services().bounds(node());
    if (ctx.local.x < 0 || ctx.local.y < 0 || ctx.local.x >= b.width || ctx.local.y >= b.height) return false;  // its calendar's presses bubble through it
    open(!expanded());
    return true;
}

void DatePicker::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("value", props.value).set("min", props.min).set("max", props.max);
    node.states.set(State::expanded, expanded());
    node.states.set(State::collapsible);
}

spec::SemanticNode DatePicker::day_item(std::string_view path, chr::year_month_day d) const {
    spec::SemanticNode n;
    const std::string key = picking::iso(d);
    n.id = std::format("{}/{}", path, key);
    n.role = Role::button;
    n.name = picking::spoken(d);
    n.value = Value(key);
    n.states.set(State::selected, key == props.value);
    const bool ok = allowed(d) && !props.disabled;
    n.states.set(State::disabled, !ok);
    if (const NodeId c = expanded() ? calendar() : NodeId{}) {
        const Value at = services().get(c, std::format("at:{}", key));
        if (const Value::List* l = at.list(); l && l->size() == 4)
            n.bounds = spec::Rect{static_cast<float>((*l)[0].as_double()), static_cast<float>((*l)[1].as_double()), static_cast<float>((*l)[2].as_double()), static_cast<float>((*l)[3].as_double())};
    }
    if (ok && key != props.value) n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

std::vector<spec::SemanticNode> DatePicker::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    const NodeId c = expanded() ? calendar() : NodeId{};
    if (!c) return out;
    // While open: the month shown, a day each, and the months either side.
    const auto ym = parse_month(services().get(c, "month").as_string());
    if (!ym) return out;
    for (const char* turn : {"previous_month", "next_month"}) out.push_back(*virtual_item(path, turn));
    const unsigned last = static_cast<unsigned>((*ym / chr::last).day());
    for (unsigned d = 1; d <= last; ++d) out.push_back(day_item(path, *ym / chr::day{d}));
    return out;
}

std::optional<spec::SemanticNode> DatePicker::virtual_item(std::string_view path, std::string_view key) const {
    if (key == "previous_month" || key == "next_month") {
        const NodeId c = expanded() ? calendar() : NodeId{};
        if (!c) return std::nullopt;
        spec::SemanticNode n;
        n.id = std::format("{}/{}", path, key);
        n.role = Role::button;
        n.name = key == "previous_month" ? "Previous month" : "Next month";
        const Value at = services().get(c, key == "previous_month" ? "at:previous" : "at:next");
        if (const Value::List* l = at.list(); l && l->size() == 4)
            n.bounds = spec::Rect{static_cast<float>((*l)[0].as_double()), static_cast<float>((*l)[1].as_double()), static_cast<float>((*l)[2].as_double()), static_cast<float>((*l)[3].as_double())};
        n.actions.push_back({"activate", Effect::read, {}, {}, {}});
        return n;
    }
    // Any date: an agent chooses one without opening the calendar.
    if (const auto d = picking::parse_date(key)) return day_item(path, *d);
    return std::nullopt;
}

ActResult DatePicker::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (key == "previous_month" || key == "next_month") {
        const NodeId c = calendar();
        if (!c || !expanded()) return ActResult::fail("the calendar is closed: expand it first");
        const auto ym = parse_month(services().get(c, "month").as_string());
        if (ym) services().set(c, "month", Value(month_iso(key == "next_month" ? *ym + chr::months{1} : *ym - chr::months{1})));
        services().invalidate_semantics(node());
        return ActResult::done();
    }
    // A day's inverse taken back on the item (S-12) is the picker's own set_value.
    if (action != "activate") return act(ctx, action);
    if (props.disabled) return ActResult::fail("it is disabled");
    return choose(std::string(key));
}

// ── its calendar ──

Value DateCalendar::picker(std::string_view prop) const { return services_->get(services_->parent(node_), prop); }

bool DateCalendar::set_prop(std::string_view k, const Value& v) {
    if (k == "open") {
        open_ = v.as_bool();
        if (services_) services_->restyle(node_);
        return true;
    }
    if (k == "month") {
        if (auto ym = parse_month(v.as_string())) month_ = *ym;
        if (services_) services_->invalidate_paint(node_), services_->invalidate_semantics(node_);
        return true;
    }
    if (k == "focus") {
        if (auto d = picking::parse_date(v.as_string())) focus_ = *d, month_ = d->year() / d->month();
        if (services_) services_->invalidate_paint(node_);
        return true;
    }
    return false;
}

Value DateCalendar::get_prop(std::string_view k) const {
    if (k == "open") return Value(open_);
    if (k == "month") return Value(month_iso(month_));
    // Where a day (or a month's arrow) is in the window, for the picker's items.
    if (k.starts_with("at:") && services_) {
        const Rect me = services_->bounds(node_);
        auto rect = [&](const Rect& r) {
            return Value(Value::List{Value(static_cast<double>(me.x + r.x)), Value(static_cast<double>(me.y + r.y)), Value(static_cast<double>(r.width)),
                                     Value(static_cast<double>(r.height))});
        };
        const std::string_view what = k.substr(3);
        if (what == "previous") return rect(prev_at_);
        if (what == "next") return rect(next_at_);
        if (const auto d = picking::parse_date(what)) {
            const auto from = chr::sys_days{first_cell()};
            const auto i = (chr::sys_days{*d} - from).count();
            if (i >= 0 && i < 42) return rect(cells_[static_cast<std::size_t>(i)]);
        }
    }
    return {};
}

void DateCalendar::style(const Theme&, mlibs::layout::Style& s) const { s.display = open_ ? mlibs::layout::Display::flex : mlibs::layout::Display::none; }

Size DateCalendar::measure(MeasureContext&, std::optional<float>, std::optional<float>, mlibs::layout::Available, mlibs::layout::Available) {
    return {2 * cal_pad + 7 * cal_cell_w, 2 * cal_pad + cal_head + cal_week + 6 * cal_cell_h};
}

chr::year_month_day DateCalendar::first_cell() const {
    const chr::year_month_day first = month_ / chr::day{1};
    const bool sunday = picker("week_start").as_string() == "sunday";
    const unsigned wd = chr::weekday{chr::sys_days{first}}.iso_encoding();  // Monday 1 … Sunday 7
    const int back = sunday ? static_cast<int>(wd % 7) : static_cast<int>(wd - 1);
    return shifted(first, -back);
}

void DateCalendar::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    popup_surface(ctx);
    // The month and its arrows.
    title_.set_style("label");
    title_.set_text(std::format("{} {}", month_names[static_cast<unsigned>(month_.month()) - 1], static_cast<int>(month_.year())));
    const Size t = title_.measure(ctx.services, th.color("text"));
    title_.paint(ctx, {cal_pad + 8, cal_pad + (cal_head - t.height) / 2}, th.color("text"));
    next_at_ = {ctx.size.width - cal_pad - 28, cal_pad + 4, 28, 28};
    prev_at_ = {next_at_.x - 30, cal_pad + 4, 28, 28};
    for (int i = 0; i < 2; ++i) {
        const Rect& r = i ? next_at_ : prev_at_;
        if (hover_ == 100 + i) ctx.list.fill_round_rect(r, CornerRadii::all(6), xrender::Paint::solid(th.color("surface_variant")));
        arrow(ctx, r.center(), i == 1, th.color("text_muted"));
    }
    // The days of the week, from the picker's first.
    const bool sunday = picker("week_start").as_string() == "sunday";
    for (std::size_t i = 0; i < 7; ++i) {
        parts::Label& w = weekdays_[i];
        w.set_style("caption");
        w.set_text(std::string(weekday_names[sunday ? (i + 6) % 7 : i]));
        const Size s = w.measure(ctx.services, th.color("text_muted"));
        w.paint(ctx, {cal_pad + static_cast<float>(i) * cal_cell_w + (cal_cell_w - s.width) / 2, cal_pad + cal_head + (cal_week - s.height) / 2},
                th.color("text_muted"));
    }
    // Six weeks, the month's days among them.
    const std::string chosen(picker("value").as_string());
    const chr::year_month_day from = first_cell();
    const NodeId pick = services_->parent(node_);
    const auto lo = picking::parse_date(services_->get(pick, "min").as_string()), hi = picking::parse_date(services_->get(pick, "max").as_string());
    for (std::size_t i = 0; i < 42; ++i) {
        const chr::year_month_day d = shifted(from, static_cast<int>(i));
        const Rect r{cal_pad + static_cast<float>(i % 7) * cal_cell_w, cal_pad + cal_head + cal_week + static_cast<float>(i / 7) * cal_cell_h, cal_cell_w,
                     cal_cell_h};
        cells_[i] = r;
        const bool here = d.month() == month_.month();
        const bool ok = (!lo || chr::sys_days{d} >= chr::sys_days{*lo}) && (!hi || chr::sys_days{d} <= chr::sys_days{*hi});
        const bool is_chosen = picking::iso(d) == chosen;
        const float disc = 30;
        const Rect dot{r.center().x - disc / 2, r.center().y - disc / 2, disc, disc};
        if (is_chosen) ctx.list.fill_round_rect(dot, CornerRadii::all(disc / 2), xrender::Paint::solid(th.color("accent")));
        else if (hover_ == static_cast<int>(i) && ok) ctx.list.fill_round_rect(dot, CornerRadii::all(disc / 2), xrender::Paint::solid(th.color("surface_variant")));
        const Color c = is_chosen ? th.color("on_accent") : !ok ? th.color("text_disabled") : here ? th.color("text") : th.color("text_muted");
        parts::Label& l = days_[i];
        l.set_style("label");
        l.set_text(std::to_string(static_cast<unsigned>(d.day())));
        const Size s = l.measure(ctx.services, c);
        l.paint(ctx, {r.center().x - s.width / 2, r.center().y - s.height / 2}, c);
    }
}

void DateCalendar::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible)) return;
    const auto i = (chr::sys_days{focus_} - chr::sys_days{first_cell()}).count();
    if (i < 0 || i >= 42) return;
    const Rect r = cells_[static_cast<std::size_t>(i)];
    parts::focus_ring(ctx, {r.center().x - 15, r.center().y - 15, 30, 30}, CornerRadii::all(15));
}

void DateCalendar::turn(int months) {
    month_ = month_ + chr::months{months};
    focus_ = clamped_day(month_, static_cast<unsigned>(focus_.day()));
    services_->invalidate_paint(node_);
    services_->invalidate_semantics(services_->parent(node_));
}

void DateCalendar::close() {
    services_->set(node_, "open", Value(false));
    services_->focus(services_->parent(node_));
    services_->invalidate_semantics(services_->parent(node_));
}

void DateCalendar::pick(chr::year_month_day d) {
    // The picker's set_value, as the person: journaled, taken back.
    (void)services_->perform(services_->parent(node_), "set_value", Value{}.set("value", picking::iso(d)));
}

bool DateCalendar::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down) {
        auto move = [&](int days) {
            focus_ = shifted(focus_, days);
            month_ = focus_.year() / focus_.month();
            ctx.repaint();
            services_->invalidate_semantics(services_->parent(node_));
        };
        switch (k->key) {
            case Key::left: move(-1); return true;
            case Key::right: move(1); return true;
            case Key::up: move(-7); return true;
            case Key::down: move(7); return true;
            case Key::page_up: turn(-1); return true;
            case Key::page_down: turn(1); return true;
            case Key::enter:
            case Key::space: pick(focus_); return true;
            case Key::escape: close(); return true;
            default: return false;
        }
    }
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    int over = -1;
    for (std::size_t i = 0; i < 42; ++i)
        if (cells_[i].contains(ctx.local)) over = static_cast<int>(i);
    if (prev_at_.contains(ctx.local)) over = 100;
    if (next_at_.contains(ctx.local)) over = 101;
    if (p->kind == PointerKind::leave) over = -1;
    if (over != hover_) hover_ = over, ctx.repaint();
    if (p->kind != PointerKind::up || !primary(*p)) return p->kind == PointerKind::down;
    if (over == 100 || over == 101) turn(over == 100 ? -1 : 1);
    else if (over >= 0) pick(shifted(first_cell(), over));
    return true;
}

void DateCalendar::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.name = std::format("{} {}", month_names[static_cast<unsigned>(month_.month()) - 1], static_cast<int>(month_.year()));
    node.value = Value(month_iso(month_));
    node.actions.push_back({"dismiss", Effect::reversible, {}, {}, {}});
}

ActResult DateCalendar::act(ActContext&, std::string_view action) {
    if (action != "dismiss") return ActResult::fail(std::format("the calendar has no action '{}' (its days are the picker's items)", action));
    close();
    return ActResult::done();
}

// ── time picker ──

std::optional<int> TimePicker::minutes_of(std::string_view s) noexcept {
    const auto colon = s.find(':');
    if (colon == std::string_view::npos) return std::nullopt;
    int h = 0, m = 0;
    if (std::from_chars(s.data(), s.data() + colon, h).ec != std::errc{} || std::from_chars(s.data() + colon + 1, s.data() + s.size(), m).ec != std::errc{})
        return std::nullopt;
    if (h < 0 || h > 23 || m < 0 || m > 59 || colon == 0 || s.size() - colon - 1 != 2) return std::nullopt;
    return h * 60 + m;
}

int TimePicker::now() const noexcept { return minutes_of(props.value).value_or(0); }

void TimePicker::mounted() {
    for (auto& t : texts_) t.set_style("body");
}

void TimePicker::prop_changed(TimePickerProp) {
    if (!attached()) return;
    focus_ = std::min(focus_, parts_count() - 1);
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::string TimePicker::shown(int part) const {
    const int m = now(), h = m / 60;
    if (part == 0) return std::format("{:02}", props.hour12 ? (h % 12 == 0 ? 12 : h % 12) : h);
    if (part == 1) return std::format("{:02}", m % 60);
    return h < 12 ? "AM" : "PM";
}

void TimePicker::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.padding = {0, theme.scaled(field_pad), 0, theme.scaled(field_pad)};
    s.min_height = mlibs::layout::pt(theme.scaled(field_h));
    s.border = Insets::all(1);
}

Size TimePicker::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    parts::Label probe;
    probe.set_style("body");
    probe.set_text(props.hour12 ? "00 : 00  PM" : "00 : 00");
    const Size t = probe.measure(m.services, m.services.theme().color("text"));
    return {width.value_or(std::ceil(t.width) + 16), height.value_or(std::ceil(t.height))};
}

void TimePicker::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});
    field(ctx, ctx.state.focused, off);
    float x = ctx.content.x;
    const Color c = th.color("text");
    for (int part = 0; part < parts_count(); ++part) {
        if (part == 1 || part == 2) {
            parts::Label& sep = texts_[3];
            sep.set_text(part == 1 ? ":" : " ");
            const Size s = sep.measure(ctx.services, th.color("text_muted"));
            sep.paint(ctx, {x + 2, ctx.content.y + (ctx.content.height - s.height) / 2}, th.color("text_muted"));
            x += s.width + 4;
        }
        parts::Label& l = texts_[static_cast<std::size_t>(part)];
        l.set_text(shown(part));
        const Size s = l.measure(ctx.services, c);
        const Rect r{x - 3, ctx.content.y + (ctx.content.height - s.height) / 2 - 2, s.width + 6, s.height + 4};
        at_[static_cast<std::size_t>(part)] = r;
        // The part the keys move, lit while it has the focus.
        if (ctx.state.focused && focus_ == part) ctx.list.fill_round_rect(r, CornerRadii::all(4), xrender::Paint::solid(th.color("accent_subtle")));
        l.paint(ctx, {x, ctx.content.y + (ctx.content.height - s.height) / 2}, ctx.state.focused && focus_ == part ? th.color("accent") : c);
        x += s.width;
    }
    if (off) ctx.list.pop_layer();
}

bool TimePicker::on_event(EventContext& ctx, const InputEvent& e) {
    if (props.disabled) return false;
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down) {
        const int key = static_cast<int>(k->key), d0 = static_cast<int>(Key::digit0);
        if (key >= d0 && key <= d0 + 9 && focus_ < 2) {
            // Typed: two digits a part (one, when no second could follow), then on to the next.
            typed_ += static_cast<char>('0' + key - d0);
            const int n = std::stoi(typed_);
            const bool hours = focus_ == 0;
            const int top = hours ? (props.hour12 ? 12 : 23) : 59;
            const bool done = typed_.size() == 2 || n * 10 > top;
            if (done || typed_.size() == 1) {
                const int m = now();
                int h = m / 60, mm = m % 60;
                if (hours) {
                    int v = std::clamp(n, props.hour12 ? 1 : 0, top);
                    if (props.hour12) v = (v % 12) + (h >= 12 ? 12 : 0);
                    h = v;
                } else {
                    mm = std::clamp(n, 0, 59);
                }
                (void)services().perform(node(), "set_value", Value{}.set("value", std::format("{:02}:{:02}", h, mm)));
            }
            if (done) {
                typed_.clear();
                if (focus_ + 1 < parts_count()) ++focus_;
            }
            ctx.repaint();
            return true;
        }
        if (k->key == Key::tab) typed_.clear();
        return false;
    }
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || p->kind != PointerKind::down || !primary(*p)) return false;
    for (int part = 0; part < parts_count(); ++part)
        if (at_[static_cast<std::size_t>(part)].contains(ctx.local)) focus_ = part;
    typed_.clear();
    services().focus(node());
    ctx.repaint();
    return true;
}

ActResult TimePicker::to(int minutes, bool restoring) {
    const std::string old = props.value;
    minutes = ((minutes % 1440) + 1440) % 1440;
    if (!restoring && props.step > 1) minutes = static_cast<int>(std::lround(static_cast<double>(minutes) / static_cast<double>(props.step)) * props.step) % 1440;
    const std::string v = std::format("{:02}:{:02}", minutes / 60, minutes % 60);
    if (v != props.value) {
        write_value(v);
        emit_changed(v);
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", old));
}

ActResult TimePicker::set_value(ActContext& ctx, const Value& value) {
    const auto m = minutes_of(value.as_string());
    if (!m) return ActResult::fail(std::format("`{}` is not a time (HH:MM, 24-hour)", value.as_string()));
    return to(*m, ctx.restoring);
}

ActResult TimePicker::step(int part, int by) {
    const int m = now(), h = m / 60, mm = m % 60;
    if (part == 0) return to(((h + by) % 24 + 24) % 24 * 60 + mm);
    if (part == 1) {
        const int s = static_cast<int>(std::max<std::int64_t>(1, props.step));
        return to(h * 60 + ((mm + by * s) % 60 + 60) % 60);
    }
    return to(m + 12 * 60);  // AM and PM, either way
}

ActResult TimePicker::increment(ActContext&) { return step(focus_, 1); }
ActResult TimePicker::decrement(ActContext&) { return step(focus_, -1); }

ActResult TimePicker::previous(ActContext&) {
    focus_ = std::max(0, focus_ - 1);
    typed_.clear();
    services().invalidate_paint(node());
    return ActResult::done();
}

ActResult TimePicker::next(ActContext&) {
    focus_ = std::min(parts_count() - 1, focus_ + 1);
    typed_.clear();
    services().invalidate_paint(node());
    return ActResult::done();
}

spec::SemanticNode TimePicker::item(std::string_view path, int part) const {
    static constexpr std::array<std::string_view, 3> keys{"hours", "minutes", "period"};
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, keys[static_cast<std::size_t>(part)]);
    const int m = now();
    if (part < 2) {
        n.role = Role::spin_button;
        n.name = part == 0 ? "Hours" : "Minutes";
        const std::int64_t v = part == 0 ? (props.hour12 ? (m / 60 % 12 == 0 ? 12 : m / 60 % 12) : m / 60) : m % 60;
        n.value = Value{}.set("value", v).set("min", std::int64_t{part == 0 && props.hour12 ? 1 : 0}).set("max", std::int64_t{part == 0 ? (props.hour12 ? 12 : 23) : 59});
        if (!props.disabled) {
            for (const char* a : {"increment", "decrement"}) n.actions.push_back({a, Effect::reversible, {}, {}, {}});
            // A whole number; the picker's own HH:MM taken back here too (S-12): any.
            n.actions.push_back({"set_value", Effect::reversible, {}, {}, {{"value", "any"}}});
        }
    } else {
        n.role = Role::button;
        n.name = shown(2);
        if (!props.disabled) n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    }
    if (attached() && at_[static_cast<std::size_t>(part)].width > 0) {
        const Rect b = services().bounds(node()), r = at_[static_cast<std::size_t>(part)];
        n.bounds = spec::Rect{b.x + r.x, b.y + r.y, r.width, r.height};
    }
    return n;
}

void TimePicker::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(props.value);
}

std::vector<spec::SemanticNode> TimePicker::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (int part = 0; part < parts_count(); ++part) out.push_back(item(path, part));
    return out;
}

std::optional<spec::SemanticNode> TimePicker::virtual_item(std::string_view path, std::string_view key) const {
    if (key == "hours") return item(path, 0);
    if (key == "minutes") return item(path, 1);
    if (key == "period" && props.hour12) return item(path, 2);
    return std::nullopt;
}

ActResult TimePicker::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    const int part = key == "hours" ? 0 : key == "minutes" ? 1 : key == "period" && props.hour12 ? 2 : -1;
    if (part < 0) return ActResult::fail(std::format("no part '{}' (hours, minutes{})", key, props.hour12 ? ", period" : ""));
    if (props.disabled) return ActResult::fail("it is disabled");
    if (part == 2 && action == "activate") return step(2, 1);
    if (part < 2 && action == "increment") return step(part, 1);
    if (part < 2 && action == "decrement") return step(part, -1);
    if (const Value* given = ctx.args.get("value"); part < 2 && action == "set_value" && given && given->is_number()) {
        const Value* v = given;
        const int x = static_cast<int>(v->as_int()), m = now();
        if (part == 0) {
            const int lo = props.hour12 ? 1 : 0, hi = props.hour12 ? 12 : 23;
            if (x < lo || x > hi) return ActResult::fail(std::format("hours are {}–{}", lo, hi));
            const int h = props.hour12 ? x % 12 + (m / 60 >= 12 ? 12 : 0) : x;
            return to(h * 60 + m % 60);
        }
        if (x < 0 || x > 59) return ActResult::fail("minutes are 0–59");
        return to(m / 60 * 60 + x);
    }
    return act(ctx, action);  // an inverse taken back on the item (S-12): the picker's own set_value
}

// ── colour picker ──

void ColorPicker::mounted() {
    label_.set_style("body");
    if (auto c = picking::parse_hex(props.value)) hue_ = picking::to_hsv(*c).h;
    label_.set_text(props.value);
}

void ColorPicker::prop_changed(ColorPickerProp p) {
    if (p == ColorPickerProp::value) {
        // A grey keeps the hue it had: its own is none.
        if (auto c = picking::parse_hex(props.value); c && picking::to_hsv(*c).s > 0) hue_ = picking::to_hsv(*c).h;
        label_.set_text(props.value);
    }
    if (!attached()) return;
    if (p == ColorPickerProp::swatches) services().invalidate_layout(panel());
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

std::vector<Element> ColorPicker::compose() {
    Element pane;
    pane.type = &component_type<ColorPanel>();
    pane.id = "picker";
    pane.on("pick_start", [this](const Value&) { drag_from_ = props.value; });
    pane.on("picking", [this](const Value& v) { picking(std::string(v.get("value") ? v.get("value")->as_string() : ""), v.get("hue") ? v.get("hue")->as_double() : hue_); });
    pane.on("picked", [this](const Value&) {
        // The drag as one action of the person's (S-12).
        if (props.value != drag_from_)
            services().performed(node(), "set_value", Value{}.set("value", props.value), {{"set_value", Value{}.set("value", drag_from_)}});
    });
    return {std::move(pane)};
}

NodeId ColorPicker::panel() const {
    for (NodeId c : services().children(node()))
        if (parts::segment(services(), c) == "picker") return c;
    return {};
}

bool ColorPicker::expanded() const {
    const NodeId p = attached() ? panel() : NodeId{};
    return p && services().get(p, "open").as_bool();
}

void ColorPicker::open(bool on) {
    const NodeId p = panel();
    if (!p) return;
    services().set(p, "open", Value(on));
    services().focus(on ? p : node());
    services().invalidate_semantics(node());
    services().invalidate_paint(node());
}

void ColorPicker::picking(const std::string& hex, double hue) {
    if (!picking::parse_hex(hex)) return;
    hue_ = hue;
    if (hex != props.value) {
        write_value(hex);
        label_.set_text(hex);
        emit_changed(hex);
    }
    services().invalidate_paint(node());
    services().invalidate_paint(panel());
    services().invalidate_semantics(node());
}

ActResult ColorPicker::take(std::string hex, std::optional<double> hue) {
    const auto c = picking::parse_hex(hex);
    if (!c) return ActResult::fail(std::format("`{}` is not a colour (#rrggbb)", hex));
    const std::string old = props.value;
    hex = picking::hex(*c);
    if (hue) hue_ = *hue;
    else if (picking::to_hsv(*c).s > 0) hue_ = picking::to_hsv(*c).h;
    if (hex != props.value) {
        write_value(hex);
        label_.set_text(hex);
        emit_changed(hex);
    }
    if (attached()) {
        services().invalidate_paint(node());
        if (const NodeId p = panel()) services().invalidate_paint(p);
        services().invalidate_semantics(node());
    }
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", old));
}

ActResult ColorPicker::set_value(ActContext&, const Value& value) { return take(std::string(value.as_string())); }

ActResult ColorPicker::expand(ActContext&) {
    if (props.disabled) return ActResult::fail("it is disabled");
    open(true);
    return ActResult::done();
}

ActResult ColorPicker::collapse(ActContext&) {
    open(false);
    return ActResult::done();
}

void ColorPicker::style(const Theme& theme, mlibs::layout::Style& s) const {
    s.padding = {0, theme.scaled(field_pad) + 20, 0, theme.scaled(field_pad) + 28};
    s.min_height = mlibs::layout::pt(theme.scaled(field_h));
    s.border = Insets::all(1);
}

Size ColorPicker::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    parts::Label probe;
    probe.set_style("body");
    probe.set_text("#888888");
    const Size t = probe.measure(m.services, m.services.theme().color("text"));
    return {width.value_or(std::ceil(t.width)), height.value_or(std::ceil(t.height))};
}

void ColorPicker::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const bool off = props.disabled || ctx.state.disabled;
    if (off) ctx.list.push_layer(0.5f, xrender::BlendMode::normal, {}, {}, {0, 0, ctx.size.width, ctx.size.height});
    field(ctx, expanded(), off);
    const Rect sw{th.scaled(field_pad), (ctx.size.height - 20) / 2, 20, 20};
    ctx.list.fill_round_rect(sw, CornerRadii::all(5), xrender::Paint::solid(picking::parse_hex(props.value).value_or(Color{})));
    ctx.list.stroke_round_rect({sw.x + 0.5f, sw.y + 0.5f, sw.width - 1, sw.height - 1}, CornerRadii::all(5), 1, xrender::Paint::solid(th.color("border_strong")));
    const Size t = label_.measure(ctx.services, th.color("text"));
    label_.paint(ctx, {ctx.content.x, ctx.content.y + (ctx.content.height - t.height) / 2}, th.color("text"));
    parts::chevron(ctx, {ctx.size.width - th.scaled(field_pad) - 6, ctx.size.height / 2}, 10, th.color("text_muted"));
    if (off) ctx.list.pop_layer();
}

void ColorPicker::paint_over(PaintContext& ctx) const {
    if (ctx.state.focused && ctx.state.focus_visible)
        parts::focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(ctx.theme.corner("medium", 8)).fitted(ctx.size));
}

bool ColorPicker::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p || props.disabled || p->kind != PointerKind::up || !primary(*p)) return false;
    const Rect b = services().bounds(node());
    if (ctx.local.x < 0 || ctx.local.y < 0 || ctx.local.x >= b.width || ctx.local.y >= b.height) return false;
    open(!expanded());
    return true;
}

void ColorPicker::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value(props.value);
    node.states.set(State::expanded, expanded());
    node.states.set(State::collapsible);
}

spec::SemanticNode ColorPicker::item(std::string_view path, std::string_view key) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, key);
    const auto hsv = picking::to_hsv(picking::parse_hex(props.value).value_or(Color{}));
    std::string at;
    if (key == "hue") {
        n.role = Role::slider;
        n.name = "Hue";
        n.value = Value{}.set("value", std::round(hue_)).set("min", 0.0).set("max", 360.0);
        for (const char* a : {"increment", "decrement"}) n.actions.push_back({a, Effect::reversible, {}, {}, {}});
        n.actions.push_back({"set_value", Effect::reversible, {}, {}, {{"value", "any"}}});  // degrees; the picker's #rrggbb taken back here too
        at = "at:hue";
    } else if (key == "field") {
        n.role = Role::slider;
        n.name = "Saturation and brightness";
        n.value = Value{}.set("saturation", std::round(hsv.s * 100)).set("brightness", std::round(hsv.v * 100));
        n.actions.push_back({"set_value", Effect::reversible, {}, {}, {{"saturation", "f64"}, {"brightness", "f64"}}});
        at = "at:field";
    } else {
        const std::size_t i = std::stoul(std::string(key.substr(7)));  // swatch_<n>
        n.role = Role::button;
        n.name = props.swatches[i];
        n.states.set(State::selected, picking::parse_hex(props.swatches[i]).transform(picking::hex).value_or("") == props.value);
        n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
        at = std::format("at:swatch_{}", i);
    }
    if (const NodeId p = expanded() ? panel() : NodeId{}) {
        const Value r = services().get(p, at);
        if (const Value::List* l = r.list(); l && l->size() == 4)
            n.bounds = spec::Rect{static_cast<float>((*l)[0].as_double()), static_cast<float>((*l)[1].as_double()), static_cast<float>((*l)[2].as_double()), static_cast<float>((*l)[3].as_double())};
    }
    return n;
}

std::vector<spec::SemanticNode> ColorPicker::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    if (!expanded()) return out;
    out.push_back(item(path, "field"));
    out.push_back(item(path, "hue"));
    for (std::size_t i = 0; i < props.swatches.size(); ++i) out.push_back(item(path, std::format("swatch_{}", i)));
    return out;
}

std::optional<spec::SemanticNode> ColorPicker::virtual_item(std::string_view path, std::string_view key) const {
    if (key == "hue" || key == "field") return item(path, key);
    if (key.starts_with("swatch_")) {
        std::size_t i = 0;
        const std::string_view n = key.substr(7);
        if (std::from_chars(n.data(), n.data() + n.size(), i).ec == std::errc{} && i < props.swatches.size()) return item(path, key);
    }
    return std::nullopt;
}

ActResult ColorPicker::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (props.disabled) return ActResult::fail("it is disabled");
    const auto hsv = picking::to_hsv(picking::parse_hex(props.value).value_or(Color{}));
    const Value* given = ctx.args.get("value");
    if (key == "hue" && (action == "increment" || action == "decrement" || (action == "set_value" && given && given->is_number()))) {
        double h = hue_;
        if (action == "set_value") {
            if (given->as_double() < 0 || given->as_double() > 360) return ActResult::fail("a hue is 0–360");
            h = given->as_double();
        } else h += action == "increment" ? 5 : -5;
        h = std::fmod(std::fmod(h, 360.0) + 360.0, 360.0);
        return take(picking::hex(picking::from_hsv({h, hsv.s, hsv.v})), h);
    }
    if (key == "field" && action == "set_value" && !given) {
        const Value* s = ctx.args.get("saturation");
        const Value* b = ctx.args.get("brightness");
        if ((!s || !s->is_number()) && (!b || !b->is_number())) return ActResult::fail("set_value takes saturation and brightness, 0–100");
        const double sat = s && s->is_number() ? std::clamp(s->as_double(), 0.0, 100.0) / 100 : hsv.s;
        const double val = b && b->is_number() ? std::clamp(b->as_double(), 0.0, 100.0) / 100 : hsv.v;
        return take(picking::hex(picking::from_hsv({hue_, sat, val})), hue_);
    }
    if (key.starts_with("swatch_") && action == "activate") {
        const auto it = virtual_item("", key);
        if (!it) return ActResult::fail(std::format("no swatch '{}'", key.substr(7)));
        return take(props.swatches[std::stoul(std::string(key.substr(7)))]);
    }
    return act(ctx, action);  // an inverse taken back on the item (S-12): the picker's own set_value
}

// ── its panel ──

Value ColorPanel::picker(std::string_view prop) const { return services_->get(services_->parent(node_), prop); }

Rect ColorPanel::field_rect() noexcept { return {panel_pad, panel_pad, panel_w, panel_field_h}; }
Rect ColorPanel::hue_rect() noexcept { return {panel_pad, panel_pad + panel_field_h + panel_gap, panel_w, panel_hue_h}; }

Rect ColorPanel::swatch_rect(std::size_t i) const noexcept {
    const float gap = (panel_w - static_cast<float>(swatches_per_row) * swatch) / static_cast<float>(swatches_per_row - 1);
    const float top = hue_rect().bottom() + panel_gap;
    return {panel_pad + static_cast<float>(i % swatches_per_row) * (swatch + gap), top + static_cast<float>(i / swatches_per_row) * (swatch + 6), swatch, swatch};
}

bool ColorPanel::set_prop(std::string_view k, const Value& v) {
    if (k != "open") return false;
    open_ = v.as_bool();
    if (services_) services_->restyle(node_);
    return true;
}

Value ColorPanel::get_prop(std::string_view k) const {
    if (k == "open") return Value(open_);
    if (k.starts_with("at:") && services_) {
        const Rect me = services_->bounds(node_);
        Rect r;
        if (k == "at:field") r = field_rect();
        else if (k == "at:hue") r = hue_rect();
        else if (k.starts_with("at:swatch_")) r = swatch_rect(std::stoul(std::string(k.substr(10))));
        else return {};
        return Value(Value::List{Value(static_cast<double>(me.x + r.x)), Value(static_cast<double>(me.y + r.y)), Value(static_cast<double>(r.width)),
                                 Value(static_cast<double>(r.height))});
    }
    return {};
}

void ColorPanel::style(const Theme&, mlibs::layout::Style& s) const { s.display = open_ ? mlibs::layout::Display::flex : mlibs::layout::Display::none; }

Size ColorPanel::measure(MeasureContext&, std::optional<float>, std::optional<float>, mlibs::layout::Available, mlibs::layout::Available) {
    const Value swatches = picker("swatches");
    const std::size_t n = swatches.list() ? swatches.list()->size() : 0;
    const float rows = static_cast<float>((n + swatches_per_row - 1) / swatches_per_row);
    const float swatches_h = n ? panel_gap + rows * swatch + (rows - 1) * 6 : 0;
    return {panel_w + 2 * panel_pad, hue_rect().bottom() + swatches_h + panel_pad};
}

void ColorPanel::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    popup_surface(ctx);
    const auto hsv = picking::to_hsv(picking::parse_hex(picker("value").as_string()).value_or(Color{}));
    // The field: the picker's hue at full strength, whitened to the left, darkened downwards.
    const double h = picker("hue_now").as_double(hsv.h);
    const Rect f = field_rect();
    const CornerRadii fr = CornerRadii::all(6);
    ctx.list.fill_round_rect(f, fr, xrender::Paint::solid(picking::from_hsv({h, 1, 1})));
    auto gradient = [&](Point from, Point to, Color a, Color b) {
        const std::array<xrender::GradientStop, 2> stops{{{0, a}, {1, b}}};
        xrender::Paint p;
        p.kind = xrender::Paint::Kind::gradient;
        p.gradient.start = from;
        p.gradient.end = to;
        p.gradient.stops = stops;
        ctx.list.fill_round_rect(f, fr, p);
    };
    gradient({f.x, f.y}, {f.right(), f.y}, Color{1, 1, 1, 1}, Color{1, 1, 1, 0});
    gradient({f.x, f.y}, {f.x, f.bottom()}, Color{0, 0, 0, 0}, Color{0, 0, 0, 1});
    // Where the colour is in it: a ring.
    const Point at{f.x + f.width * static_cast<float>(hsv.s), f.y + f.height * static_cast<float>(1 - hsv.v)};
    ctx.list.stroke_round_rect({at.x - 7, at.y - 7, 14, 14}, CornerRadii::all(7), 2.5f, xrender::Paint::solid(Color{1, 1, 1, 1}));
    ctx.list.stroke_round_rect({at.x - 8.5f, at.y - 8.5f, 17, 17}, CornerRadii::all(8.5f), 1, xrender::Paint::solid(Color{0, 0, 0, 0.35f}));
    // The hue strip, round the wheel, and where the hue is on it.
    const Rect hr = hue_rect();
    std::array<xrender::GradientStop, 7> stops{};
    for (std::size_t i = 0; i < stops.size(); ++i) stops[i] = {static_cast<float>(i) / 6, picking::from_hsv({static_cast<double>(i) * 60, 1, 1})};
    xrender::Paint hp;
    hp.kind = xrender::Paint::Kind::gradient;
    hp.gradient.start = {hr.x, hr.y};
    hp.gradient.end = {hr.right(), hr.y};
    hp.gradient.stops = stops;
    ctx.list.fill_round_rect(hr, CornerRadii::all(hr.height / 2), hp);
    const float hx = hr.x + hr.width * static_cast<float>(h / 360);
    const Rect knob{hx - 8, hr.center().y - 8, 16, 16};
    parts::shadow(ctx, "sm", knob, CornerRadii::all(8));
    ctx.list.fill_round_rect(knob, CornerRadii::all(8), xrender::Paint::solid(Color{1, 1, 1, 1}));
    ctx.list.stroke_round_rect({knob.x + 2.5f, knob.y + 2.5f, 11, 11}, CornerRadii::all(5.5f), 1, xrender::Paint::solid(picking::from_hsv({h, 1, 1})));
    // The application's swatches; the chosen one ringed.
    const Value swatches = picker("swatches"), value = picker("value");  // held: their lists and text are read below
    if (const Value::List* sw = swatches.list()) {
        const std::string chosen(value.as_string());
        for (std::size_t i = 0; i < sw->size(); ++i) {
            const auto c = picking::parse_hex((*sw)[i].as_string());
            if (!c) continue;
            const Rect r = swatch_rect(i);
            ctx.list.fill_round_rect(r, CornerRadii::all(5), xrender::Paint::solid(*c));
            ctx.list.stroke_round_rect({r.x + 0.5f, r.y + 0.5f, r.width - 1, r.height - 1}, CornerRadii::all(5), 1, xrender::Paint::solid(th.color("border")));
            if (picking::hex(*c) == chosen)
                ctx.list.stroke_round_rect({r.x - 3, r.y - 3, r.width + 6, r.height + 6}, CornerRadii::all(7), 2, xrender::Paint::solid(th.color("accent")));
        }
    }
}

void ColorPanel::drag_to(Point local) {
    const auto hsv = picking::to_hsv(picking::parse_hex(picker("value").as_string()).value_or(Color{}));
    double h = picker("hue_now").as_double(hsv.h);
    std::string hex;
    if (dragging_ == 1) {
        const Rect f = field_rect();
        const double s = std::clamp((local.x - f.x) / f.width, 0.0f, 1.0f), v = 1 - std::clamp((local.y - f.y) / f.height, 0.0f, 1.0f);
        hex = picking::hex(picking::from_hsv({h, s, v}));
    } else {
        const Rect r = hue_rect();
        h = 360.0 * std::clamp((local.x - r.x) / r.width, 0.0f, 1.0f);
        hex = picking::hex(picking::from_hsv({h, hsv.s, hsv.v}));
    }
    services_->emit(node_, "picking", Value{}.set("value", hex).set("hue", h));
}

bool ColorPanel::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down) {
        if (k->key == Key::escape) {
            services_->set(node_, "open", Value(false));
            services_->focus(services_->parent(node_));
            services_->invalidate_semantics(services_->parent(node_));
            return true;
        }
        // The keys: the hue (Left, Right), the brightness (Up, Down) — each a step of the picker's set_value.
        const auto hsv = picking::to_hsv(picking::parse_hex(picker("value").as_string()).value_or(Color{}));
        const double h = picker("hue_now").as_double(hsv.h);
        std::optional<picking::Hsv> to;
        if (k->key == Key::left || k->key == Key::right) to = picking::Hsv{std::fmod(h + (k->key == Key::right ? 5 : 355), 360.0), hsv.s, hsv.v};
        if (k->key == Key::up || k->key == Key::down) to = picking::Hsv{h, hsv.s, std::clamp(hsv.v + (k->key == Key::up ? 0.05 : -0.05), 0.0, 1.0)};
        if (!to) return false;
        (void)services_->perform(services_->parent(node_), "set_value", Value{}.set("value", picking::hex(picking::from_hsv(*to))));
        return true;
    }
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    switch (p->kind) {
        case PointerKind::down: {
            if (!primary(*p)) return false;
            const Rect hr = hue_rect();
            if (field_rect().contains(ctx.local)) dragging_ = 1;
            else if (Rect{hr.x, hr.y - 6, hr.width, hr.height + 12}.contains(ctx.local)) dragging_ = 2;
            if (!dragging_) return true;
            services_->emit(node_, "pick_start", {});
            services_->capture_pointer(node_);
            drag_to(ctx.local);
            return true;
        }
        case PointerKind::move:
            if (!dragging_) return false;
            drag_to(ctx.local);
            return true;
        case PointerKind::up:
        case PointerKind::cancel: {
            if (dragging_) {
                dragging_ = 0;
                services_->release_pointer();
                services_->emit(node_, "picked", {});
                return true;
            }
            if (p->kind != PointerKind::up || !primary(*p)) return false;
            const Value swatches = picker("swatches");
            if (const Value::List* sw = swatches.list())
                for (std::size_t i = 0; i < sw->size(); ++i)
                    if (swatch_rect(i).contains(ctx.local)) {
                        (void)services_->perform(services_->parent(node_), "set_value", Value{}.set("value", (*sw)[i].as_string()));
                        return true;
                    }
            return true;
        }
        default:
            return false;
    }
}

void ColorPanel::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.name = "Colour";
    node.actions.push_back({"dismiss", Effect::reversible, {}, {}, {}});
}

ActResult ColorPanel::act(ActContext&, std::string_view action) {
    if (action != "dismiss") return ActResult::fail(std::format("the picker has no action '{}' (its parts are the colour picker's items)", action));
    services_->set(node_, "open", Value(false));
    services_->focus(services_->parent(node_));
    return ActResult::done();
}

}  // namespace omui
