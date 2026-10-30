// A frame (§3.1 of the plan): input → handlers and actions → signals →
// regions rebuilt → animations → layout (incremental) → paint.
module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import openmui.render.vector;
import openmui.text;
import openmui.trace;
import openmui.pipeline;
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
    FrameTimings& t = d.stats.timings;
    const auto started = std::chrono::steady_clock::now();
    trace::Span whole("frame", "frame");
    if (input.time) d.clock.set(*input.time);
    else if (d.clock.kind() == hanim::ClockKind::interactive) d.clock.begin_frame(input.present_at.value_or(started));
    d.apply_time_control();
    if (input.display_hz > 0) d.display_hz = input.display_hz;
    if (d.fading) d.touch(dirty_paint);  // the cross-fade moves every frame
    d.reconcile_outlets();  // views move to the slots that claim them (Services::outlet)
    d.frame_requested = false;
    d.todo = 0;
    d.dirty_taken = d.bounds_ran = d.repainted = false;

    // The stages (stages.cpp): timers → input → regions → animations →
    // layout → bounds → paint → debug, or what a custom pipeline has.
    d.resources.put(frame_resource::input, const_cast<FrameInput*>(&input));
    d.resources.put(frame_resource::tree, &d.tree_token);
    if (auto r = d.frames->run(d.resources); !r) {
        constexpr Log frame_log{"frame"};
        frame_log.error("{}", r.error());
    }
    if (!d.dirty_taken) d.dirty = dirty_none;  // no bounds stage (H0): nothing waits for the changes
    for (const pipeline::StageTime& st : d.frames->timings()) {
        double* into = st.name == "timers" ? &t.timers
                     : st.name == "input" ? &t.input
                     : st.name == "regions" ? &t.regions
                     : st.name == "animations" ? &t.animations
                     : st.name == "layout" ? &t.layout
                     : st.name == "bounds" ? &t.bounds
                     : st.name == "paint" || st.name == "debug" ? &t.paint
                     : &t.other;
        *into += st.ms;
    }
    const bool repainted = d.repainted;
    if (repainted) d.drawn[d.drawn_next++ % d.drawn.size()] = std::chrono::steady_clock::now();
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
                       "bounds {:.2f}, paint {:.2f}, other stages {:.2f}); {} nodes, {} laid out, {} painted",
                       t.total, t.timers, t.input, t.regions, t.animations, t.layout, t.bounds, t.paint, t.other, d.stats.nodes,
                       d.stats.laid_out, d.stats.painted);
    }
    const bool more = !d.animating.empty() || !d.removing.empty() || d.frame_requested || !d.dirty_regions.empty() || d.fading.has_value() ||
                      d.options.debug.hud || !d.flashes.empty();
    // What frames are wanted for from now on (D-70).
    {
        // Input that moves something (a drag, the wheel, a touch) keeps the
        // display at its fastest a little after, so a gesture's end is smooth.
        for (const InputEvent& e : input.events) {
            const auto* p = std::get_if<PointerEvent>(&e);
            if (std::holds_alternative<WheelEvent>(e) || (p && (p->buttons != 0 || p->type == PointerType::touch)))
                d.input_until = d.now() + seconds(0.3);
        }
        FrameDemand want;
        if (!d.animating.empty() || d.fading) want = merge(want, FrameDemand::fastest("animation"));
        if (d.now() < d.input_until) want = merge(want, FrameDemand::fastest("input"));
        for (const auto& [token, lease] : d.leases) want = merge(want, lease);
        // A timer: as often as it is due (a caret's blink), at most 60 a second.
        if (const auto next = next_timer(); next && want.idle()) {
            const double in = (*next - d.now()).count();
            if (in < 2.0) {
                const double hz = std::min(60.0, 1.0 / std::max(in, 1.0 / 60));
                want = merge(want, FrameDemand{0, hz, hz, "timer"});
            }
        }
        d.demand = std::move(want);
        trace::count("frame.demand_hz", d.demand.idle() ? 0.0 : std::min(d.demand.wanted(), 1000.0), "frame.pacing");
    }
    // Bindings that ran since the last frame count toward this one.
    FrameStats stats = std::exchange(d.stats, {});
    d.history.push(stats);
    return FrameOutput{&d.list, repainted, more, d.semantic_revision, stats, next_timer(), d.demand};
}

pipeline::Pipeline& Runtime::frame_pipeline() noexcept { return *impl_->frames; }

void Runtime::Impl::take_time_control() {
    if (time_control.controlled) return;
    time_control.controlled = true;
    time_control.host_last = clock.now();
    time_control.shown = clock.now();
}

