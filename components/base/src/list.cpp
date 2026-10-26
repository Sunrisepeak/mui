module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

namespace {

constexpr std::size_t overscan = 3;  // rows materialised beyond each edge

}  // namespace

// ── list ──

void List::bind_model() {
    model_ = props.model.empty() ? nullptr : &services().model(props.model);
    prefix_dirty_ = true;
    // The model's revision is a signal: a change of the rows recomposes the window.
    watch_ = Watch([this] {
        if (model_) (void)model_->revision();
        untracked([this] {
            prefix_dirty_ = true;
            update_window();
            services().recompose(node());
            services().invalidate_semantics(node());
        });
    });
}

void List::mounted() { bind_model(); }

void List::prop_changed(ListProp p) {
    if (!attached()) return;
    if (p == ListProp::model) bind_model();
    if (p == ListProp::estimate) prefix_dirty_ = true;
    if (p == ListProp::selected || p == ListProp::estimate || p == ListProp::selectable) services().recompose(node());
    services().invalidate_semantics(node());
}

void List::style(const Theme&, layout::Style& s) const {
    s.direction = layout::Direction::column;
    s.overflow_y = layout::Overflow::scroll;
}

float List::height_of(std::size_t i) const {
    auto it = heights_.find(model_->at(i).key);
    return it == heights_.end() ? static_cast<float>(props.estimate) : it->second;
}

float List::top_of(std::size_t i) const {
    if (!model_) return 0;
    if (prefix_dirty_ || prefix_.size() != rows() + 1) {
        prefix_.assign(rows() + 1, 0.0f);
        for (std::size_t k = 0; k < rows(); ++k) prefix_[k + 1] = prefix_[k] + height_of(k);
        prefix_dirty_ = false;
    }
    return prefix_[std::min(i, rows())];
}

float List::viewport() const {
    const float h = attached() ? services().bounds(node()).height : 0;
    // Before the first layout, a screenful of estimated rows.
    return h > 0 ? h : static_cast<float>(props.estimate) * 20;
}

std::size_t List::index_at(float y) const {
    if (rows() == 0) return 0;
    (void)top_of(0);
    auto it = std::upper_bound(prefix_.begin(), prefix_.end(), y);
    const auto i = static_cast<std::size_t>(std::max<std::ptrdiff_t>(0, (it - prefix_.begin()) - 1));
    return std::min(i, rows() - 1);
}

bool List::update_window() {
    const std::size_t n = rows();
    std::size_t first = 0, last = 0;
    if (n > 0) {
        first = index_at(offset_);
        last = index_at(offset_ + viewport()) + 1;
        first = first > overscan ? first - overscan : 0;
        last = std::min(n, last + overscan);
    }
    const bool changed = first != first_ || last != last_;
    first_ = first;
    last_ = last;
    return changed;
}

std::vector<Element> List::compose() {
    std::vector<Element> out;
    auto spacer = [](std::string id, float h) {
        Element e = box({.height = h});
        e.id = std::move(id);
        e.prop("role", Source{Value("none"), {}, {}});
        return e;
    };
    if (!model_) return out;
    if (current_.empty() || !model_->find(current_)) current_ = rows() > 0 ? model_->at(std::min(first_, rows() - 1)).key : std::string{};
    out.push_back(spacer("before", top_of(first_)));
    for (std::size_t i = first_; i < last_ && i < rows(); ++i) {
        const Row& row = model_->at(i);
        Element e;
        e.type = &component_type<ListItem>();
        e.id = row.key;
        e.key = row.key;
        e.prop("key", Source{Value(row.key), {}, {}});
        e.prop("label", Source{Value(Model::label_of(row)), {}, {}});
        const bool sel = std::ranges::find(props.selected, row.key) != props.selected.end();
        e.prop("selected", Source{Value(sel), {}, {}});
        // A roving focus: Tab enters at one item; arrows walk the rest.
        e.prop("focusable", Source{Value(row.key == current_), {}, {}});
        e.on("activate", [this, key = row.key](const Value&) { activate_row(key); });
        e.on("focus", [this, key = row.key](const Value&) { current_ = key; });
        e.children.push_back(item ? item(row, i) : text({.text = Model::label_of(row)}));
        out.push_back(std::move(e));
    }
    out.push_back(spacer("after", std::max(0.0f, total() - top_of(last_))));
    return out;
}

void List::scroll(float to) {
    const float limit = std::max(0.0f, total() - viewport());
    to = std::clamp(to, 0.0f, limit);
    if (to == offset_) return;
    offset_ = to;
    if (update_window()) services().recompose(node());
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

bool List::on_event(EventContext&, const InputEvent& e) {
    const auto* w = std::get_if<WheelEvent>(&e);
    if (!w || w->delta.y == 0) return false;
    const float before = offset_;
    revealing_.clear();
    scroll(offset_ + w->delta.y);
    return offset_ != before;  // at an end, the outer view scrolls
}

void List::laid_out() {
    // Measured heights replace estimates: the spacers follow at the next frame.
    bool moved = false;
    for (NodeId c : services().children(node())) {
        const std::string key(services().get(c, "key").as_string());
        if (key.empty()) continue;
        const float h = services().bounds(c).height;
        if (h <= 0) continue;
        auto it = heights_.find(key);
        if (it == heights_.end() ? std::abs(h - static_cast<float>(props.estimate)) > 0.5f : std::abs(it->second - h) > 0.5f) moved = true;
        heights_[key] = h;
    }
    if (moved) prefix_dirty_ = true;
    if (!revealing_.empty() && model_) {
        if (auto i = model_->find(revealing_); i && show(*i)) moved = true;
        else if (!moved) revealing_.clear();  // in place, with its real height
    }
    // A viewport that changed (the first layout) changes the window too.
    if (update_window() || moved) {
        services().recompose(node());
        services().request_frame();
    }
}

void List::paint_over(PaintContext& ctx) const {
    const float view = viewport(), all = total();
    if (all <= view || view <= 0) return;
    constexpr float bar = 6, inset = 2;
    const float h = std::max(24.0f, view * view / all);
    const float y = (view - h) * (offset_ / std::max(1.0f, all - view));
    ctx.list.fill_round_rect({ctx.size.width - bar - inset, y, bar, h}, CornerRadii::all(bar / 2),
                             render::Paint::solid(ctx.theme.color("border_strong").with_alpha(0.7f)));
}

void List::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.value = Value{}.set("rows", static_cast<std::int64_t>(rows())).set("first", static_cast<std::int64_t>(first_)).set("last",
                                                                                                                     static_cast<std::int64_t>(last_));
    if (props.selectable == ListSelectable::multiple) node.states.set(State::multiselectable);
}

