module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

namespace {

constexpr float crumb_gap = 8, crumb_pad = 4;
constexpr float slot = 32, slot_gap = 4;

// A chevron pointing left (back) or right (on), centred at c.
void arrowhead(PaintContext& ctx, Point c, bool right, Color color) {
    const float s = 5, d = right ? 1.0f : -1.0f;
    xrender::Path p;
    p.move_to({c.x - d * s / 2, c.y - s}).line_to({c.x + d * s / 2, c.y}).line_to({c.x - d * s / 2, c.y + s});
    xrender::StrokeStyle st;
    st.width = 1.75f;
    st.cap = xrender::LineCap::round;
    st.join = xrender::LineJoin::round;
    ctx.list.stroke_path(p, st, xrender::Paint::solid(color));
}

// A part's box as the semantic tree states bounds: in the window (the node's own box, then the part in it).
spec::Rect in_window(const Rect& node, const Rect& part) { return {node.x + part.x, node.y + part.y, part.width, part.height}; }

std::optional<std::int64_t> index_of(std::string_view key) {
    std::int64_t i = 0;
    if (key.empty() || std::from_chars(key.data(), key.data() + key.size(), i).ec != std::errc{}) return std::nullopt;
    return i;
}

}  // namespace

// ── breadcrumb ──

void Breadcrumb::mounted() {
    separator_.set_style("label");
    separator_.set_text(props.separator);
    relabel();
}

void Breadcrumb::relabel() {
    crumbs_.resize(props.items.size());
    for (std::size_t i = 0; i < props.items.size(); ++i) {
        crumbs_[i].set_style("label");
        crumbs_[i].set_text(props.items[i]);
    }
    if (!props.items.empty()) focus_ = std::min(focus_, props.items.size() - 1);
}

void Breadcrumb::prop_changed(BreadcrumbProp p) {
    if (p == BreadcrumbProp::items) relabel();
    if (p == BreadcrumbProp::separator) separator_.set_text(props.separator);
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

Size Breadcrumb::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const Theme& th = m.services.theme();
    float w = 0, h = 0;
    const Size sep = separator_.measure(m.services, th.color("text_disabled"));
    for (std::size_t i = 0; i < crumbs_.size(); ++i) {
        const Size t = crumbs_[i].measure(m.services, th.color("text"));
        w += std::ceil(t.width) + 2 * crumb_pad + (i ? 2 * crumb_gap + std::ceil(sep.width) : 0);
        h = std::max(h, std::ceil(t.height) + 2 * crumb_pad);
    }
    return {width.value_or(w), height.value_or(std::max(h, 24.0f))};
}

void Breadcrumb::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    at_.assign(crumbs_.size(), Rect{});
    const Size sep = separator_.measure(ctx.services, th.color("text_disabled"));
    float x = 0;
    for (std::size_t i = 0; i < crumbs_.size(); ++i) {
        if (i) {
            x += crumb_gap;
            separator_.paint(ctx, {x, (ctx.size.height - sep.height) / 2}, th.color("text_disabled"));
            x += std::ceil(sep.width) + crumb_gap;
        }
        const bool current = i + 1 == crumbs_.size();
        const Color c = current ? th.color("text") : hover_ == static_cast<int>(i) ? th.color("accent") : th.color("text_muted");
        const Size t = crumbs_[i].measure(ctx.services, c);
        const Rect r{x, (ctx.size.height - t.height) / 2 - crumb_pad, std::ceil(t.width) + 2 * crumb_pad, std::ceil(t.height) + 2 * crumb_pad};
        at_[i] = r;
        crumbs_[i].paint(ctx, {x + crumb_pad, r.y + crumb_pad}, c);
        // A link looks like one where the pointer is over it: underlined.
        if (!current && hover_ == static_cast<int>(i)) ctx.list.fill_rect({r.x + crumb_pad, r.bottom() - crumb_pad + 1, std::ceil(t.width), 1}, c);
        x = r.right();
    }
}

void Breadcrumb::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible) || focus_ >= at_.size()) return;
    parts::focus_ring(ctx, at_[focus_], CornerRadii::all(4));
}

bool Breadcrumb::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* p = std::get_if<PointerEvent>(&e)) {
        int over = -1;
        for (std::size_t i = 0; i < at_.size(); ++i)
            if (at_[i].contains(ctx.local)) over = static_cast<int>(i);
        if (p->kind == PointerKind::leave) over = -1;
        if (over != hover_) hover_ = over, ctx.repaint();
        if (p->kind == PointerKind::up && over >= 0) {
            (void)go_to(over);
            return true;
        }
        return p->kind == PointerKind::up;  // a press between crumbs is nobody's
    }
    if (const auto* k = std::get_if<KeyEvent>(&e); k && k->kind == KeyKind::down && (k->key == Key::enter || k->key == Key::space)) {
        (void)go_to(static_cast<std::int64_t>(focus_));
        return true;
    }
    return false;
}

