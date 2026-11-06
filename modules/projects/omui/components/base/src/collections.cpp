module openmui.ui.base;

import std;
import openmui.core;
import openmui.view;

namespace omui {

namespace {

constexpr float tree_row = 30, tree_indent = 18, tree_pad = 6;
constexpr float grid_head = 36, grid_row = 34, grid_pad = 12;

// A chevron pointing right, or down when open, centred at c.
void tree_chevron(PaintContext& ctx, Point c, bool down, Color color) {
    const float s = 8;
    auto at = [&](float x, float y) { return down ? Point{c.x - y, c.y + x} : Point{c.x + x, c.y + y}; };
    xrender::Path p;
    p.move_to(at(-s / 4, -s / 2)).line_to(at(s / 4, 0)).line_to(at(-s / 4, s / 2));
    xrender::StrokeStyle st;
    st.width = 1.6f;
    st.cap = xrender::LineCap::round;
    st.join = xrender::LineJoin::round;
    ctx.list.stroke_path(p, st, xrender::Paint::solid(color));
}

// A small triangle, up or down, centred at c (a column's sort).
void triangle(PaintContext& ctx, Point c, bool up, Color color) {
    const float w = 7, h = 4.5f, d = up ? -1.0f : 1.0f;
    xrender::Path p;
    p.move_to({c.x - w / 2, c.y - d * h / 2}).line_to({c.x + w / 2, c.y - d * h / 2}).line_to({c.x, c.y + d * h / 2}).close();
    ctx.list.fill_path(p, xrender::FillRule::nonzero, xrender::Paint::solid(color));
}

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    return out;
}

// How two cells order: numbers by value, text without regard to case, nothing last.
int compare(const Value& a, const Value& b) {
    if (a.is_null() || b.is_null()) return a.is_null() == b.is_null() ? 0 : a.is_null() ? 1 : -1;
    if (a.is_number() && b.is_number()) return a.as_double() < b.as_double() ? -1 : a.as_double() > b.as_double() ? 1 : 0;
    const std::string x = lower(DataGrid::text_of(a)), y = lower(DataGrid::text_of(b));
    return x < y ? -1 : x > y ? 1 : 0;
}

std::vector<std::string> strings_of(const Value& v) {
    std::vector<std::string> out;
    if (const Value::List* l = v.list())
        for (const Value& x : *l) out.emplace_back(x.as_string());
    return out;
}

}  // namespace

// ── tree view ──

void TreeView::bind_model() {
    model_ = props.model.empty() ? nullptr : &services().model(props.model);
    // The model's revision is a signal: a change of its rows shows and states them again.
    watch_ = Watch([this] {
        if (model_) (void)model_->revision();
        untracked([this] {
            dirty();
        });
    });
}

void TreeView::mounted() {
    bind_model();
}