void Runtime::Impl::apply_time_control() {
    TimeControl& tc = time_control;
    if (!tc.controlled) return;
    const seconds host = clock.now();
    const seconds delta = host - tc.host_last;
    tc.host_last = host;
    if (!tc.paused) tc.shown += delta * tc.rate;
    bool moved = false;
    if (tc.step != seconds{0}) {
        tc.shown += std::exchange(tc.step, seconds{0});
        moved = true;
    }
    if (tc.seek) {
        tc.shown = *tc.seek;
        tc.seek.reset();
        moved = true;
    }
    if (moved) retime();
}

void Runtime::Impl::retime() {
    // Channels are analytic in time: sampling at another time is the state
    // at that time, earlier or later.
    const seconds t = now();
    const hanim::Policy policy{reduce_motion};
    // Values to apply once every node is visited (applying can reach a component).
    std::vector<std::tuple<NodeId, std::string, Value>> rested;
    for (std::size_t index = 1; index < by_index.size(); ++index) {
        const NodeId id = by_index[index];
        if (!alive(id)) continue;
        Node& n = at(id);
        bool any = false;
        for (std::size_t i = 0; i < field_count; ++i) {
            const hanim::Set::Channel c = n.channels[i];
            if (!anims.valid(c)) continue;
            const std::uint32_t bit = 1u << i;
            n.visual.f[i] = static_cast<float>(anims.value(c, t, policy));
            if (anims.settled(c, t)) n.moving &= ~bit;
            else {
                n.moving |= bit;
                any = true;
            }
        }
        Visual& v = n.visual;
        v.background_color = n.moving & background_bits ? lanes_color(&v.f[static_cast<std::size_t>(Field::background_r)]) : v.background_target;
        v.border_color = n.moving & border_bits ? lanes_color(&v.f[static_cast<std::size_t>(Field::border_r)]) : v.border_target;
        for (Node::PropAnimation& a : n.prop_animations) {
            bool settled = true;
            for (std::uint8_t i = 0; i < a.lanes; ++i) settled = settled && anims.settled(a.channels[i], t);
            // Settled by then: shown as the value it rests on; moving: the next frame applies it.
            if (settled) rested.emplace_back(id, a.name, a.target);
            a.resting = settled;
        }
        // Glyph motions are functions of the time: before their end, they move again.
        if (any || n.props_moving() || n.glyphs_moving(t) || n.effect_moving(t)) {
            if (std::ranges::find(animating, id) == animating.end()) animating.push_back(id);
            if (!n.state.animating) {
                n.state.animating = true;
                touch(dirty_semantics);
            }
        }
    }
    for (const auto& [id, name, value] : rested)
        if (alive(id)) apply_now(id, name, value);
    tell_retimed();
    touch(dirty_paint | dirty_layout);
    frame_requested = true;
}

void Runtime::Impl::tell_retimed() {
    const seconds t = now();
    std::vector<NodeId> told;  // first gathered: a component may build or remove nodes
    for (std::size_t index = 1; index < by_index.size(); ++index)
        if (const NodeId id = by_index[index]; alive(id) && at(id).type && at(id).type->retimed && at(id).instance) told.push_back(id);
    for (const NodeId id : told)
        if (alive(id) && at(id).instance) {
            auto use = use_theme(id);
            at(id).type->retimed(at(id).instance, t);
        }
    frame_requested = true;
}

bool Runtime::time_paused() const { return impl_->time_control.controlled && impl_->time_control.paused; }

void Runtime::pause_time(bool paused) {
    impl_->take_time_control();
    impl_->time_control.paused = paused;
    impl_->tell_retimed();
    impl_->frame_requested = true;
}

void Runtime::set_time_rate(double rate) {
    impl_->take_time_control();
    impl_->time_control.rate = std::max(0.0, rate);
    impl_->tell_retimed();
    impl_->frame_requested = true;
}

void Runtime::step_time(seconds dt) {
    impl_->take_time_control();
    impl_->time_control.step += dt;
    impl_->frame_requested = true;
}

void Runtime::seek_time(seconds t) {
    impl_->take_time_control();
    impl_->time_control.seek = t;
    impl_->frame_requested = true;
}

void Runtime::release_time() {
    impl_->time_control = {};
    impl_->retime();
}