spec::SemanticNode Breadcrumb::item(std::string_view path, std::size_t i) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, i);
    n.role = Role::link;
    n.name = props.items[i];
    const bool current = i + 1 == props.items.size();
    n.states.set(State::selected, current);
    if (i < at_.size() && attached()) n.bounds = in_window(services().bounds(node()), at_[i]);
    if (!current) n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

void Breadcrumb::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = props.items.empty() ? Value{} : Value(props.items.back());
}

std::vector<spec::SemanticNode> Breadcrumb::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (std::size_t i = 0; i < props.items.size(); ++i) out.push_back(item(path, i));
    return out;
}

std::optional<spec::SemanticNode> Breadcrumb::virtual_item(std::string_view path, std::string_view key) const {
    const auto i = index_of(key);
    if (!i || *i < 0 || static_cast<std::size_t>(*i) >= props.items.size()) return std::nullopt;
    return item(path, static_cast<std::size_t>(*i));
}

ActResult Breadcrumb::act_item(ActContext&, std::string_view key, std::string_view action) {
    const auto i = index_of(key);
    if (!i || action != "activate") return ActResult::fail(std::format("a crumb is activated, not '{}'", action));
    return go_to(*i);
}

ActResult Breadcrumb::go_to(std::int64_t index) {
    if (index < 0 || static_cast<std::size_t>(index) >= props.items.size()) return ActResult::fail(std::format("there is no place {}", index));
    if (static_cast<std::size_t>(index) + 1 == props.items.size()) return ActResult::done(Value(index));  // here already
    // Up to that place: the ones below it are left, as the way back down would be another.
    std::vector<std::string> up(props.items.begin(), props.items.begin() + index + 1);
    write_items(std::move(up));
    prop_changed(BreadcrumbProp::items);
    focus_ = static_cast<std::size_t>(index);
    emit_navigated(index);
    return ActResult::done(Value(index));
}

ActResult Breadcrumb::go(ActContext&, const Value& index) { return go_to(index.as_int(-1)); }

ActResult Breadcrumb::next(ActContext&) {
    if (focus_ + 1 < props.items.size()) ++focus_, services().invalidate_paint(node());
    return ActResult::done();
}

ActResult Breadcrumb::previous(ActContext&) {
    if (focus_ > 0) --focus_, services().invalidate_paint(node());
    return ActResult::done();
}

// ── pagination ──

void Pagination::mounted() {}

void Pagination::prop_changed(PaginationProp) {
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

std::vector<std::int64_t> Pagination::slots() const {
    const std::int64_t n = pages(), p = std::clamp<std::int64_t>(props.page, 1, n), k = std::max<std::int64_t>(0, props.around);
    std::vector<std::int64_t> out{0};
    // The first, the pages around the current one, the last; a gap where pages are left out (one page left out
    // is shown instead: a gap stands for two or more).
    std::int64_t lo = std::max<std::int64_t>(1, p - k), hi = std::min(n, p + k);
    if (lo <= 3) lo = 1;
    if (hi >= n - 2) hi = n;
    if (lo > 1) out.push_back(1), out.push_back(-2);
    for (std::int64_t i = lo; i <= hi; ++i) out.push_back(i);
    if (hi < n) out.push_back(-2), out.push_back(n);
    out.push_back(-1);
    return out;
}

Size Pagination::measure(MeasureContext& m, std::optional<float> width, std::optional<float> height, mlibs::layout::Available, mlibs::layout::Available) {
    const float s = m.services.theme().scaled(slot);
    const auto n = static_cast<float>(slots().size());
    return {width.value_or(n * s + (n - 1) * slot_gap), height.value_or(s)};
}

void Pagination::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    const float s = th.scaled(slot);
    const std::int64_t n = pages(), cur = std::clamp<std::int64_t>(props.page, 1, n);
    at_.clear();
    const auto all = slots();
    if (digits_.size() < all.size()) digits_.resize(all.size());
    float x = 0;
    std::size_t k = 0;
    for (const std::int64_t v : all) {
        parts::Label& digit = digits_[k++];
        digit.set_style("label");
        const Rect r{x, (ctx.size.height - s) / 2, s, s};
        at_.emplace_back(v, r);
        x += s + slot_gap;
        const CornerRadii round = CornerRadii::all(th.corner("medium", 8)).fitted(r.size());
        const bool off = (v == 0 && cur <= 1) || (v == -1 && cur >= n);
        if (v == cur) {
            ctx.list.fill_round_rect(r, round, xrender::Paint::solid(th.color("accent")));
        } else if (v != -2 && !off && hover_ == v) {
            ctx.list.fill_round_rect(r, round, xrender::Paint::solid(th.color("surface_variant")));
        }
        if (v == 0 || v == -1) {
            arrowhead(ctx, r.center(), v == -1, off ? th.color("text_disabled") : th.color("text_muted"));
            continue;
        }
        const Color c = v == cur ? th.color("on_accent") : v == -2 ? th.color("text_disabled") : th.color("text");
        digit.set_text(v == -2 ? std::string("…") : std::format("{}", v));  // shaped again only when it changed
        const Size t = digit.measure(ctx.services, c);
        digit.paint(ctx, {r.x + (s - t.width) / 2, r.y + (s - t.height) / 2}, c);
    }
}