std::vector<spec::SemanticNode> List::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    if (!model_) return out;
    out.reserve(rows());
    for (std::size_t i = 0; i < rows(); ++i) {
        const Row& row = model_->at(i);
        spec::SemanticNode n;
        n.id = std::format("{}/{}", path, row.key);
        n.role = Role::list_item;
        n.name = Model::label_of(row);
        n.states.set(State::offscreen, i < first_ || i >= last_);
        n.states.set(State::selected, std::ranges::find(props.selected, row.key) != props.selected.end());
        n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
        n.actions.push_back({"scroll_into_view", Effect::read, {}, {}, {}});
        out.push_back(std::move(n));
    }
    return out;
}

bool List::show(std::size_t i) {
    const float before = offset_;
    const float top = top_of(i), bottom = top + height_of(i);
    if (top < offset_) scroll(top);
    else if (bottom > offset_ + viewport()) scroll(bottom - viewport());
    return offset_ != before;
}

bool List::reveal(std::string_view key) {
    if (!model_) return false;
    const auto i = model_->find(key);
    if (!i) return false;
    // Estimated heights place it now; measured ones may move it, so it stays
    // the target until it is seen in place (paint checks).
    revealing_ = std::string(key);
    show(*i);
    if (update_window() || *i < first_ || *i >= last_) {
        // Still outside (a viewport not laid out yet): centre the window on it.
        first_ = *i > overscan ? *i - overscan : 0;
        last_ = std::min(rows(), *i + overscan + 1);
    }
    services().recompose(node());
    return true;
}

ActResult List::scroll_to(ActContext&, const Value& x, const Value& y) {
    (void)x;
    float to = offset_;
    if (y.is_number()) to = static_cast<float>(y.as_double());
    else if (y.as_string() == "end") to = total();
    else if (y.as_string() == "page") to = offset_ + viewport() * 0.9f;
    else if (y.as_string() == "-page") to = offset_ - viewport() * 0.9f;
    scroll(to);
    return ActResult::done(Value{}.set("y", static_cast<double>(offset_)));
}

ActResult List::step(int by) {
    if (rows() == 0) return ActResult::fail("the list is empty");
    std::size_t i = 0;
    if (auto at = model_->find(current_)) {
        const auto n = static_cast<std::ptrdiff_t>(rows());
        i = static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(*at) + by, 0, n - 1));
    }
    current_ = model_->at(i).key;
    reveal(current_);
    if (NodeId item = services().reveal(std::format("{}/{}", services().path(node()), current_))) services().focus(item);
    return ActResult::done(Value(current_));
}

ActResult List::next(ActContext&) { return step(1); }
ActResult List::previous(ActContext&) { return step(-1); }

void List::activate_row(const std::string& key) {
    current_ = key;
    emit_activated(key);
    if (props.selectable == ListSelectable::none) return;
    std::vector<std::string> sel = props.selected;
    if (props.selectable == ListSelectable::single) {
        sel = {key};
    } else if (auto it = std::ranges::find(sel, key); it != sel.end()) {
        sel.erase(it);
    } else {
        sel.push_back(key);
    }
    if (sel != props.selected) {
        write_selected(sel);
        emit_changed(props.selected);
        services().recompose(node());
        services().invalidate_semantics(node());
    }
}

// ── item ──

bool ListItem::set_prop(std::string_view k, const Value& v) {
    if (k == "key") key_ = std::string(v.as_string());
    else if (k == "selected") selected_ = v.as_bool();
    else return false;
    return true;
}

Value ListItem::get_prop(std::string_view k) const {
    if (k == "key") return Value(key_);
    if (k == "selected") return Value(selected_);
    return {};
}

void ListItem::style(const Theme&, layout::Style& s) const {
    s.padding = {6, 12, 6, 12};
    s.min_height = layout::pt(32);
    s.justify = layout::Justify::center;
}

void ListItem::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    Color fill = Color::transparent();
    if (selected_) fill = th.color("accent_subtle");
    else if (ctx.state.pressed) fill = th.color("surface_pressed");
    else if (ctx.state.hovered) fill = th.color("surface_variant");
    if (fill.a > 0) ctx.list.fill_round_rect({0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(th.corner("small", 4)), render::Paint::solid(fill));
    if (ctx.state.focused && ctx.state.focus_visible)
        ctx.list.stroke_round_rect({1, 1, ctx.size.width - 2, ctx.size.height - 2}, CornerRadii::all(th.corner("small", 4)), 2,
                                   render::Paint::solid(th.color("focus")));
}

void ListItem::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    node.states.set(State::selected, selected_);
}

Element list_view(ListOptions options, List::ItemFn item, std::source_location where) {
    Element e = list(std::move(options), where);
    e.configure = [item = std::move(item)](void* p) { static_cast<List*>(p)->item = item; };
    return e;
}

}  // namespace omui
