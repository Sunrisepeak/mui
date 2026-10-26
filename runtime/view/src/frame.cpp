// A frame (§3.1 of the plan): input → handlers and actions → signals →
// regions rebuilt → animations → layout (incremental) → paint.
module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import openmui.render;
import openmui.text;
import openmui.trace;
import :impl;

namespace omui {

namespace {

constexpr float degrees_to_radians = static_cast<float>(std::numbers::pi / 180.0);

Insets content_insets_of(const layout::Style& s) {
    return {s.padding.top + s.border.top, s.padding.right + s.border.right, s.padding.bottom + s.border.bottom,
            s.padding.left + s.border.left};
}

}  // namespace

namespace {

// A phase of the frame: a trace span, and its time added to `out` (ms).
struct Phase {
    trace::Span                           span;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    double&                               out;
    Phase(const char* name, double& o) : span(name, "frame"), out(o) {}
    ~Phase() { out += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count(); }
    Phase(const Phase&) = delete;
    Phase& operator=(const Phase&) = delete;
};


}  // namespace

FrameOutput Runtime::frame(const FrameInput& input) {
    Impl& d = *impl_;
    FrameTimings& t = d.stats.timings;
    const auto started = std::chrono::steady_clock::now();
    trace::Span whole("frame", "frame");
    if (input.time) d.clock.set(*input.time);
    else if (d.clock.kind() == anim::ClockKind::interactive) d.clock.begin_frame();
    d.frame_requested = false;

    {
        Phase p("timers", t.timers);
        d.queue.drain();
        d.run_timers();
    }
    {
        Phase p("input", t.input);
        for (const InputEvent& e : input.events) batch([&] { d.dispatch(e); });
    }
    {
        Phase p("regions", t.regions);
        d.reconcile_regions();
    }
    {
        Phase p("animations", t.animations);
        d.run_animations();
        d.finish_removals();
        d.rebuild_paths();
    }
    const Stages stages = d.options.stages;
    if (stages >= Stages::layout) {
        Phase p("layout", t.layout);
        d.run_layout();
    }

    // What painting asks for (a scroll view still gliding) is for the next frame.
    const std::uint8_t todo = d.dirty;
    d.dirty = dirty_none;
    bool repainted = false;
    const bool debugging = d.options.debug.bounds || d.options.debug.repaints || d.options.debug.inspector || d.options.debug.hud;
    if (stages >= Stages::layout && ((todo & (dirty_paint | dirty_layout | dirty_tree)) || debugging)) {
        {
            Phase p("bounds", t.bounds);
            d.compute_bounds(d.root, {0, 0}, Affine{});
            d.place_overlays();
            d.trap_focus();
            for (NodeId w : std::vector<NodeId>(d.watchers))
                if (d.alive(w)) d.at(w).type->laid_out(d.at(w).instance);
        }
        if (stages >= Stages::paint) {
            Phase p("paint", t.paint);
            d.paint();
            if (debugging) d.paint_debug();
            repainted = true;
        }
    }
    // Nodes that lived through a frame are shown: their changes may animate
    // from now on (at every stage alike, so H0, H1 and H2 agree).
    for (NodeId id : d.fresh)
        if (d.alive(id)) d.at(id).shown = true;
    d.fresh.clear();

    d.stats.nodes = d.nodes.size();
    for (NodeId id : d.animating) d.stats.animating += static_cast<std::size_t>(std::popcount(d.at(id).moving));
    trace::count("nodes", static_cast<double>(d.stats.nodes), "frame");
    t.total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (d.options.debug.slow_frame_ms > 0 && t.total > d.options.debug.slow_frame_ms && d.now() - d.last_slow_log > seconds(1)) {
        d.last_slow_log = d.now();
        constexpr Log frame_log{"frame"};
        frame_log.warn("a slow frame: {:.2f} ms (timers {:.2f}, input {:.2f}, regions {:.2f}, animations {:.2f}, layout {:.2f}, "
                       "bounds {:.2f}, paint {:.2f}); {} nodes, {} laid out, {} painted",
                       t.total, t.timers, t.input, t.regions, t.animations, t.layout, t.bounds, t.paint, d.stats.nodes,
                       d.stats.laid_out, d.stats.painted);
    }
    const bool more = !d.animating.empty() || !d.removing.empty() || d.frame_requested || !d.dirty_regions.empty() ||
                      d.options.debug.hud || !d.flashes.empty();
    // Bindings that ran since the last frame count toward this one.
    FrameStats stats = std::exchange(d.stats, {});
    d.history.push(stats);
    return FrameOutput{&d.list, repainted, more, d.semantic_revision, stats, next_timer()};
}

std::vector<FrameStats> Runtime::frame_history() const {
    std::vector<FrameStats> out;
    out.reserve(impl_->history.size());
    for (std::size_t i = 0; i < impl_->history.size(); ++i) out.push_back(impl_->history[i]);
    return out;
}

void Runtime::set_debug(DebugOptions debug) {
    impl_->options.debug = debug;
    impl_->touch(dirty_paint);
}

const DebugOptions& Runtime::debug() const noexcept { return impl_->options.debug; }

std::optional<seconds> Runtime::next_timer() const noexcept {
    std::optional<seconds> next;
    for (const Impl::Timer& t : impl_->timers) next = next ? std::min(*next, t.due) : t.due;
    return next;
}

void Runtime::Impl::run_timers() {
    // Due timers in order of due time, then of creation; one a timer adds
    // runs at a later frame at the earliest.
    const seconds t = now();
    std::vector<Timer> due;
    for (auto it = timers.begin(); it != timers.end();) {
        if (it->due <= t) {
            due.push_back(std::move(*it));
            it = timers.erase(it);
        } else {
            ++it;
        }
    }
    std::ranges::sort(due, [](const Timer& a, const Timer& b) { return a.due != b.due ? a.due < b.due : a.token < b.token; });
    for (Timer& x : due) batch([&] { x.fn(); });
}

std::uint64_t Runtime::after(seconds delay, std::function<void()> fn) {
    Impl& d = *impl_;
    const std::uint64_t token = d.next_token++;
    d.timers.push_back({d.now() + std::max(delay, seconds(0)), token, std::move(fn)});
    d.frame_requested = true;
    return token;
}

void Runtime::cancel(std::uint64_t timer) {
    std::erase_if(impl_->timers, [&](const Impl::Timer& t) { return t.token == timer; });
}

void Runtime::Impl::run_animations() {
    if (animating.empty()) return;
    const seconds t = now();
    const anim::Policy policy{reduce_motion};
    std::vector<NodeId> still;
    for (NodeId id : animating) {
        if (!alive(id)) continue;
        Node& n = at(id);
        for (std::size_t i = 0; i < field_count; ++i) {
            const std::uint32_t bit = 1u << i;
            if (!(n.moving & bit)) continue;
            const anim::Set::Channel c = n.channels[i];
            if (!anims.valid(c)) {
                n.moving &= ~bit;
                continue;
            }
            if (reduce_motion || anims.settled(c, t)) {
                n.visual.f[i] = static_cast<float>(anims.target(c));
                n.moving &= ~bit;
            } else {
                n.visual.f[i] = static_cast<float>(anims.value(c, t, policy));
            }
        }
        if (n.moving) still.push_back(id);
        else if (n.state.animating) {
            n.state.animating = false;
            // Settled: readers waiting for it see a new revision (no further frame is needed).
            dirty |= dirty_semantics;
            ++semantic_revision;
        }
    }
    animating = std::move(still);
    dirty |= dirty_paint;
}

void Runtime::Impl::run_layout() {
    stats.laid_out = 0;
    if (!(dirty & dirty_layout)) return;
    layout.reset_stats();
    layout.compute(at(root).layout, size);
    stats.laid_out = layout.stats().layouts;
    dirty |= dirty_paint | dirty_semantics;
    ++semantic_revision;  // bounds moved
}

bool Runtime::Impl::displayed(NodeId id) const {
    const Node& n = at(id);
    return n.visual.visible && layout.style(n.layout).display != layout::Display::none;
}

bool Runtime::Impl::contains(NodeId ancestor, NodeId id) const {
    for (NodeId c = id; alive(c); c = at(c).parent)
        if (c == ancestor) return true;
    return false;
}

NodeId Runtime::Impl::topmost_modal() const {
    for (auto it = overlays.rbegin(); it != overlays.rend(); ++it) {
        const Node& n = at(*it);
        if (displayed(*it) && n.type->placement && n.type->placement(n.instance).modal) return *it;
    }
    return {};
}

void Runtime::Impl::trap_focus() {
    // A modal overlay that opened takes focus (its first focusable node);
    // one that closed gives it back to where it was.
    while (!modal_focus.empty()) {
        const NodeId m = modal_focus.back().first;
        if (alive(m) && displayed(m)) break;
        const NodeId before = modal_focus.back().second;
        modal_focus.pop_back();
        if (alive(before)) set_focus(before, focus_visible);
    }
    const NodeId modal = topmost_modal();
    if (!modal || (!modal_focus.empty() && modal_focus.back().first == modal)) return;
    modal_focus.emplace_back(modal, focus);
    if (!contains(modal, focus)) {
        const std::vector<NodeId> order = focus_order();
        set_focus(order.empty() ? NodeId{} : order.front(), focus_visible);
    }
}

void Runtime::Impl::place_overlays() {
    const Rect window{0, 0, size.width, size.height};
    for (NodeId id : overlays) {
        if (!alive(id) || !displayed(id)) continue;
        Node& n = at(id);
        const Placement p = n.type->placement ? n.type->placement(n.instance) : Placement{};
        const Rect frame = layout.frame(n.layout);
        Rect anchor = alive(n.parent) ? at(n.parent).bounds : window;
        if (p.anchor == Placement::Anchor::previous && alive(n.parent)) {
            NodeId before;
            for (NodeId c : at(n.parent).children) {
                if (c == id) break;
                if (!at(c).type->overlay) before = c;
            }
            if (alive(before)) anchor = at(before).bounds;
        }
        const float w = frame.width, h = frame.height;
        Point at{};
        switch (p.side) {
            case Placement::Side::center: at = {(window.width - w) / 2, (window.height - h) / 2}; break;
            case Placement::Side::below:
            case Placement::Side::above: {
                const float below = anchor.bottom() + p.offset, above = anchor.y - p.offset - h;
                const bool down = p.side == Placement::Side::below ? (below + h <= window.bottom() || above < 0)
                                                                   : !(above >= 0 || below + h > window.bottom());
                at = {anchor.x, down ? below : above};
                break;
            }
            case Placement::Side::start:
            case Placement::Side::end: {
                const float after = anchor.right() + p.offset, before = anchor.x - p.offset - w;
                const bool right = p.side == Placement::Side::end ? (after + w <= window.right() || before < 0)
                                                                  : !(before >= 0 || after + w > window.right());
                at = {right ? after : before, anchor.y};
                break;
            }
        }
        // Inside the window, as far as it fits.
        at.x = std::clamp(at.x, 0.0f, std::max(0.0f, window.width - w));
        at.y = std::clamp(at.y, 0.0f, std::max(0.0f, window.height - h));
        compute_bounds(id, {0, 0}, Affine{}, at);
        if (p.match_width && w < anchor.width) {
            sync_style(id);  // the anchor's width became known: lay out again next frame
            frame_requested = true;
        }
    }
}

void Runtime::Impl::compute_bounds(NodeId id, Point origin, const Affine& parent_world, std::optional<Point> placed) {
    Node& n = at(id);
    n.frame = layout.frame(n.layout);
    if (placed) {
        n.frame.x = placed->x;
        n.frame.y = placed->y;
    }
    n.bounds = {origin.x + n.frame.x, origin.y + n.frame.y, n.frame.width, n.frame.height};
    n.content_offset = n.type->scroll_offset ? n.type->scroll_offset(n.instance) : Point{};

    // The local transform: the frame's offset, then the visual transforms about the centre.
    const Visual& v = n.visual;
    const Size size = n.frame.size();
    Affine local = Affine::translate(n.frame.x + v[Field::translate_x], n.frame.y + v[Field::translate_y]);
    const float s = v[Field::scale];
    const float rotation = v[Field::rotation];
    if (s != 1 || rotation != 0) {
        const float cx = size.width / 2, cy = size.height / 2;
        Affine m = Affine::translate(cx, cy);
        if (rotation != 0) m = m * render::rotate(rotation * degrees_to_radians);
        if (s != 1) m = m * Affine::scale(s, s);
        local = local * m * Affine::translate(-cx, -cy);
    }
    n.local = local;
    n.world = parent_world * local;

    const Point off = n.content_offset;
    const Point inner{n.bounds.x - off.x, n.bounds.y - off.y};
    const Affine child_world = (off.x != 0 || off.y != 0) ? n.world * Affine::translate(-off.x, -off.y) : n.world;
    for (std::size_t i = 0; i < at(id).children.size(); ++i) {
        const NodeId c = at(id).children[i];
        if (!at(c).type->overlay) compute_bounds(c, inner, child_world);
    }
}

void Runtime::Impl::paint() {
    list.clear();
    paint_node(root);
    // Overlays above everything, bottom to top; a modal one darkens what is below.
    for (NodeId id : overlays) {
        if (!alive(id) || !displayed(id)) continue;
        const Node& n = at(id);
        if (n.type->placement && n.type->placement(n.instance).scrim) {
            const Color scrim = theme.color("scrim", Color::hex(0x0f172a, 0.45f));
            list.fill_rect({0, 0, size.width, size.height}, scrim.with_alpha(scrim.a * std::clamp(n.visual[Field::opacity], 0.0f, 1.0f)));
        }
        paint_node(id);
    }
}

void Runtime::Impl::paint_node(NodeId id) {
    Node& n = at(id);
    const Visual& v = n.visual;
    const Size size = n.frame.size();
    const Affine local = n.local;
    const float s = v[Field::scale];
    if (!v.visible || v[Field::opacity] <= 0 || s == 0) return;
    if (layout.style(n.layout).display == layout::Display::none) return;
    ++stats.painted;

    list.begin_node(n.index);
    list.push_transform(local);
    const bool layer = v[Field::opacity] < 1;
    const Rect box{0, 0, size.width, size.height};
    if (layer) list.push_layer(v[Field::opacity], render::BlendMode::normal, {}, {}, box);

    const CornerRadii radii = v.corners(size);
    if (!v.shadow.empty())
        if (auto it = theme.shadows.find(v.shadow); it != theme.shadows.end())
            list.box_shadow(box, radii, it->second.offset, it->second.blur, it->second.spread, it->second.color);
    if (const Color bg = v.background(); bg.a > 0) {
        if (radii.zero()) list.fill_rect(box, bg);
        else list.fill_round_rect(box, radii, render::Paint::solid(bg));
    }
    if (const float bw = v[Field::border_width]; bw > 0)
        if (const Color bc = v.border(); bc.a > 0) {
            const float h = bw / 2;
            CornerRadii inner = radii;
            for (float* r : {&inner.top_left, &inner.top_right, &inner.bottom_right, &inner.bottom_left}) *r = std::max(0.0f, *r - h);
            list.stroke_round_rect({h, h, size.width - bw, size.height - bw}, inner, bw, render::Paint::solid(bc));
        }

    const Insets in = content_insets_of(layout.style(n.layout));
    const Rect content{in.left, in.top, std::max(0.0f, size.width - in.horizontal()), std::max(0.0f, size.height - in.vertical())};
    const ComponentType& type = *n.type;
    const bool clip = v.clip || type.clips;
    if (clip) list.push_clip(box, radii);
    if (type.paint) {
        PaintContext ctx{self, list, id, size, content, n.state, theme};
        type.paint(n.instance, ctx);
    }
    const Point off = n.content_offset;
    const bool shifted = off.x != 0 || off.y != 0;
    if (shifted) list.push_transform(Affine::translate(-off.x, -off.y));
    for (std::size_t i = 0; i < at(id).children.size(); ++i) {
        const NodeId c = at(id).children[i];
        if (!at(c).type->overlay) paint_node(c);  // overlays paint in their own pass
    }
    if (shifted) list.pop_transform();

    Node& again = at(id);
    if (type.paint_over) {
        PaintContext ctx{self, list, id, size, content, again.state, theme};
        type.paint_over(again.instance, ctx);
    }
    if (clip) list.pop_clip();

    // The focus ring of nodes that draw none themselves (keyboard focus only).
    if (again.state.focused && again.state.focus_visible && !type.paint_over) {
        auto ring = again.props.find("focus_ring");
        if (ring == again.props.end() || ring->second.as_bool(true)) {
            constexpr float gap = 2, width = 2;
            CornerRadii r = radii;
            for (float* x : {&r.top_left, &r.top_right, &r.bottom_right, &r.bottom_left}) *x += gap + width / 2;
            list.stroke_round_rect({-gap - width / 2, -gap - width / 2, size.width + 2 * gap + width, size.height + 2 * gap + width},
                                   r, width, render::Paint::solid(theme.color("focus", Color::hex(0x6366f1, 0.55f))));
        }
    }

    if (layer) list.pop_layer();
    list.pop_transform();
    list.end_node();
}

NodeId Runtime::Impl::hit(NodeId id, Point window, std::optional<Rect>) const {
    const Node& n = at(id);
    if (!n.visual.visible || n.visual[Field::opacity] <= 0 || n.state.removing) return {};
    if (layout.style(n.layout).display == layout::Display::none) return {};
    const Point p = local_point(n, window);
    const bool inside = p.x >= 0 && p.y >= 0 && p.x < n.frame.width && p.y < n.frame.height;
    if ((n.visual.clip || n.type->clips) && !inside) return {};
    for (std::size_t i = n.children.size(); i-- > 0;) {
        if (at(n.children[i]).type->overlay) continue;
        if (NodeId h = hit(n.children[i], window, std::nullopt)) return h;
    }
    if (inside && n.visual.hit) return id;
    return {};
}

NodeId Runtime::hit_test(Point window) const {
    const Impl& d = *impl_;
    // Overlays first, top to bottom; a modal one hides what is below it.
    for (auto it = d.overlays.rbegin(); it != d.overlays.rend(); ++it) {
        if (!d.alive(*it) || !d.displayed(*it)) continue;
        if (NodeId h = d.hit(*it, window, std::nullopt)) return h;
        const Node& o = d.at(*it);
        if (o.type->placement && o.type->placement(o.instance).modal) return *it;
    }
    NodeId h = d.hit(d.root, window, std::nullopt);
    return h ? h : d.root;
}

Insets Runtime::content_insets(NodeId node) const {
    if (!valid(node)) return {};
    return content_insets_of(impl_->layout.style(impl_->at(node).layout));
}

Size Runtime::content_size(NodeId node) const {
    if (!valid(node)) return {};
    return impl_->layout.content_size(impl_->at(node).layout);
}

}  // namespace omui