void Pagination::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible)) return;
    for (const auto& [v, r] : at_)
        if (v == std::clamp<std::int64_t>(props.page, 1, pages())) parts::focus_ring(ctx, r, CornerRadii::all(ctx.theme.corner("medium", 8)).fitted(r.size()));
}

bool Pagination::on_event(EventContext& ctx, const InputEvent& e) {
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    std::int64_t over = -3;
    for (const auto& [v, r] : at_)
        if (r.contains(ctx.local)) over = v;
    if (p->kind == PointerKind::leave) over = -3;
    if (over != hover_) hover_ = over, ctx.repaint();
    if (p->kind != PointerKind::up) return false;
    const std::int64_t cur = std::clamp<std::int64_t>(props.page, 1, pages());
    if (over == 0) (void)go_to(cur - 1);
    else if (over == -1) (void)go_to(cur + 1);
    else if (over > 0) (void)go_to(over);
    return true;
}

void Pagination::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("value", std::clamp<std::int64_t>(props.page, 1, pages())).set("min", std::int64_t{1}).set("max", pages());
}

std::vector<spec::SemanticNode> Pagination::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (const std::int64_t v : slots())
        if (v != -2)
            if (auto n = virtual_item(path, v == 0 ? "previous" : v == -1 ? "next" : std::format("{}", v))) out.push_back(std::move(*n));
    return out;
}

std::optional<spec::SemanticNode> Pagination::virtual_item(std::string_view path, std::string_view key) const {
    const std::int64_t n = pages(), cur = std::clamp<std::int64_t>(props.page, 1, n);
    spec::SemanticNode out;
    out.id = std::format("{}/{}", path, key);
    out.role = Role::link;
    std::int64_t v = 0;
    if (key == "previous") {
        out.name = "Previous page";
        v = 0;
        out.states.set(State::disabled, cur <= 1);
    } else if (key == "next") {
        out.name = "Next page";
        v = -1;
        out.states.set(State::disabled, cur >= n);
    } else if (const auto i = index_of(key); i && *i >= 1 && *i <= n) {
        out.name = std::format("Page {}", *i);
        v = *i;
        out.states.set(State::selected, *i == cur);
    } else {
        return std::nullopt;
    }
    for (const auto& [s, r] : at_)
        if (s == v && attached()) out.bounds = in_window(services().bounds(node()), r);
    if (!out.states.has(State::disabled) && !out.states.has(State::selected)) out.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return out;
}

ActResult Pagination::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    // Taken back: a page's step is undone on that page, by the page it replaced.
    if (action == "set_value") return set_value(ctx, arg(ctx, "value"));
    if (action != "activate") return ActResult::fail(std::format("a page is activated, not '{}'", action));
    const std::int64_t cur = std::clamp<std::int64_t>(props.page, 1, pages());
    if (key == "previous") return go_to(cur - 1);
    if (key == "next") return go_to(cur + 1);
    if (const auto i = index_of(key)) return go_to(*i);
    return ActResult::fail(std::format("there is no page '{}'", key));
}

ActResult Pagination::go_to(std::int64_t page) {
    if (page < 1 || page > pages()) return ActResult::fail(std::format("there is no page {} (1 to {})", page, pages()));
    const std::int64_t old = props.page;
    if (page == old) return ActResult::done(Value(page));
    write_page(page);
    prop_changed(PaginationProp::page);
    emit_changed(page);
    return ActResult::done(Value(page)).undo_by("set_value", Value{}.set("value", old));
}

ActResult Pagination::increment(ActContext&) { return go_to(std::clamp<std::int64_t>(props.page, 1, pages()) + 1); }
ActResult Pagination::decrement(ActContext&) { return go_to(std::clamp<std::int64_t>(props.page, 1, pages()) - 1); }

ActResult Pagination::set_value(ActContext&, const Value& value) {
    if (value.as_string() == "first") return go_to(1);
    if (value.as_string() == "last") return go_to(pages());
    if (!value.is_number()) return ActResult::fail(std::format("set_value takes a page (or first, last), not {}", value.json()));
    return go_to(value.as_int());
}

}  // namespace omui