void TreeView::dirty() {
    dirty_ = true;
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

void TreeView::prop_changed(TreeViewProp p) {
    if (!attached()) return;
    if (p == TreeViewProp::model) bind_model();
    dirty();
}

std::string TreeView::label_of(std::size_t row) const {
    const Row& r = model_->at(row);
    for (const char* k : {"label", "title", "name"})
        if (const Value* v = r.fields.get(k); v && !v->as_string().empty()) return std::string(v->as_string());
    return r.key;
}

std::string TreeView::parent_of(std::string_view key) const {
    if (!model_) return {};
    const auto i = model_->find(key);
    if (!i) return {};
    const Value* p = model_->at(*i).fields.get("parent");
    return p ? std::string(p->as_string()) : std::string();
}

bool TreeView::is_open(std::string_view key) const { return std::ranges::find(props.expanded, key) != props.expanded.end(); }

const std::vector<TreeView::Line>& TreeView::lines() const {
    if (!dirty_) return lines_;
    dirty_ = false;
    lines_.clear();
    kids_.clear();
    if (!model_) return lines_;
    for (std::size_t i = 0; i < model_->size(); ++i) {
        const Value* p = model_->at(i).fields.get("parent");
        std::string parent = p ? std::string(p->as_string()) : std::string();
        if (!parent.empty() && !model_->find(parent)) parent.clear();  // a parent not there: a root
        kids_[parent].push_back(i);
    }
    // Depth first, the open ones' children under them (a cycle goes no deeper than it came).
    auto walk = [&](auto& self, const std::string& parent, int depth) -> void {
        const auto it = kids_.find(parent);
        if (it == kids_.end() || depth > 64) return;
        for (const std::size_t r : it->second) {
            const std::string& key = model_->at(r).key;
            const bool parenting = kids_.contains(key);
            lines_.push_back({key, r, depth, parenting});
            if (parenting && is_open(key)) self(self, key, depth + 1);
        }
    };
    walk(walk, std::string(), 0);
    return lines_;
}

bool TreeView::has_children(std::string_view key) const {
    (void)lines();
    return kids_.contains(std::string(key));
}

std::optional<std::size_t> TreeView::line_of(std::string_view key) const {
    const auto& ls = lines();
    for (std::size_t i = 0; i < ls.size(); ++i)
        if (ls[i].key == key) return i;
    return std::nullopt;
}

Size TreeView::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    const float rows = static_cast<float>(lines().size());
    return {width.value_or(aw.is_definite() ? aw.value : 280.0f), height.value_or(std::min(rows, 12.0f) * tree_row + 2 * tree_pad)};
}

void TreeView::show(std::size_t line) {
    const float top = tree_pad + static_cast<float>(line) * tree_row, view = size_.height;
    if (view <= 0) return;
    if (top < offset_) offset_ = top - tree_pad;
    else if (top + tree_row > offset_ + view) offset_ = top + tree_row + tree_pad - view;
    offset_ = std::max(0.0f, offset_);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

void TreeView::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    size_ = ctx.size;
    const auto& ls = lines();
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    ctx.list.push_clip(box);
    const std::size_t from = static_cast<std::size_t>(std::max(0.0f, (offset_ - tree_pad) / tree_row));
    const std::size_t to = std::min(ls.size(), static_cast<std::size_t>((offset_ + ctx.size.height) / tree_row) + 1);
    if (labels_.size() < to - std::min(from, to)) labels_.resize(to - std::min(from, to));
    for (std::size_t i = from; i < to; ++i) {
        const Line& l = ls[i];
        const Rect r{tree_pad, tree_pad + static_cast<float>(i) * tree_row - offset_, ctx.size.width - 2 * tree_pad, tree_row};
        const bool chosen = l.key == props.value;
        if (chosen) ctx.list.fill_round_rect(r, CornerRadii::all(6), xrender::Paint::solid(th.color("accent_subtle")));
        else if (hover_ == static_cast<int>(i)) ctx.list.fill_round_rect(r, CornerRadii::all(6), xrender::Paint::solid(th.color("surface_variant")));
        const float x = r.x + 6 + static_cast<float>(l.depth) * tree_indent;
        if (l.parent) tree_chevron(ctx, {x + 6, r.center().y}, is_open(l.key), th.color("text_muted"));
        parts::Label& label = labels_[i - from];
        label.set_style("body");
        label.set_max_lines(1);
        label.set_text(label_of(l.row));
        const Color c = chosen ? th.color("accent") : th.color("text");
        const Size t = label.measure(ctx.services, c, std::max(0.0f, r.right() - x - 22));
        label.paint(ctx, {x + 18, r.center().y - t.height / 2}, c);
    }
    ctx.list.pop_clip();
}

void TreeView::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible)) return;
    if (const auto i = line_of(props.value)) {
        const Rect r{tree_pad, tree_pad + static_cast<float>(*i) * tree_row - offset_, ctx.size.width - 2 * tree_pad, tree_row};
        parts::focus_ring(ctx, r, CornerRadii::all(6));
    } else parts::focus_ring(ctx, {0, 0, ctx.size.width, ctx.size.height}, CornerRadii::all(6));
}

