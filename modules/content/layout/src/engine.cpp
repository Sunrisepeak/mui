// The engine's nodes and caches, the box model, leaves, stacks and absolute
// positioning. Flexbox is in flex.cpp, grid in grid.cpp.
//
// Sizes are border-box sizes (CSS box-sizing: border-box, as UI toolkits use):
// `width` includes padding and border. Percentages resolve against the
// parent's content box and are auto where that is indefinite.
module openmui.layout;

import std;
import openmui.geom;
import :internal;

namespace omui::layout {

bool operator==(const Style& a, const Style& b) noexcept {
    auto same_tracks = [](const std::vector<Track>& x, const std::vector<Track>& y) {
        if (x.size() != y.size()) return false;
        for (std::size_t i = 0; i < x.size(); ++i)
            if (x[i].kind != y[i].kind || x[i].value != y[i].value || x[i].min_kind != y[i].min_kind ||
                x[i].min_value != y[i].min_value || x[i].max_kind != y[i].max_kind || x[i].max_value != y[i].max_value)
                return false;
        return true;
    };
    auto same_track = [&](const Track& x, const Track& y) { return same_tracks({x}, {y}); };
    return a.display == b.display && a.position == b.position && a.overflow_x == b.overflow_x &&
           a.overflow_y == b.overflow_y && a.width == b.width && a.height == b.height && a.min_width == b.min_width &&
           a.min_height == b.min_height && a.max_width == b.max_width && a.max_height == b.max_height &&
           a.aspect_ratio == b.aspect_ratio && a.margin == b.margin && a.padding == b.padding && a.border == b.border &&
           a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom &&
           a.direction == b.direction && a.wrap == b.wrap && a.justify == b.justify && a.align_items == b.align_items &&
           a.align_content == b.align_content && a.row_gap == b.row_gap && a.column_gap == b.column_gap &&
           a.grow == b.grow && a.shrink == b.shrink && a.basis == b.basis && a.align_self == b.align_self &&
           a.justify_self == b.justify_self && same_tracks(a.columns, b.columns) && same_tracks(a.rows, b.rows) &&
           same_track(a.auto_rows, b.auto_rows) && same_track(a.auto_columns, b.auto_columns) &&
           a.grid_flow == b.grid_flow && a.column_start.line == b.column_start.line &&
           a.column_start.span == b.column_start.span && a.row_start.line == b.row_start.line &&
           a.row_start.span == b.row_start.span;
}

Engine::Engine() = default;
Engine::~Engine() = default;

Engine::Node& Engine::at(NodeId id) { return nodes_[id]; }
const Engine::Node& Engine::at(NodeId id) const { return nodes_[id]; }

NodeId Engine::create(Style style) {
    NodeId id;
    if (!free_.empty()) {
        id = free_.back();
        free_.pop_back();
        nodes_[id] = Node{};
    } else {
        id = static_cast<NodeId>(nodes_.size());
        nodes_.emplace_back();
    }
    nodes_[id].style = std::move(style);
    return id;
}

bool Engine::valid(NodeId node) const noexcept { return node < nodes_.size() && nodes_[node].alive; }

void Engine::destroy(NodeId node) {
    if (!valid(node)) return;
    Node& n = at(node);
    if (n.parent != no_node) remove_child(n.parent, node);
    for (NodeId c : n.children) at(c).parent = no_node;
    n = Node{};
    n.alive = false;
    free_.push_back(node);
}

void Engine::set_style(NodeId node, Style style) {
    Node& n = at(node);
    if (n.style == style) return;
    n.style = std::move(style);
    mark_dirty(node);
}

const Style& Engine::style(NodeId node) const { return at(node).style; }

void Engine::append(NodeId parent, NodeId child) { insert(parent, at(parent).children.size(), child); }

void Engine::insert(NodeId parent, std::size_t index, NodeId child) {
    Node& c = at(child);
    if (c.parent != no_node) remove_child(c.parent, child);
    Node& p = at(parent);
    index = std::min(index, p.children.size());
    p.children.insert(p.children.begin() + static_cast<std::ptrdiff_t>(index), child);
    at(child).parent = parent;
    mark_dirty(parent);
}

void Engine::remove_child(NodeId parent, NodeId child) {
    Node& p = at(parent);
    if (std::erase(p.children, child) > 0) {
        at(child).parent = no_node;
        mark_dirty(parent);
    }
}

void Engine::clear_children(NodeId parent) {
    for (NodeId c : at(parent).children) at(c).parent = no_node;
    at(parent).children.clear();
    mark_dirty(parent);
}

std::span<const NodeId> Engine::children(NodeId node) const { return at(node).children; }
NodeId Engine::parent(NodeId node) const { return at(node).parent; }

void Engine::set_measure(NodeId node, MeasureFn measure, void* context, BaselineFn baseline) {
    Node& n = at(node);
    n.measure = measure;
    n.context = context;
    n.baseline_fn = baseline;
    mark_dirty(node);
}

void Engine::mark_dirty(NodeId node) {
    while (node != no_node) {
        Node& n = at(node);
        n.invalidate();
        node = n.parent;
    }
}

Rect Engine::frame(NodeId node) const { return at(node).frame; }
Size Engine::content_size(NodeId node) const { return at(node).content; }
float Engine::baseline(NodeId node) const { return at(node).baseline; }

void Engine::compute(NodeId root, Size available) {
    Request r;
    r.avail_width = Available::definite(available.width);
    r.avail_height = Available::definite(available.height);
    r.parent_width = available.width;
    r.parent_height = available.height;
    // The root's own style decides its size; an auto root fills the window.
    const Style& s = at(root).style;
    if (s.width.is_auto()) r.width = available.width;
    if (s.height.is_auto()) r.height = available.height;
    r.perform = true;
    Size size = layout(root, r);
    Node& n = at(root);
    n.frame = {0, 0, size.width, size.height};
}

// ── box model ─────────────────────────────────────────────────────────────

namespace {

constexpr float infinity = std::numeric_limits<float>::infinity();

float clamp_size(float v, std::optional<float> lo, std::optional<float> hi) {
    if (hi && v > *hi) v = *hi;
    if (lo && v < *lo) v = *lo;  // min wins over max (CSS)
    return v < 0 ? 0 : v;
}

}  // namespace

Size Engine::layout(NodeId node, const Request& request) {
    Node& n = at(node);
    // Served from cache: the same constraints as a previous request.
    if (request.perform) {
        if (n.performed && n.performed->request.same_constraints(request)) {
            ++stats_.cache_hits;
            return n.performed->size;
        }
    } else {
        for (std::uint8_t i = 0; i < n.cache_count; ++i)
            if (n.cache[i].request.same_constraints(request)) {
                ++stats_.cache_hits;
                return n.cache[i].size;
            }
        if (n.performed && n.performed->request.same_constraints(request)) {
            ++stats_.cache_hits;
            return n.performed->size;
        }
    }
    ++stats_.layouts;

    const Style& s = n.style;
    Request r = request;
    // The node's own definite sizes: the parent's decision, else the style.
    std::optional<float> min_w = s.min_width.resolve(r.parent_width);
    std::optional<float> max_w = s.max_width.resolve(r.parent_width);
    std::optional<float> min_h = s.min_height.resolve(r.parent_height);
    std::optional<float> max_h = s.max_height.resolve(r.parent_height);
    if (!r.width) r.width = s.width.resolve(r.parent_width);
    if (!r.height) r.height = s.height.resolve(r.parent_height);
    if (s.aspect_ratio > 0) {
        if (r.width && !r.height) r.height = *r.width / s.aspect_ratio;
        else if (r.height && !r.width) r.width = *r.height * s.aspect_ratio;
    }
    if (r.width) r.width = clamp_size(*r.width, min_w, max_w);
    if (r.height) r.height = clamp_size(*r.height, min_h, max_h);

    Size size;
    if (s.display == Display::none) {
        hide_subtree(node);
        size = {0, 0};
    } else if (n.children.empty() || n.measure) {
        size = layout_leaf(node, r);
    } else if (s.display == Display::grid) {
        size = layout_grid(node, r);
    } else if (s.display == Display::stack) {
        size = layout_stack(node, r);
    } else {
        size = layout_flex(node, r);
    }
    if (!r.width) size.width = clamp_size(size.width, min_w, max_w);
    if (!r.height) size.height = clamp_size(size.height, min_h, max_h);
    if (s.aspect_ratio > 0 && !r.width && !r.height) size.height = size.width / s.aspect_ratio;

    if (s.display != Display::none) layout_absolute(node, size, request.perform);

    Node& m = at(node);  // `nodes_` does not grow during layout; refresh the reference anyway
    Node::Entry entry{request, size, m.baseline};
    if (request.perform) {
        m.performed = entry;
    } else {
        m.cache[m.cache_next] = entry;
        m.cache_next = static_cast<std::uint8_t>((m.cache_next + 1) % Node::cache_slots);
        if (m.cache_count < Node::cache_slots) ++m.cache_count;
    }
    return size;
}

void Engine::hide_subtree(NodeId node) {
    for (NodeId c : at(node).children) {
        Node& n = at(c);
        n.frame = {};
        // The zeroed frames are not what its cache describes: shown again, it
        // must be laid out (and position its children) anew.
        n.invalidate();
        hide_subtree(c);
    }
}

Size Engine::layout_leaf(NodeId node, const Request& r) {
    Node& n = at(node);
    const Style& s = n.style;
    float pb_w = s.padding.horizontal() + s.border.horizontal();
    float pb_h = s.padding.vertical() + s.border.vertical();
    Size content{0, 0};
    if (n.measure) {
        auto inner = [](std::optional<float> v, float pb) -> std::optional<float> {
            if (!v) return std::nullopt;
            return std::max(0.0f, *v - pb);
        };
        auto inner_avail = [](Available a, float pb) {
            if (a.is_definite()) return Available::definite(std::max(0.0f, a.value - pb));
            return a;
        };
        if (r.width && r.height) {
            content = {*r.width - pb_w, *r.height - pb_h};
            // Still measured once when laid out, so the baseline is known.
            if (r.perform) {
                ++stats_.measures;
                content = n.measure(n.context, node, inner(r.width, pb_w), inner(r.height, pb_h),
                                    inner_avail(r.avail_width, pb_w), inner_avail(r.avail_height, pb_h));
            }
        } else {
            ++stats_.measures;
            content = n.measure(n.context, node, inner(r.width, pb_w), inner(r.height, pb_h),
                                inner_avail(r.avail_width, pb_w), inner_avail(r.avail_height, pb_h));
        }
    }
    Size size{r.width.value_or(content.width + pb_w), r.height.value_or(content.height + pb_h)};
    Node& m = at(node);
    m.content = content;
    m.baseline = s.border.top + s.padding.top +
                 (m.baseline_fn ? m.baseline_fn(m.context, node, {size.width - pb_w, size.height - pb_h})
                                : std::max(0.0f, size.height - pb_h));
    // A leaf with children (display: none excluded) only happens for a node with
    // a measure function; its children are not laid out by the engine.
    return size;
}

Size Engine::layout_stack(NodeId node, const Request& r) {
    Node& n = at(node);
    const Style& s = n.style;
    float pb_l = s.padding.left + s.border.left, pb_t = s.padding.top + s.border.top;
    float pb_w = s.padding.horizontal() + s.border.horizontal();
    float pb_h = s.padding.vertical() + s.border.vertical();
    std::optional<float> inner_w = r.width ? std::optional<float>(std::max(0.0f, *r.width - pb_w)) : std::nullopt;
    std::optional<float> inner_h = r.height ? std::optional<float>(std::max(0.0f, *r.height - pb_h)) : std::nullopt;
    auto avail = [](std::optional<float> inner, Available outer, float pb) {
        if (inner) return Available::definite(*inner);
        if (outer.is_definite()) return Available::definite(std::max(0.0f, outer.value - pb));
        return outer;
    };
    Available aw = avail(inner_w, r.avail_width, pb_w), ah = avail(inner_h, r.avail_height, pb_h);

    struct Item {
        NodeId id;
        Size   size;
        Align  h, v;
    };
    std::vector<Item> items;
    float content_w = 0, content_h = 0;
    for (NodeId c : std::vector<NodeId>(n.children)) {
        const Style& cs = at(c).style;
        if (cs.position == Position::absolute || cs.display == Display::none) {
            if (cs.display == Display::none) {
                at(c).frame = {};
                hide_subtree(c);
            }
            continue;
        }
        Align h = cs.justify_self == Align::auto_ ? s.align_items : cs.justify_self;
        Align v = cs.align_self == Align::auto_ ? s.align_items : cs.align_self;
        Request cr;
        cr.parent_width = inner_w;
        cr.parent_height = inner_h;
        cr.avail_width = aw.is_definite() ? Available::definite(std::max(0.0f, aw.value - cs.margin.horizontal())) : aw;
        cr.avail_height = ah.is_definite() ? Available::definite(std::max(0.0f, ah.value - cs.margin.vertical())) : ah;
        if (h == Align::stretch && cs.width.is_auto() && inner_w) cr.width = std::max(0.0f, *inner_w - cs.margin.horizontal());
        if (v == Align::stretch && cs.height.is_auto() && inner_h) cr.height = std::max(0.0f, *inner_h - cs.margin.vertical());
        Size cs_size = layout(c, cr);
        items.push_back({c, cs_size, h, v});
        content_w = std::max(content_w, cs_size.width + cs.margin.horizontal());
        content_h = std::max(content_h, cs_size.height + cs.margin.vertical());
    }
    float width = r.width.value_or(content_w + pb_w);
    float height = r.height.value_or(content_h + pb_h);
    float iw = std::max(0.0f, width - pb_w), ih = std::max(0.0f, height - pb_h);

    if (r.perform) {
        for (Item& it : items) {
            const Style& cs = at(it.id).style;
            // A stretched item that the container's own size now determines.
            Request cr;
            cr.parent_width = iw;
            cr.parent_height = ih;
            cr.avail_width = Available::definite(std::max(0.0f, iw - cs.margin.horizontal()));
            cr.avail_height = Available::definite(std::max(0.0f, ih - cs.margin.vertical()));
            cr.width = it.h == Align::stretch && cs.width.is_auto() ? std::optional<float>(std::max(0.0f, iw - cs.margin.horizontal()))
                                                                      : std::optional<float>(it.size.width);
            cr.height = it.v == Align::stretch && cs.height.is_auto() ? std::optional<float>(std::max(0.0f, ih - cs.margin.vertical()))
                                                                        : std::optional<float>(it.size.height);
            cr.perform = true;
            Size final_size = layout(it.id, cr);
            auto place = [](Align a, float space, float size, float m0, float m1) {
                switch (a) {
                    case Align::end: return space - size - m1;
                    case Align::center: return m0 + (space - m0 - m1 - size) / 2;
                    default: return m0;
                }
            };
            float x = pb_l + place(it.h, iw, final_size.width, cs.margin.left, cs.margin.right);
            float y = pb_t + place(it.v, ih, final_size.height, cs.margin.top, cs.margin.bottom);
            at(it.id).frame = {x, y, final_size.width, final_size.height};
        }
        Node& m = at(node);
        m.content = {content_w, content_h};
        m.baseline = items.empty() ? height : at(items.front().id).frame.y + at(items.front().id).baseline;
    }
    return {width, height};
}

void Engine::layout_absolute(NodeId node, Size border_box, bool perform) {
    const Style& s = at(node).style;
    float pad_l = s.border.left, pad_t = s.border.top;
    float pad_w = std::max(0.0f, border_box.width - s.border.horizontal());
    float pad_h = std::max(0.0f, border_box.height - s.border.vertical());
    for (NodeId c : std::vector<NodeId>(at(node).children)) {
        const Style& cs = at(c).style;
        if (cs.position != Position::absolute || cs.display == Display::none) continue;
        auto l = cs.left.resolve(pad_w), rgt = cs.right.resolve(pad_w);
        auto t = cs.top.resolve(pad_h), btm = cs.bottom.resolve(pad_h);
        Request cr;
        cr.parent_width = pad_w;
        cr.parent_height = pad_h;
        cr.width = cs.width.resolve(pad_w);
        cr.height = cs.height.resolve(pad_h);
        if (!cr.width && l && rgt) cr.width = std::max(0.0f, pad_w - *l - *rgt - cs.margin.horizontal());
        if (!cr.height && t && btm) cr.height = std::max(0.0f, pad_h - *t - *btm - cs.margin.vertical());
        cr.avail_width = Available::definite(pad_w);
        cr.avail_height = Available::definite(pad_h);
        cr.perform = perform;
        Size sz = layout(c, cr);
        if (!perform) continue;
        float x = l ? *l + cs.margin.left
                    : (rgt ? pad_w - *rgt - cs.margin.right - sz.width : s.padding.left + cs.margin.left);
        float y = t ? *t + cs.margin.top
                    : (btm ? pad_h - *btm - cs.margin.bottom - sz.height : s.padding.top + cs.margin.top);
        at(c).frame = {pad_l + x, pad_t + y, sz.width, sz.height};
    }
}

}  // namespace omui::layout
