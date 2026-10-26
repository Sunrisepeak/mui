// A frame (§3.1 of the plan): input → handlers and actions → signals →
// regions rebuilt → animations → layout (incremental) → paint.
module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import openmui.render;
import :impl;

namespace omui {

namespace {

constexpr float degrees_to_radians = static_cast<float>(std::numbers::pi / 180.0);

Insets content_insets_of(const layout::Style& s) {
    return {s.padding.top + s.border.top, s.padding.right + s.border.right, s.padding.bottom + s.border.bottom,
            s.padding.left + s.border.left};
}

}  // namespace

FrameOutput Runtime::frame(const FrameInput& input) {
    Impl& d = *impl_;
    if (input.time) d.clock.set(*input.time);
    else if (d.clock.kind() == anim::ClockKind::interactive) d.clock.begin_frame();
    d.frame_requested = false;

    d.queue.drain();
    d.run_timers();
    for (const InputEvent& e : input.events) batch([&] { d.dispatch(e); });
    d.reconcile_regions();
    d.run_animations();
    d.finish_removals();
    d.rebuild_paths();
    d.run_layout();

    // What painting asks for (a scroll view still gliding) is for the next frame.
    const std::uint8_t todo = d.dirty;
    d.dirty = dirty_none;
    bool repainted = false;
    if (todo & (dirty_paint | dirty_layout | dirty_tree)) {
        d.compute_bounds(d.root, {0, 0});
        d.paint();
        repainted = true;
    }

    d.stats.nodes = d.nodes.size();
    for (NodeId id : d.animating) d.stats.animating += static_cast<std::size_t>(std::popcount(d.at(id).moving));
    const bool more = !d.animating.empty() || !d.removing.empty() || d.frame_requested || !d.dirty_regions.empty();
    // Bindings that ran since the last frame count toward this one.
    FrameStats stats = std::exchange(d.stats, {});
    std::optional<seconds> next;
    for (const Impl::Timer& t : d.timers) next = next ? std::min(*next, t.due) : t.due;
    return FrameOutput{d.list, repainted, more, d.semantic_revision, stats, next};
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
            dirty |= dirty_semantics;
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

void Runtime::Impl::compute_bounds(NodeId id, Point origin) {
    Node& n = at(id);
    n.frame = layout.frame(n.layout);
    n.bounds = {origin.x + n.frame.x, origin.y + n.frame.y, n.frame.width, n.frame.height};
    n.content_offset = n.type->scroll_offset ? n.type->scroll_offset(n.instance) : Point{};
    const Point inner{n.bounds.x - n.content_offset.x, n.bounds.y - n.content_offset.y};
    for (std::size_t i = 0; i < at(id).children.size(); ++i) compute_bounds(at(id).children[i], inner);
}

void Runtime::Impl::paint() {
    list.clear();
    paint_node(root, Affine{});
}

void Runtime::Impl::paint_node(NodeId id, const Affine& parent_world) {
    Node& n = at(id);
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
    n.world = parent_world * local;
    n.shown = true;
    if (!v.visible || v[Field::opacity] <= 0 || s == 0) return;
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
    const Affine child_world = shifted ? at(id).world * Affine::translate(-off.x, -off.y) : at(id).world;
    for (std::size_t i = 0; i < at(id).children.size(); ++i) paint_node(at(id).children[i], child_world);
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
    const Point p = local_point(n, window);
    const bool inside = p.x >= 0 && p.y >= 0 && p.x < n.frame.width && p.y < n.frame.height;
    if ((n.visual.clip || n.type->clips) && !inside) return {};
    for (std::size_t i = n.children.size(); i-- > 0;)
        if (NodeId h = hit(n.children[i], window, std::nullopt)) return h;
    if (inside && n.visual.hit) return id;
    return {};
}

NodeId Runtime::hit_test(Point window) const {
    NodeId h = impl_->hit(impl_->root, window, std::nullopt);
    return h ? h : impl_->root;
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