bool TreeView::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* w = std::get_if<WheelEvent>(&e)) {
        const float total = tree_pad * 2 + static_cast<float>(lines().size()) * tree_row;
        offset_ = std::clamp(offset_ + w->delta.y, 0.0f, std::max(0.0f, total - size_.height));
        ctx.repaint();
        services().invalidate_semantics(node());
        return true;
    }
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    const auto& ls = lines();
    const float y = ctx.local.y + offset_ - tree_pad;
    int over = y >= 0 && ctx.local.x >= 0 && ctx.local.x < size_.width ? static_cast<int>(y / tree_row) : -1;
    if (over >= static_cast<int>(ls.size()) || p->kind == PointerKind::leave) over = -1;
    if (over != hover_) hover_ = over, ctx.repaint();
    if (p->kind != PointerKind::up || over < 0 || (p->type == PointerType::mouse && p->button != MouseButton::left)) return p->kind == PointerKind::down && over >= 0;
    const Line& l = ls[static_cast<std::size_t>(over)];
    const float toggle = tree_pad + 6 + static_cast<float>(l.depth) * tree_indent;
    // On its chevron: open or close it. Else: choose it (twice: activate it).
    if (l.parent && ctx.local.x >= toggle - 4 && ctx.local.x <= toggle + 16)
        (void)services().perform(node(), "set_expanded", Value{}.set("key", l.key).set("open", !is_open(l.key)));
    else {
        (void)services().perform(node(), "set_value", Value{}.set("value", l.key));
        if (p->clicks >= 2) emit_activated(l.key);
    }
    services().focus(node());
    return true;
}

ActResult TreeView::choose(std::string key) {
    if (!key.empty() && (!model_ || !model_->find(key))) return ActResult::fail(std::format("no row `{}`", key));
    const std::string old = props.value;
    // A row inside closed ones: they open, so it is shown.
    for (std::string up = parent_of(key); !up.empty() && !is_open(up); up = parent_of(up)) {
        auto open = props.expanded;
        open.push_back(up);
        write_expanded(open);
        dirty();
    }
    if (key != props.value) {
        write_value(key);
        emit_changed(key);
    }
    if (const auto i = line_of(key)) show(*i);
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
    return ActResult::done(Value(props.value)).undo_by("set_value", Value{}.set("value", old));
}

ActResult TreeView::set_open(std::string key, bool open) {
    if (!model_ || !model_->find(key)) return ActResult::fail(std::format("no row `{}`", key));
    if (!has_children(key)) return ActResult::fail(std::format("`{}` has no rows under it", key));
    const bool was = is_open(key);
    if (open != was) {
        auto keys = props.expanded;
        if (open) keys.push_back(key);
        else std::erase(keys, key);
        write_expanded(keys);
        dirty();
        // Closed over the chosen row: the closed one is chosen instead.
        if (!open && !props.value.empty() && !line_of(props.value)) (void)choose(key);
    }
    return ActResult::done(Value(open)).undo_by("set_expanded", Value{}.set("key", key).set("open", was));
}

ActResult TreeView::set_value(ActContext&, const Value& value) { return choose(std::string(value.as_string())); }

ActResult TreeView::set_expanded(ActContext&, const SetExpandedArgs& args) {
    if (args.open.type() != Value::Type::boolean) return ActResult::fail("set_expanded takes a key and open, true or false");
    return set_open(std::string(args.key.as_string()), args.open.as_bool());
}

ActResult TreeView::next(ActContext&) {
    const auto& ls = lines();
    if (ls.empty()) return ActResult::done();
    const auto i = line_of(props.value);
    return choose(ls[i ? std::min(*i + 1, ls.size() - 1) : 0].key);
}

ActResult TreeView::previous(ActContext&) {
    const auto& ls = lines();
    if (ls.empty()) return ActResult::done();
    const auto i = line_of(props.value);
    return choose(ls[i && *i > 0 ? *i - 1 : 0].key);
}

ActResult TreeView::open(ActContext&) {
    if (props.value.empty() || !has_children(props.value)) return ActResult::done();
    if (!is_open(props.value)) return set_open(props.value, true);
    return choose(model_->at(kids_.at(props.value).front()).key);  // open already: to its first child
}