Runtime::TimeState Runtime::time_state() const noexcept {
    const Impl& d = *impl_;
    return {d.clock.now(), d.now(), d.time_control.controlled, d.time_control.paused, d.time_control.rate};
}
pipeline::Registry& Runtime::stage_plugins() noexcept { return impl_->registry; }

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
    const hanim::Policy policy{reduce_motion};
    std::vector<NodeId> still;
    for (NodeId id : animating) {
        if (!alive(id)) continue;
        Node& n = at(id);
        for (std::size_t i = 0; i < field_count; ++i) {
            const std::uint32_t bit = 1u << i;
            if (!(n.moving & bit)) continue;
            const hanim::Set::Channel c = n.channels[i];
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
        // Colours: the lanes' colour while moving, the exact colour once settled.
        Visual& v = n.visual;
        v.background_color = n.moving & background_bits ? lanes_color(&v.f[static_cast<std::size_t>(Field::background_r)]) : v.background_target;
        v.border_color = n.moving & border_bits ? lanes_color(&v.f[static_cast<std::size_t>(Field::border_r)]) : v.border_target;
        if (n.props_moving()) {
            step_props(id, t, policy);  // layout and component properties (D-60)
            if (!alive(id)) continue;
        }
        Node& after = at(id);
        if (after.moving || after.props_moving() || after.glyphs_moving(t) || after.effect_moving(t)) still.push_back(id);
        else if (after.state.animating) {
            after.state.animating = false;
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
    glyph_movers.clear();
    paint_node(root);
    // What was on screen before a style's change, fading out over what comes.
    if (fading) {
        const double u = reduce_motion ? 1.0 : (now() - fade_start).count() / std::max(1e-6, fade_length.count());
        if (u >= 1) fading.reset();
        else {
            if (fade_clip) list.push_clip(*fade_clip);  // a scope's: only its area
            list.push_layer(static_cast<float>(1 - hanim::ease::standard(u)), render::BlendMode::normal);
            list.append(*fading);
            list.pop_layer();
            if (fade_clip) list.pop_clip();
        }
    }
    // Overlays above everything, bottom to top; a modal one darkens what is below.
    for (NodeId id : overlays) {
        if (!alive(id) || !displayed(id)) continue;
        const Node& n = at(id);
        if (n.type->placement && n.type->placement(n.instance).scrim) {
            const Color scrim = theme_of(id).color("scrim", Color::hex(0x0f172a, 0.45f));
            list.fill_rect({0, 0, size.width, size.height}, scrim.with_alpha(scrim.a * std::clamp(n.visual[Field::opacity], 0.0f, 1.0f)));
        }
        paint_node(id);
    }
}

void Runtime::Impl::paint_node(NodeId id) {
    Node& n = at(id);
    // Its scope's theme: what it draws with, and what its hooks are told.
    const Theme& theme = theme_of(id);
    auto use = use_theme(id);
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
    // The pixel effect takes the node's whole drawing, children included (D-68).
    // Its own, else what the style's rules give its type or role (D-69: a
    // style pack's effects; the node's property wins).
    Node::PixelEffectState* fxs = n.pixel_effect ? &*n.pixel_effect : (n.styled_effect ? &*n.styled_effect : nullptr);
    const bool effected = fxs != nullptr;
    if (effected) {
        auto& fx = *fxs;
        if (fx.duration && fx.progress >= 0) {
            const double u = (now() - fx.start - fx.delay).count() / std::max(1e-6, fx.duration->count());
            double p = reduce_motion ? 1.0 : fx.ease(std::clamp(u, 0.0, 1.0));
            if (fx.reverse) p = 1 - p;
            fx.params[static_cast<std::size_t>(fx.progress)] = static_cast<float>(p);
        }
        // The layer's shapes: what the node paints as surfaces — its own
        // rounded box, and each child's that has a background or a border
        // (a group's members: what neon lights and metaball merges).
        std::array<render::EffectShape, 8> shapes{};
        std::size_t count = 0;
        if (n.type->effect_shapes) {
            const Insets pad = content_insets_of(layout.style(n.layout));
            const Rect inner{pad.left, pad.top, std::max(0.0f, size.width - pad.horizontal()), std::max(0.0f, size.height - pad.vertical())};
            count = n.type->effect_shapes(n.instance, size, inner, std::span(shapes));
        }
        if (count == 0) shapes[count++] = {box, v.corners(size)};
        for (const NodeId c : n.children) {
            if (count == shapes.size()) break;
            const Node& k = at(c);
            if (k.type->overlay || !k.visual.visible) continue;
            const bool surface = k.visual.background().a > 0 || (k.visual[Field::border_width] > 0 && k.visual.border().a > 0);
            if (!surface) continue;
            const Rect f = k.frame;
            shapes[count++] = {{f.x + k.visual[Field::translate_x], f.y + k.visual[Field::translate_y], f.width, f.height}, k.visual.corners(f.size())};
        }
        list.push_layer(1, render::BlendMode::normal, fx.effect, fx.params, box, std::span(shapes.data(), count));
    }
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
    const std::size_t first_glyphs = list.commands().size();
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
    // The node's glyph motion takes the text of its whole drawing (D-68): its
    // own, and its descendants' that have none of their own.
    if (at(id).glyph_motion) {
        restyle_glyphs(id, first_glyphs);
        glyph_movers.push_back(at(id).index);
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
    if (effected) list.pop_layer();
    list.pop_transform();
    list.end_node();
}

void Runtime::Impl::restyle_glyphs(NodeId id, std::size_t first) {
    // Every glyph command recorded since `first` — the node's own and its
    // descendants', each descendant a paragraph of its own — styled by the
    // node's glyph motion at this frame's time (D-68): the components did
    // nothing. A descendant with a motion of its own keeps it. The text's
    // decorations go with it: a fill no taller than a line drawn under or
    // behind a paragraph's glyphs (an underline, a strikethrough, an inline
    // code's background) moves by those glyphs' mean offset and fades with
    // their mean opacity.
    Node& n = at(id);
    struct Fill {
        std::size_t   index;
        Rect          rect;
        std::uint32_t paragraph;
    };
    std::vector<Fill> fills;
    std::vector<std::size_t> at_index;
    std::vector<std::span<const render::Glyph>> runs;
    std::vector<std::uint32_t> paragraphs;
    std::vector<std::uint32_t> open;  // the paragraphs of the descendants begun and not ended
    std::uint32_t next = 0;
    std::size_t kept = 0;  // the depth at which a descendant with its own motion began (0: none)
    const auto& commands = list.commands();
    for (std::size_t i = first; i < commands.size(); ++i) {
        if (const auto* b = std::get_if<render::BeginNode>(&commands[i])) {
            open.push_back(++next);
            if (!kept && std::ranges::contains(glyph_movers, b->node)) kept = open.size();
        } else if (std::holds_alternative<render::EndNode>(commands[i])) {
            if (kept == open.size()) kept = 0;
            if (!open.empty()) open.pop_back();
        } else if (const auto* g = std::get_if<render::DrawGlyphs>(&commands[i]); g && !kept) {
            at_index.push_back(i);
            runs.push_back(g->glyphs);
            paragraphs.push_back(open.empty() ? 0 : open.back());
        } else if (!kept) {
            const std::uint32_t paragraph = open.empty() ? 0 : open.back();
            if (const auto* r = std::get_if<render::FillRect>(&commands[i])) fills.push_back({i, r->rect, paragraph});
            else if (const auto* rr = std::get_if<render::FillRoundRect>(&commands[i])) fills.push_back({i, rr->rect, paragraph});
        }
    }
    if (runs.empty()) return;
    auto& state = *n.glyph_motion;
    const GlyphFrame f = glyph_frame(state.motion, now() - state.start, runs, reduce_motion, paragraphs);
    state.ends = f.ends;
    for (std::size_t k = 0; k < at_index.size(); ++k) list.style_glyphs(at_index[k], f.styles[k]);
    for (const Fill& fill : fills) {
        // The glyphs whose pens it lies under: across its width, about their line's middle.
        double dx = 0, dy = 0, opacity = 0;
        std::size_t count = 0;
        for (std::size_t k = 0; k < at_index.size(); ++k) {
            if (paragraphs[k] != fill.paragraph || f.styles[k].empty()) continue;
            const auto& g = std::get<render::DrawGlyphs>(list.commands()[at_index[k]]);
            if (fill.rect.height > 1.6f * g.size) continue;
            for (std::size_t j = 0; j < g.glyphs.size(); ++j) {
                const Point pen{g.origin.x + g.glyphs[j].position.x, g.origin.y + g.glyphs[j].position.y};
                const float middle = pen.y - 0.3f * g.size;
                if (pen.x < fill.rect.x || pen.x >= fill.rect.x + fill.rect.width) continue;
                if (std::abs(fill.rect.y + fill.rect.height / 2 - middle) > 0.6f * g.size) continue;
                const render::GlyphStyle& st = f.styles[k][j];
                dx += st.transform.e;
                dy += st.transform.f;
                opacity += st.opacity;
                ++count;
            }
        }
        if (count == 0) continue;
        const double c = static_cast<double>(count);
        list.move_fill(fill.index, {static_cast<float>(dx / c), static_cast<float>(dy / c)}, static_cast<float>(opacity / c));
    }
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