ActResult TreeView::close(ActContext&) {
    if (props.value.empty()) return ActResult::done();
    if (has_children(props.value) && is_open(props.value)) return set_open(props.value, false);
    const std::string up = parent_of(props.value);
    return up.empty() ? ActResult::done() : choose(up);  // closed already: to its parent
}

ActResult TreeView::activate(ActContext&) {
    if (!props.value.empty()) emit_activated(props.value);
    return ActResult::done();
}

spec::SemanticNode TreeView::item(std::string_view path, std::string_view key) const {
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, key);
    n.role = Role::tree_item;
    const auto row = model_->find(key);
    n.name = row ? label_of(*row) : std::string(key);
    const bool parenting = has_children(key), open = parenting && is_open(key);
    int depth = 0;
    for (std::string up = parent_of(key); !up.empty() && depth < 64; up = parent_of(up)) ++depth;
    n.value = Value{}.set("level", static_cast<std::int64_t>(depth + 1)).set("children",
                                                                              static_cast<std::int64_t>(parenting ? kids_.at(std::string(key)).size() : 0));
    n.states.set(State::selected, key == props.value);
    if (parenting) n.states.set(State::collapsible).set(State::expanded, open);
    const auto line = line_of(key);
    if (line && attached()) {
        const Rect me = services().bounds(node());
        const float top = tree_pad + static_cast<float>(*line) * tree_row - offset_;
        n.bounds = spec::Rect{me.x + tree_pad, me.y + top, me.width - 2 * tree_pad, tree_row};
        n.states.set(State::offscreen, top + tree_row <= 0 || top >= me.height);
    }
    n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    if (parenting) n.actions.push_back({open ? "collapse" : "expand", Effect::reversible, {}, {}, {}});
    return n;
}

void TreeView::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("value", props.value).set("rows", static_cast<std::int64_t>(model_ ? model_->size() : 0));
}

std::vector<spec::SemanticNode> TreeView::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    // The rows shown: the open ones' children are; the closed ones' are not.
    for (const Line& l : lines()) out.push_back(item(path, l.key));
    return out;
}

std::optional<spec::SemanticNode> TreeView::virtual_item(std::string_view path, std::string_view key) const {
    if (!model_ || !model_->find(key)) return std::nullopt;
    return item(path, key);  // any row: an agent chooses one inside closed ones, and they open
}

ActResult TreeView::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (action == "activate") {
        ActResult r = choose(std::string(key));
        emit_activated(std::string(key));
        return r;
    }
    if (action == "expand" || action == "collapse") return set_open(std::string(key), action == "expand");
    return act(ctx, action);  // an inverse taken back on the item (S-12): the tree's own action
}

// ── data grid ──

std::string DataGrid::text_of(const Value& v) {
    if (v.is_null()) return {};
    if (v.type() == Value::Type::boolean) return v.as_bool() ? "Yes" : "No";
    if (v.is_number()) {
        const double d = v.as_double();
        if (d == std::floor(d) && std::abs(d) < 1e15) return std::format("{}", static_cast<std::int64_t>(d));
        return std::format("{:.2f}", d);
    }
    if (v.list() || v.map()) return v.json();
    return std::string(v.as_string());
}

void DataGrid::bind_model() {
    model_ = props.model.empty() ? nullptr : &services().model(props.model);
    watch_ = Watch([this] {
        if (model_) (void)model_->revision();
        untracked([this] { dirty(); });
    });
}

void DataGrid::mounted() { bind_model(); }

void DataGrid::dirty() {
    dirty_ = true;
    if (!attached()) return;
    services().invalidate_layout(node());
    services().invalidate_semantics(node());
}

void DataGrid::prop_changed(DataGridProp p) {
    if (!attached()) return;
    if (p == DataGridProp::model) bind_model();
    if (p == DataGridProp::model || p == DataGridProp::sort) dirty();
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

const std::vector<std::size_t>& DataGrid::order() const {
    if (!dirty_) return order_;
    dirty_ = false;
    order_.clear();
    if (!model_) return order_;
    for (std::size_t i = 0; i < model_->size(); ++i) order_.push_back(i);
    if (props.sort.empty()) return order_;
    const bool down = props.sort.starts_with('-');
    const std::string field = down ? props.sort.substr(1) : props.sort;
    std::ranges::stable_sort(order_, [&](std::size_t a, std::size_t b) {
        static const Value none;
        const Value* x = model_->at(a).fields.get(field);
        const Value* y = model_->at(b).fields.get(field);
        const int c = compare(x ? *x : none, y ? *y : none);
        return down ? c > 0 : c < 0;
    });
    return order_;
}

std::string DataGrid::header_of(std::size_t c) const { return c < props.headers.size() ? props.headers[c] : props.fields[c]; }

std::vector<float> DataGrid::column_widths(float width) const {
    std::vector<float> w(props.fields.size(), -1);
    float fixed = 0;
    std::size_t flexible = 0;
    for (std::size_t c = 0; c < w.size(); ++c) {
        float px = 0;
        const std::string_view s = c < props.widths.size() ? std::string_view(props.widths[c]) : std::string_view();
        if (!s.empty() && s != "*" && std::from_chars(s.data(), s.data() + s.size(), px).ec == std::errc{} && px > 0) w[c] = px, fixed += px;
        else ++flexible;
    }
    const float share = flexible ? std::max(60.0f, (width - fixed) / static_cast<float>(flexible)) : 0;
    for (float& x : w)
        if (x < 0) x = share;
    return w;
}

Size DataGrid::measure(MeasureContext&, std::optional<float> width, std::optional<float> height, mlibs::layout::Available aw, mlibs::layout::Available) {
    const float rows = static_cast<float>(order().size());
    return {width.value_or(aw.is_definite() ? aw.value : 480.0f), height.value_or(grid_head + std::min(rows, 10.0f) * grid_row)};
}

bool DataGrid::chosen(std::string_view key) const { return std::ranges::find(props.selected, key) != props.selected.end(); }

void DataGrid::paint(PaintContext& ctx) const {
    const Theme& th = ctx.theme;
    size_ = ctx.size;
    const Rect box{0, 0, ctx.size.width, ctx.size.height};
    const CornerRadii r = CornerRadii::all(th.corner("medium", 8)).fitted(ctx.size);
    ctx.list.push_clip(box, r);
    ctx.list.fill_rect(box, th.color("surface"));
    const auto widths = column_widths(ctx.size.width);
    const auto& ord = order();
    // The rows in view, under the header.
    const std::size_t from = static_cast<std::size_t>(std::max(0.0f, offset_ / grid_row));
    const std::size_t to = std::min(ord.size(), static_cast<std::size_t>((offset_ + ctx.size.height - grid_head) / grid_row) + 1);
    const std::size_t cells = (to - std::min(from, to)) * props.fields.size();
    if (cells_.size() < cells) cells_.resize(cells);
    std::size_t k = 0;
    for (std::size_t i = from; i < to; ++i) {
        const Row& row = model_->at(ord[i]);
        const Rect rr{0, grid_head + static_cast<float>(i) * grid_row - offset_, ctx.size.width, grid_row};
        if (chosen(row.key)) ctx.list.fill_rect(rr, th.color("accent_subtle"));
        else if (hover_ == static_cast<int>(i)) ctx.list.fill_rect(rr, th.color("surface_variant"));
        ctx.list.fill_rect({0, rr.bottom() - 1, ctx.size.width, 1}, th.color("border"));
        float x = 0;
        for (std::size_t c = 0; c < props.fields.size(); ++c) {
            const Value* v = row.fields.get(props.fields[c]);
            parts::Label& l = cells_[k++];
            l.set_style("body");
            l.set_max_lines(1);
            l.set_text(v ? text_of(*v) : std::string());
            const Color tc = chosen(row.key) ? th.color("text") : th.color("text");
            const Size t = l.measure(ctx.services, tc, std::max(0.0f, widths[c] - 2 * grid_pad));
            // Numbers line up on the right.
            const float tx = v && v->is_number() ? x + widths[c] - grid_pad - t.width : x + grid_pad;
            l.paint(ctx, {tx, rr.center().y - t.height / 2}, tc);
            x += widths[c];
        }
    }
    // The header over them, each column's name and its sort.
    ctx.list.fill_rect({0, 0, ctx.size.width, grid_head}, th.color("surface_variant"));
    ctx.list.fill_rect({0, grid_head - 1, ctx.size.width, 1}, th.color("border_strong"));
    headers_.resize(props.fields.size());
    float x = 0;
    for (std::size_t c = 0; c < props.fields.size(); ++c) {
        parts::Label& h = headers_[c];
        h.set_style("label");
        h.set_max_lines(1);
        h.set_text(header_of(c));
        const bool by = props.sort == props.fields[c] || props.sort == "-" + props.fields[c];
        const Color hc = by ? th.color("text") : th.color("text_muted");
        const Size t = h.measure(ctx.services, hc, std::max(0.0f, widths[c] - 2 * grid_pad - 14));
        if (hover_ == -2 - static_cast<int>(c)) ctx.list.fill_rect({x, 0, widths[c], grid_head - 1}, th.color("surface_pressed").with_alpha(0.5f));
        h.paint(ctx, {x + grid_pad, (grid_head - t.height) / 2}, hc);
        if (by) triangle(ctx, {x + grid_pad + t.width + 9, grid_head / 2}, !props.sort.starts_with('-'), th.color("accent"));
        x += widths[c];
    }
    ctx.list.pop_clip();
    ctx.list.stroke_round_rect({0.5f, 0.5f, box.width - 1, box.height - 1}, r, 1, xrender::Paint::solid(th.color("border")));
}

void DataGrid::paint_over(PaintContext& ctx) const {
    if (!(ctx.state.focused && ctx.state.focus_visible) || order().empty()) return;
    const float top = grid_head + static_cast<float>(focus_) * grid_row - offset_;
    if (top < grid_head - 1 || top > ctx.size.height) return;
    parts::focus_ring(ctx, {2, top + 1, ctx.size.width - 4, grid_row - 2}, CornerRadii::all(4));
}

void DataGrid::show(std::size_t i) {
    const float top = static_cast<float>(i) * grid_row, view = size_.height - grid_head;
    if (view <= 0) return;
    if (top < offset_) offset_ = top;
    else if (top + grid_row > offset_ + view) offset_ = top + grid_row - view;
    services().invalidate_paint(node());
    services().invalidate_semantics(node());
}

std::string DataGrid::sort_after(std::string_view field) const {
    const std::string f(field);
    if (props.sort == f) return "-" + f;
    if (props.sort == "-" + f) return {};
    return f;
}

bool DataGrid::on_event(EventContext& ctx, const InputEvent& e) {
    if (const auto* w = std::get_if<WheelEvent>(&e)) {
        const float total = static_cast<float>(order().size()) * grid_row;
        offset_ = std::clamp(offset_ + w->delta.y, 0.0f, std::max(0.0f, total - (size_.height - grid_head)));
        ctx.repaint();
        services().invalidate_semantics(node());
        return true;
    }
    const auto* p = std::get_if<PointerEvent>(&e);
    if (!p) return false;
    int over = -1;
    const auto widths = column_widths(size_.width);
    if (ctx.local.y >= 0 && ctx.local.y < grid_head) {
        float x = 0;
        for (std::size_t c = 0; c < widths.size(); ++c) {
            if (ctx.local.x >= x && ctx.local.x < x + widths[c]) over = -2 - static_cast<int>(c);
            x += widths[c];
        }
    } else if (ctx.local.y >= grid_head) {
        const std::size_t i = static_cast<std::size_t>((ctx.local.y - grid_head + offset_) / grid_row);
        if (i < order().size()) over = static_cast<int>(i);
    }
    if (p->kind == PointerKind::leave) over = -1;
    if (over != hover_) hover_ = over, ctx.repaint();
    if (p->kind != PointerKind::up || over == -1 || (p->type == PointerType::mouse && p->button != MouseButton::left)) return p->kind == PointerKind::down && over != -1;
    services().focus(node());
    if (over <= -2) {
        const std::size_t c = static_cast<std::size_t>(-2 - over);
        (void)services().perform(node(), "set_sort", Value{}.set("sort", sort_after(props.fields[c])));
        return true;
    }
    const std::size_t i = static_cast<std::size_t>(over);
    const std::string key = model_->at(order()[i]).key;
    focus_ = i;
    // Several at once: Ctrl adds or takes out, Shift reaches from the row gone to before.
    std::vector<std::string> keys{key};
    if (props.multiple && (p->mods.has(Modifier::ctrl) || p->mods.has(Modifier::meta))) {
        keys = props.selected;
        if (chosen(key)) std::erase(keys, key);
        else keys.push_back(key);
    }
    (void)services().perform(node(), "set_selected", Value{}.set("keys", [&] {
        Value::List l;
        for (const std::string& s : keys) l.emplace_back(s);
        return Value(std::move(l));
    }()));
    if (p->clicks >= 2) emit_activated(key);
    return true;
}

ActResult DataGrid::select(std::vector<std::string> keys) {
    for (const std::string& k : keys)
        if (!model_ || !model_->find(k)) return ActResult::fail(std::format("no row `{}`", k));
    if (!props.multiple && keys.size() > 1) return ActResult::fail("one row at a time (it is not `multiple`)");
    const std::vector<std::string> old = props.selected;
    if (keys != props.selected) {
        write_selected(keys);
        services().invalidate_paint(node());
        services().invalidate_semantics(node());
    }
    Value::List was;
    for (const std::string& s : old) was.emplace_back(s);
    return ActResult::done().undo_by("set_selected", Value{}.set("keys", Value(std::move(was))));
}

ActResult DataGrid::sort_to(std::string sort) {
    const std::string field = sort.starts_with('-') ? sort.substr(1) : sort;
    if (!sort.empty() && std::ranges::find(props.fields, field) == props.fields.end())
        return ActResult::fail(std::format("`{}` is not one of its columns", field));
    const std::string old = props.sort;
    if (sort != props.sort) {
        // The row gone to stays the one gone to wherever the sort puts it.
        const std::string at = focus_ < order().size() ? model_->at(order()[focus_]).key : std::string();
        write_sort(sort);
        dirty();
        emit_sorted(sort);
        if (const auto i = position_of(at)) focus_ = *i;
        services().invalidate_paint(node());
    }
    return ActResult::done(Value(props.sort)).undo_by("set_sort", Value{}.set("sort", old));
}

ActResult DataGrid::set_sort(ActContext&, const Value& sort) { return sort_to(std::string(sort.as_string())); }
ActResult DataGrid::set_selected(ActContext&, const Value& keys) {
    if (!keys.list()) return ActResult::fail("set_selected takes a list of the rows' keys");
    return select(strings_of(keys));
}

std::optional<std::size_t> DataGrid::position_of(std::string_view key) const {
    if (!model_ || key.empty()) return std::nullopt;
    const auto& ord = order();
    for (std::size_t i = 0; i < ord.size(); ++i)
        if (model_->at(ord[i]).key == key) return i;
    return std::nullopt;
}

ActResult DataGrid::go(std::size_t i) {
    const auto& ord = order();
    if (ord.empty()) return ActResult::done();
    focus_ = std::min(i, ord.size() - 1);
    show(focus_);
    if (props.multiple) return ActResult::done();  // several: going is not choosing (Space chooses)
    return select({model_->at(ord[focus_]).key});
}

ActResult DataGrid::next(ActContext&) { return go(focus_ + 1); }
ActResult DataGrid::previous(ActContext&) { return go(focus_ > 0 ? focus_ - 1 : 0); }
ActResult DataGrid::first(ActContext&) { return go(0); }
ActResult DataGrid::last(ActContext&) { return go(order().empty() ? 0 : order().size() - 1); }

ActResult DataGrid::activate(ActContext&) {
    if (focus_ < order().size()) emit_activated(model_->at(order()[focus_]).key);
    return ActResult::done();
}

ActResult DataGrid::toggle(ActContext&) {
    if (focus_ >= order().size()) return ActResult::done();
    const std::string key = model_->at(order()[focus_]).key;
    if (!props.multiple) return select({key});
    auto keys = props.selected;
    if (chosen(key)) std::erase(keys, key);
    else keys.push_back(key);
    return select(std::move(keys));
}

spec::SemanticNode DataGrid::header_item(std::string_view path, std::size_t c) const {
    spec::SemanticNode n;
    n.id = std::format("{}/col_{}", path, props.fields[c]);
    n.role = Role::column_header;
    n.name = header_of(c);
    const std::string sort = props.sort == props.fields[c] ? "ascending" : props.sort == "-" + props.fields[c] ? "descending" : "none";
    n.value = Value{}.set("field", props.fields[c]).set("sort", sort);
    if (attached() && size_.width > 0) {
        const auto widths = column_widths(size_.width);
        float x = 0;
        for (std::size_t i = 0; i < c; ++i) x += widths[i];
        const Rect me = services().bounds(node());
        n.bounds = spec::Rect{me.x + x, me.y, widths[c], grid_head};
    }
    n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    return n;
}

spec::SemanticNode DataGrid::row_item(std::string_view path, std::size_t at) const {
    const Row& row = model_->at(order()[at]);
    spec::SemanticNode n;
    n.id = std::format("{}/{}", path, row.key);
    n.role = Role::row;
    const Value* first = props.fields.empty() ? nullptr : row.fields.get(props.fields.front());
    n.name = first ? text_of(*first) : row.key;
    Value cells = Value(Value::Map{});
    for (const std::string& c : props.fields)
        if (const Value* v = row.fields.get(c)) cells.set(c, *v);
    n.value = std::move(cells);
    n.states.set(State::selected, chosen(row.key));
    if (attached()) {
        const Rect me = services().bounds(node());
        const float top = grid_head + static_cast<float>(at) * grid_row - offset_;
        n.bounds = spec::Rect{me.x, me.y + top, me.width, grid_row};
        n.states.set(State::offscreen, top + grid_row <= grid_head || top >= me.height);
    }
    n.actions.push_back({"activate", Effect::reversible, {}, {}, {}});
    if (props.multiple) n.actions.push_back({"toggle", Effect::reversible, {}, {}, {}});
    n.actions.push_back({"scroll_into_view", Effect::read, {}, {}, {}});
    return n;
}

void DataGrid::semantics(SemanticsContext&, spec::SemanticNode& node) const {
    if (node.name.empty()) node.name = props.label;
    node.value = Value{}.set("rows", static_cast<std::int64_t>(order().size())).set("sort", props.sort).set("selected", static_cast<std::int64_t>(props.selected.size()));
    if (props.multiple) node.states.set(State::multiselectable);
}

std::vector<spec::SemanticNode> DataGrid::virtual_items(std::string_view path) const {
    std::vector<spec::SemanticNode> out;
    for (std::size_t c = 0; c < props.fields.size(); ++c) out.push_back(header_item(path, c));
    for (std::size_t i = 0; i < order().size(); ++i) out.push_back(row_item(path, i));
    return out;
}

std::optional<spec::SemanticNode> DataGrid::virtual_item(std::string_view path, std::string_view key) const {
    if (key.starts_with("col_")) {
        for (std::size_t c = 0; c < props.fields.size(); ++c)
            if (props.fields[c] == key.substr(4)) return header_item(path, c);
    }
    if (const auto i = position_of(key)) return row_item(path, *i);
    return std::nullopt;
}

ActResult DataGrid::act_item(ActContext& ctx, std::string_view key, std::string_view action) {
    if (key.starts_with("col_") && action == "activate") return sort_to(sort_after(key.substr(4)));
    if (const auto i = position_of(key)) {
        if (action == "activate") {
            focus_ = *i;
            ActResult r = select({std::string(key)});
            emit_activated(std::string(key));
            return r;
        }
        if (action == "toggle") {
            focus_ = *i;
            return toggle(ctx);
        }
        if (action == "scroll_into_view") {
            show(*i);
            return ActResult::done();
        }
    }
    return act(ctx, action);  // an inverse taken back on the item (S-12): the grid's own action
}

}  // namespace omui
