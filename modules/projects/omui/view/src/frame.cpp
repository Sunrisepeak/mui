// A frame (§3.1 of the plan): input → handlers and actions → signals →
// regions rebuilt → animations → layout (incremental) → paint.
module omui.view;

import std;
import omui.core;
import mlibs.layout;
import xrender.vector;
import xrender.raster;
import mlibs.text;
import mlibs.trace;
import mlibs.pipeline;
import :impl;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
}

namespace omui {

namespace {

constexpr float degrees_to_radians = static_cast<float>(std::numbers::pi / 180.0);

Insets content_insets_of(const mlibs::layout::Style& s) {
    return {s.padding.top + s.border.top, s.padding.right + s.border.right, s.padding.bottom + s.border.bottom,
            s.padding.left + s.border.left};
}

}  // namespace

FrameOutput Runtime::frame(const FrameInput& input) {
    Impl& d = *impl_;
    if (auto valid = validate_input_attribution(input); !valid) {
        constexpr Log input_log{"agent"};
        input_log.warn("{}", valid.error());
        FrameOutput refused;
        refused.list = &d.list;
        refused.revision = d.semantic_revision;
        refused.failure = valid.error();
        return refused;
    }
    FrameTimings& t = d.stats.timings;
    const auto started = std::chrono::steady_clock::now();
    mlibs::trace::Span whole("frame", "frame");
    if (input.time) d.clock.set(*input.time);
    else if (d.clock.kind() == hanim::ClockKind::interactive) d.clock.begin_frame(input.present_at.value_or(started));
    d.shown.now = d.clock.now();
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
    std::string failure;
    if (auto r = d.frames->run(d.resources); !r) {
        failure = r.error();
        // Said when it is new, and once a second while it goes on (not every frame).
        if (failure != d.last_failure || d.now() - d.last_failure_said > seconds(1)) {
            constexpr Log frame_log{"frame"};
            frame_log.error("{}", failure);
            d.last_failure_said = d.now();
        }
        d.last_failure = failure;
    } else if (!d.last_failure.empty()) {
        d.last_failure.clear();
    }
    if (!d.dirty_taken) d.dirty = dirty_none;  // no bounds stage (H0): nothing waits for the changes
    for (const mlibs::pipeline::StageTime& st : d.frames->timings()) {
        double* into = st.name == "timers" ? &t.timers
                     : st.name == "input" ? &t.input
                     : st.name == "regions" ? &t.regions
                     : st.name == "animations" ? &t.animations
                     : st.name == "layout" ? &t.layout
                     : st.name == "bounds" ? &t.bounds
                     : st.name == "paint" || st.name == "motion" || st.name == "debug" ? &t.paint
                     : &t.other;
        *into += st.ms;
    }
    // Only causal layout work keeps an external allocation transaction open.
    // Bounds hooks may schedule a responsive region for the following frame;
    // semantic notifications, ordinary animation frames and future timers do not.
    if (d.dirty_regions.empty() && !(d.dirty & (dirty_layout | dirty_tree))) d.constraint_boundaries.clear();
    const bool repainted = d.repainted;
    if (repainted) d.drawn[d.drawn_next++ % d.drawn.size()] = std::chrono::steady_clock::now();
    // Nodes that lived through a frame are shown: their changes may animate
    // from now on (at every stage alike, so H0, H1 and H2 agree).
    for (NodeId id : d.fresh)
        if (d.alive(id)) d.at(id).shown = true;
    d.fresh.clear();

    d.stats.nodes = d.nodes.size();
    for (NodeId id : d.animating) d.stats.animating += static_cast<std::size_t>(std::popcount(d.at(id).moving));
    mlibs::trace::count("nodes", static_cast<double>(d.stats.nodes), "frame");
    t.total = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    if (d.options.debug.slow_frame_ms > 0 && t.total > d.options.debug.slow_frame_ms && d.now() - d.last_slow_log > seconds(1)) {
        d.last_slow_log = d.now();
        constexpr Log frame_log{"frame"};
        frame_log.warn("a slow frame: {:.2f} ms (timers {:.2f}, input {:.2f}, regions {:.2f}, animations {:.2f}, layout {:.2f}, "
                       "bounds {:.2f}, paint {:.2f}, other stages {:.2f}); {} nodes, {} laid out, {} painted",
                       t.total, t.timers, t.input, t.regions, t.animations, t.layout, t.bounds, t.paint, t.other, d.stats.nodes,
                       d.stats.laid_out, d.stats.painted);
    }
    // Retaining a seekable handle does not imply movement. A paused handle
    // or one at its directional endpoint must let the host become idle.
    const bool moving_morph = d.motions_moving();
    const bool more = !d.animating.empty() || moving_morph || d.frame_requested || !d.dirty_regions.empty() || d.fading.has_value() ||
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
        if (!d.animating.empty() || moving_morph || d.fading) want = merge(want, FrameDemand::fastest("animation"));
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
        mlibs::trace::count("frame.demand_hz", d.demand.idle() ? 0.0 : std::min(d.demand.wanted(), 1000.0), "frame.pacing");
    }
    // Bindings that ran since the last frame count toward this one.
    FrameStats stats = std::exchange(d.stats, {});
    d.history.push(stats);
    return FrameOutput{&d.list, repainted, more, d.semantic_revision, stats, next_timer(), d.demand, std::move(failure)};
}

mlibs::pipeline::Pipeline& Runtime::frame_pipeline() noexcept { return *impl_->frames; }

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
        if (any || n.props_moving() || n.glyphs_moving(t) || n.effect_moving(t, !reduce_motion && motion_now() == MotionLevel::full)) {
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
mlibs::pipeline::Registry& Runtime::stage_plugins() noexcept { return impl_->registry; }

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
        if (after.moving || after.props_moving() || after.glyphs_moving(t) || after.effect_moving(t, !reduce_motion && motion_now() == MotionLevel::full)) still.push_back(id);
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
    for (const NodeId id : overlays)
        if (alive(id)) at(id).overlay_measured = layout.frame(at(id).layout).size();
    stats.laid_out = layout.stats().layouts;
    dirty |= dirty_paint | dirty_semantics;
    ++semantic_revision;  // bounds moved
}

bool Runtime::Impl::displayed(NodeId id) const {
    const Node& n = at(id);
    return n.visual.visible && layout.style(n.layout).display != mlibs::layout::Display::none;
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
    // How far each edge's and corner's overlays reach in so far (they stack).
    std::array<float, 13> stacked{};
    for (std::size_t overlay_index = 0; overlay_index < overlays.size(); ++overlay_index) {
        const NodeId id = overlays[overlay_index];
        if (!alive(id) || !displayed(id)) continue;
        Node& n = at(id);
        const Placement p = n.type->placement ? n.type->placement(n.instance) : Placement{};
        const bool was_bounded = alive(n.overlay_boundary);
        n.overlay_boundary = {};
        for (NodeId ancestor = n.parent; alive(ancestor); ancestor = at(ancestor).parent) {
            const auto flag = at(ancestor).props.find("overlay_boundary");
            if (flag != at(ancestor).props.end() && flag->second.as_bool()) {
                n.overlay_boundary = ancestor;
                break;
            }
        }
        Rect area = window;
        if (alive(n.overlay_boundary)) {
            const Node& boundary = at(n.overlay_boundary);
            area = area.intersection(projected_target_bounds(n.overlay_boundary).value_or(boundary.world.map({0, 0, boundary.frame.width, boundary.frame.height})));
            for (NodeId ancestor = n.parent; alive(ancestor); ancestor = at(ancestor).parent) {
                const Node& parent = at(ancestor);
                if (parent.visual.clip || parent.type->clips)
                    area = area.intersection(projected_target_bounds(ancestor).value_or(parent.world.map({0, 0, parent.frame.width, parent.frame.height})));
            }
            // Resolve a side panel's auto height against its local viewport;
            // other auto dimensions retain their measured extent, constrained
            // by the available region. Layout's cache makes unchanged requests free.
            const Size measured = n.overlay_measured;
            const bool side = p.side == Placement::Side::left || p.side == Placement::Side::right;
            const auto before = layout.stats().layouts;
            layout.compute(n.layout, {std::min(measured.width, area.width), side ? area.height : std::min(measured.height, area.height)});
            stats.laid_out += layout.stats().layouts - before;
        }
        if (was_bounded && !n.overlay_boundary) {
            const bool side = p.side == Placement::Side::left || p.side == Placement::Side::right;
            const auto before = layout.stats().layouts;
            layout.compute(n.layout, {n.overlay_measured.width, side ? window.height : n.overlay_measured.height});
            stats.laid_out += layout.stats().layouts - before;
        }
        n.overlay_area = area;
        n.overlay_stacked = 0;
        const Rect frame = layout.frame(n.layout);
        auto anchor_of = [&](NodeId who) {
            const Node& parent = at(who);
            if(const auto projected=projected_target_bounds(who)){n.projected_anchor=true;return *projected;}
            return n.overlay_boundary ? parent.world.map({0, 0, parent.frame.width, parent.frame.height}) : parent.bounds;
        };
        Rect anchor = alive(n.parent) ? anchor_of(n.parent) : area;
        if (p.anchor == Placement::Anchor::previous && alive(n.parent)) {
            NodeId before;
            for (NodeId c : at(n.parent).children) {
                if (c == id) break;
                if (!at(c).type->overlay) before = c;
            }
            if (alive(before)) anchor = anchor_of(before);
        }
        const float w = frame.width, h = frame.height;
        Point at{};
        switch (p.side) {
            case Placement::Side::center: at = {area.x + (area.width - w) / 2, area.y + (area.height - h) / 2}; break;
            case Placement::Side::left: at = {area.x + p.offset, area.y}; break;
            case Placement::Side::right: at = {area.right() - w - p.offset, area.y}; break;
            case Placement::Side::below:
            case Placement::Side::above: {
                const float below = anchor.bottom() + p.offset, above = anchor.y - p.offset - h;
                const bool down = p.side == Placement::Side::below ? (below + h <= area.bottom() || above < area.y)
                                                                   : !(above >= area.y || below + h > area.bottom());
                at = {anchor.x, down ? below : above};
                break;
            }
            case Placement::Side::start:
            case Placement::Side::end: {
                const float after = anchor.right() + p.offset, before = anchor.x - p.offset - w;
                const bool right = p.side == Placement::Side::end ? (after + w <= area.right() || before < area.x)
                                                                  : !(before >= area.x || after + w > area.right());
                at = {right ? after : before, anchor.y};
                break;
            }
            default: {  // an edge or a corner of the window, past those already there
                float local_stack = 0;
                if (n.overlay_boundary) {
                    // The last overlay on this boundary/edge carries its
                    // accumulated extent. No per-frame map or vector is needed.
                    for (std::size_t previous = overlay_index; previous-- > 0;) {
                        const NodeId other = overlays[previous];
                        if (!alive(other) || !displayed(other)) continue;
                        const Node& old = this->at(other);
                        const Placement op = old.type->placement ? old.type->placement(old.instance) : Placement{};
                        if (old.overlay_boundary == n.overlay_boundary && op.side == p.side) {
                            local_stack = old.overlay_stacked;
                            break;
                        }
                    }
                }
                float& in = n.overlay_boundary ? local_stack : stacked[static_cast<std::size_t>(p.side)];
                const bool top = p.side == Placement::Side::top || p.side == Placement::Side::top_start || p.side == Placement::Side::top_end;
                const bool start = p.side == Placement::Side::top_start || p.side == Placement::Side::bottom_start;
                const bool end = p.side == Placement::Side::top_end || p.side == Placement::Side::bottom_end;
                const float x = start ? area.x + p.offset : end ? area.right() - w - p.offset : area.x + (area.width - w) / 2;
                const float y = top ? area.y + p.offset + in : area.bottom() - h - p.offset - in;
                in += h + p.offset / 2;
                n.overlay_stacked = in;
                at = {x, y};
                break;
            }
        }
        // Inside its available region, as far as it fits.
        at.x = std::clamp(at.x, area.x, std::max(area.x, area.right() - w));
        at.y = std::clamp(at.y, area.y, std::max(area.y, area.bottom() - h));
        compute_bounds(id, {0, 0}, Affine{}, Affine{}, at);
        if (p.match_width && w < anchor.width) {
            sync_style(id);  // the anchor's width became known: lay out again next frame
            frame_requested = true;
        }
    }
}

void Runtime::Impl::compute_bounds(NodeId id, Point origin, const Affine& parent_target, const Affine& parent_world, std::optional<Point> placed) {
    Node& n = at(id);
    n.frame = layout.frame(n.layout);
    if (placed) {
        n.frame.x = placed->x;
        n.frame.y = placed->y;
    }
    n.content_offset = n.type->scroll_offset ? n.type->scroll_offset(n.instance) : Point{};

    // The local transform: the frame's offset, then the visual transforms
    // about the centre — as drawn now (their transitions part way), and as
    // they are set (where they go: the bounds, what the tree states).
    const Visual& v = n.visual;
    const Size size = n.frame.size();
    auto local_of = [&](float tx, float ty, float s, float rotation) {
        Affine local = Affine::translate(n.frame.x + tx, n.frame.y + ty);
        if (s != 1 || rotation != 0) {
            const float cx = size.width / 2, cy = size.height / 2;
            Affine m = Affine::translate(cx, cy);
            if (rotation != 0) m = m * xrender::rotate(rotation * degrees_to_radians);
            if (s != 1) m = m * Affine::scale(s, s);
            local = local * m * Affine::translate(-cx, -cy);
        }
        return local;
    };
    const Affine local = local_of(v[Field::translate_x], v[Field::translate_y], v[Field::scale], v[Field::rotation]);
    auto set = [&](std::string_view k, float now) {
        const auto it = n.props.find(k);
        return it != n.props.end() && it->second.is_number() ? static_cast<float>(it->second.as_double()) : now;
    };
    const Affine target = parent_target * local_of(set("translate_x", v[Field::translate_x]), set("translate_y", v[Field::translate_y]),
                                                   set("scale", v[Field::scale]), set("rotation", v[Field::rotation]));
    // The layout's box (before the transforms), and where it is on the
    // window as set: a node a canvas shows under its camera (a live window,
    // scaled and moved) is where it is drawn, not where it was laid out.
    n.bounds = {origin.x + n.frame.x, origin.y + n.frame.y, n.frame.width, n.frame.height};
    n.screen = target.translation_only() ? Rect{target.e, target.f, n.frame.width, n.frame.height} : target.map({0, 0, n.frame.width, n.frame.height});
    n.local = local;
    n.world = parent_world * local;

    const Point off = n.content_offset;
    const Point inner{n.bounds.x - off.x, n.bounds.y - off.y};
    const bool scrolled = off.x != 0 || off.y != 0;
    const Affine child_world = scrolled ? n.world * Affine::translate(-off.x, -off.y) : n.world;
    const Affine child_target = scrolled ? target * Affine::translate(-off.x, -off.y) : target;
    for (std::size_t i = 0; i < at(id).children.size(); ++i) {
        const NodeId c = at(id).children[i];
        if (!at(c).type->overlay) compute_bounds(c, inner, child_target, child_world);
    }
}

void Runtime::Impl::paint() {
    if(presentation_source_valid) {
        std::swap(list,presentation_source);
        history_range=presentation_source_history;
        presentation_source_valid=false;
    }
    // The last frame's drawing is kept: what a change moves from (motion.cpp).
    std::swap(list, previous_list);
    previous_history_range = history_range;
    history_range = {};
    list.clear();
    glyph_movers.clear();
    moving_said.clear();
    paint_node(root);
    // What was on screen before a style's change, fading out over what comes.
    if (fading) {
        const double u = reduce_motion || motion_now() == MotionLevel::off ? 1.0 : (now() - fade_start).count() / std::max(1e-6, fade_length.count());
        if (u >= 1) {
            fading.reset();
            touch(dirty_semantics);  // settled: said at the next frame
            frame_requested = true;
        }
        else {
            history_range.first = list.size();
            if (fade_clip) list.push_clip(*fade_clip);  // a scope's: only its area
            list.push_layer(static_cast<float>(1 - hanim::ease::standard(u)), xrender::BlendMode::normal);
            list.append(*fading);
            list.pop_layer();
            if (fade_clip) list.pop_clip();
            history_range.second = list.size();
        }
    }
    // Overlays above everything, bottom to top; a modal one darkens what is below.
    for (NodeId id : overlays) {
        if (!alive(id) || !displayed(id)) continue;
        const Node& n = at(id);
        const bool bounded = alive(n.overlay_boundary);
        std::size_t clips = 0;
        if (bounded) {
            list.push_clip(n.overlay_area);
            ++clips;
            // Retain transformed/rounded ancestor clips too: their bounding
            // rectangles alone would let pixels escape at rotated corners.
            for (NodeId ancestor = n.parent; alive(ancestor); ancestor = at(ancestor).parent) {
                const Node& parent = at(ancestor);
                if (!(parent.visual.clip || parent.type->clips)) continue;
                list.push_transform(projected_target_world(ancestor).value_or(parent.world));
                list.push_clip({0, 0, parent.frame.width, parent.frame.height}, parent.visual.corners(parent.frame.size()));
                list.pop_transform();
                ++clips;
            }
        }
        if (n.type->placement && n.type->placement(n.instance).scrim) {
            const Color scrim = theme_of(id).color("scrim", Color::hex(0x0f172a, 0.45f));
            list.fill_rect(bounded ? n.overlay_area : Rect{0, 0, size.width, size.height}, scrim.with_alpha(scrim.a * std::clamp(n.visual[Field::opacity], 0.0f, 1.0f)));
        }
        paint_node(id);
        while (clips > 0) { list.pop_clip(); --clips; }
    }
    // Components' own motions: a node said moving is animating; one no longer said, settled (readers told either way).
    for (const NodeId id : moving_flagged)
        if (alive(id) && std::ranges::find(moving_said, id) == moving_said.end()) touch(dirty_semantics);
    for (const NodeId id : moving_said)
        if (std::ranges::find(moving_flagged, id) == moving_flagged.end()) touch(dirty_semantics);
    moving_flagged = moving_said;
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
    if (layout.style(n.layout).display == mlibs::layout::Display::none) return;
    ++stats.painted;

    list.begin_node(n.index);
    list.push_transform(local);
    const bool layer = v[Field::opacity] < 1;
    const Rect box{0, 0, size.width, size.height};
    // The pixel effect takes the node's whole drawing, children included (D-68).
    // Its own, else what the style's rules give its type or role (D-69: a
    // style pack's effects; the node's property wins).
    Node::PixelEffectState* fxs = n.pixel_effect ? &*n.pixel_effect : (n.styled_effect ? &*n.styled_effect : nullptr);
    // Less transparency asked for: a see-through effect (glass, frost: it
    // reads what is behind) is left out, the node drawn as its own opaque
    // panel — its background as it looks over the theme's, made solid.
    bool solid = false;
    if (fxs && reduce_transparency)
        if (const xrender::EffectInfo* info = xrender::effects().info(fxs->effect); info && info->backdrop) fxs = nullptr, solid = true;
    // Below the tiers it runs on (performance tiers, D-94: the device's, or as low as the frame budget allows): what
    // stands in for it, its defaults; none — a backdrop's effect, its panel then drawn solid.
    if (fxs)
        if (const xrender::EffectInfo* info = xrender::effects().info(fxs->effect); info && !(info->tiers & mlibs::pipeline::tier_bit(effects_now()))) {
            const auto instead = info->degrade.empty() ? std::nullopt : xrender::effects().find(info->degrade);
            solid = solid || info->backdrop;
            if (instead) {
                n.stood_in = {.effect = *instead, .params = xrender::effects().params(*instead, {})};
                fxs = &n.stood_in;
            } else {
                fxs = nullptr;
            }
        }
    // A disabled control is not lit by its style's effect (a neon tube, a
    // glow): the decoration is for what can be used.
    if (fxs == (n.styled_effect ? &*n.styled_effect : nullptr) && fxs && n.state.disabled && !fxs->appearance) fxs = nullptr;
    // A state's params (pressed, hovered, focused), eased to from wherever they were.
    if (fxs && fxs == (n.styled_effect ? &*n.styled_effect : nullptr) && !fxs->variants.empty()) {
        auto& fx = *fxs;
        const Node::PixelEffectState::Variant* want = nullptr;
        for (const auto& v : fx.variants)
            if (!want && !n.state.disabled && ((v.state == 1 && n.state.pressed) || (v.state == 2 && n.state.hovered) || (v.state == 3 && n.state.focused))) want = &v;
        const std::uint8_t to = want ? want->state : 0;
        if (to != fx.shown) {
            fx.from = fx.params;
            fx.shown = to;
            fx.changed = now();
            if (std::ranges::find(animating, id) == animating.end()) animating.push_back(id);
            frame_requested = true;
        }
        const std::vector<float>& target = want ? want->params : fx.rest;
        const double u = std::clamp((now() - fx.changed).count() / std::max(1e-6, fx.transition.count()), 0.0, 1.0);
        const double e = reduce_motion || motion_now() != MotionLevel::full ? 1.0 : fx.transition_ease(u);
        if (fx.from.size() == target.size() && fx.params.size() == target.size())
            for (std::size_t i = 0; i < target.size(); ++i)
                if (!fx.animates(i)) fx.params[i] = hanim::lerp(fx.from[i], target[i], e);
    }
    auto sample = [&](Node::PixelEffectState& fx) {
        if (!fx.duration || !fx.animation) return;
        const auto local = now() - fx.start - fx.delay;
        const double u = local.count() < 0 ? 0 : fx.duration->count() == 0 ? 1 : std::clamp(local.count() / fx.duration->count(), 0.0, 1.0);
        double p = reduce_motion || motion_now() != MotionLevel::full ? 1.0 : u == 0 || u == 1 ? u : fx.ease(u);
        if (fx.reverse) p = 1 - p;
        auto& animation = *fx.animation;
        if (animation.plan.sample(p, animation.samples))
            for (std::size_t i = 0; i < animation.params.size(); ++i) fx.params[animation.params[i]] = animation.samples[i];
    };
    if (fxs) {
        sample(*fxs);
        // UI effect wrappers carry no opacity/blend of their own. An identity
        // needs no extra isolated group (which would reapply ancestor clips).
        // The node's actual opacity layer below remains independent.
        if (const auto* info = xrender::effects().info(fxs->effect); info && info->identity && info->identity(fxs->params)) fxs = nullptr;
    }
    const bool effected = fxs != nullptr;
    // More of its own over it (a list), each where it runs (performance tiers: below its tiers, left out).
    std::size_t more = 0;
    for (auto& fx : n.more_effects)
        if (const xrender::EffectInfo* info = xrender::effects().info(fx.effect); info && (info->tiers & mlibs::pipeline::tier_bit(effects_now())) &&
                                                                               !(info->backdrop && reduce_transparency)) {
            sample(fx);
            if (!info->identity || !info->identity(fx.params)) ++more;
        }
    if (effected || more > 0) {
        // The layer's shapes: what the node paints as surfaces — its own
        // rounded box, and each child's that has a background or a border
        // (a group's members: what neon lights and metaball merges).
        std::array<xrender::EffectShape, 8> shapes{};
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
        // The last of the list outermost: pushed first.
        for (auto it = n.more_effects.rbegin(); it != n.more_effects.rend(); ++it) {
            const xrender::EffectInfo* info = xrender::effects().info(it->effect);
            if (!info || !(info->tiers & mlibs::pipeline::tier_bit(effects_now())) || (info->backdrop && reduce_transparency)) continue;
            if (info->identity && info->identity(it->params)) continue;
            list.push_layer(1, xrender::BlendMode::normal, it->effect, it->params, box, std::span(shapes.data(), count));
        }
        if (effected) {
            list.push_layer(1, xrender::BlendMode::normal, fxs->effect, fxs->params, box, std::span(shapes.data(), count));
        }
    }
    if (layer) list.push_layer(v[Field::opacity], xrender::BlendMode::normal, {}, {}, box);

    const CornerRadii radii = v.corners(size);
    Size constraint = this->size;
    if (alive(n.parent)) {
        const Node& parent = at(n.parent);
        const Insets inset = content_insets_of(layout.style(parent.layout));
        constraint = {std::max(0.0f, parent.frame.width - inset.horizontal()), std::max(0.0f, parent.frame.height - inset.vertical())};
    }
    list.node_frame(n.index, box, 1, n.frame, constraint, Affine::translate(-n.frame.x, -n.frame.y) * local);
    if (!v.shadow.empty())
        if (auto it = theme.shadows.find(v.shadow); it != theme.shadows.end())
            list.box_shadow(box, radii, it->second.offset, it->second.blur, it->second.spread, it->second.color);
    if (Color bg = v.background(); bg.a > 0) {
        if (solid && bg.a < 1) {
            const Color under = theme.color("background");
            bg = {under.r + (bg.r - under.r) * bg.a, under.g + (bg.g - under.g) * bg.a, under.b + (bg.b - under.b) * bg.a, 1};
        }
        if (radii.zero()) list.fill_rect(box, bg);
        else list.fill_round_rect(box, radii, xrender::Paint::solid(bg));
    }
    if (const float bw = v[Field::border_width]; bw > 0)
        if (const Color bc = v.border(); bc.a > 0) {
            const float h = bw / 2;
            CornerRadii inner = radii;
            for (float* r : {&inner.top_left, &inner.top_right, &inner.bottom_right, &inner.bottom_left}) *r = std::max(0.0f, *r - h);
            list.stroke_round_rect({h, h, size.width - bw, size.height - bw}, inner, bw, xrender::Paint::solid(bc));
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
                                   r, width, xrender::Paint::solid(theme.color("focus", Color::hex(0x6366f1, 0.55f))));
        }
    }

    if (layer) list.pop_layer();
    if (effected) list.pop_layer();
    for (std::size_t i = 0; i < more; ++i) list.pop_layer();
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
    std::vector<std::span<const xrender::Glyph>> runs;
    std::vector<std::uint32_t> paragraphs;
    std::vector<std::uint32_t> open;  // the paragraphs of the descendants begun and not ended
    std::uint32_t next = 0;
    std::size_t kept = 0;  // the depth at which a descendant with its own motion began (0: none)
    const auto& commands = list.commands();
    for (std::size_t i = first; i < commands.size(); ++i) {
        if (const auto* b = std::get_if<xrender::BeginNode>(&commands[i])) {
            open.push_back(++next);
            if (!kept && std::ranges::contains(glyph_movers, b->node)) kept = open.size();
        } else if (std::holds_alternative<xrender::EndNode>(commands[i])) {
            if (kept == open.size()) kept = 0;
            if (!open.empty()) open.pop_back();
        } else if (const auto* g = std::get_if<xrender::DrawGlyphs>(&commands[i]); g && !kept) {
            at_index.push_back(i);
            runs.push_back(g->glyphs);
            paragraphs.push_back(open.empty() ? 0 : open.back());
        } else if (!kept) {
            const std::uint32_t paragraph = open.empty() ? 0 : open.back();
            if (const auto* r = std::get_if<xrender::FillRect>(&commands[i])) fills.push_back({i, r->rect, paragraph});
            else if (const auto* rr = std::get_if<xrender::FillRoundRect>(&commands[i])) fills.push_back({i, rr->rect, paragraph});
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
            const auto& g = std::get<xrender::DrawGlyphs>(list.commands()[at_index[k]]);
            if (fill.rect.height > 1.6f * g.size) continue;
            for (std::size_t j = 0; j < g.glyphs.size(); ++j) {
                const Point pen{g.origin.x + g.glyphs[j].position.x, g.origin.y + g.glyphs[j].position.y};
                const float middle = pen.y - 0.3f * g.size;
                if (pen.x < fill.rect.x || pen.x >= fill.rect.x + fill.rect.width) continue;
                if (std::abs(fill.rect.y + fill.rect.height / 2 - middle) > 0.6f * g.size) continue;
                const xrender::GlyphStyle& st = f.styles[k][j];
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

void Runtime::Impl::collect_presentation() {
    mlibs::trace::Span span("presentation geometry", "frame");
    const auto previous_epoch = presentation_epoch++;
    presentation_transforms.assign(1, Affine{});
    presentation_clip_stack.assign(1, -1);
    presentation_opacity.assign(1, 1);
    presentation_clip_count = 0;
    const auto commands = list.commands();
    bool changed = false;
    auto add_clip = [&](PresentationClip clip) {
        clip.parent = presentation_clip_stack.back();
        clip.world = presentation_transforms.back(); clip.inverse = clip.world.inverse();
        if (clip.parent >= 0) clip.bounds = clip.bounds.intersection(presentation_clips[static_cast<std::size_t>(clip.parent)].bounds);
        const auto index = presentation_clip_count++;
        if (index == presentation_clips.size()) presentation_clips.push_back(std::move(clip));
        else presentation_clips[index] = std::move(clip);
        presentation_clip_stack.push_back(static_cast<int>(index));
    };
    for (std::size_t i = 0; i < commands.size(); ++i) {
        if (history_range.second > history_range.first && i == history_range.first) { i = history_range.second - 1; continue; }
        const auto& command = commands[i];
        if (const auto* t = std::get_if<xrender::PushTransform>(&command)) presentation_transforms.push_back(presentation_transforms.back() * t->transform);
        else if (std::holds_alternative<xrender::PopTransform>(command)) { if (presentation_transforms.size() > 1) presentation_transforms.pop_back(); }
        else if (const auto* c = std::get_if<xrender::PushClipRect>(&command)) {
            PresentationClip clip; clip.rect = *c; clip.bounds = presentation_transforms.back().map(c->rect); add_clip(std::move(clip));
        } else if (const auto* c = std::get_if<xrender::PushClipPath>(&command)) {
            PresentationClip clip; clip.path = *c;
            if (!c->path.points.empty()) {
                Point first = presentation_transforms.back().apply(c->path.points.front());
                float left = first.x, top = first.y, right = first.x, bottom = first.y;
                for (const auto point : c->path.points) {
                    const auto at = presentation_transforms.back().apply(point);
                    left = std::min(left, at.x); right = std::max(right, at.x); top = std::min(top, at.y); bottom = std::max(bottom, at.y);
                }
                clip.bounds = {left, top, right - left, bottom - top};
            }
            add_clip(std::move(clip));
        } else if (std::holds_alternative<xrender::PopClip>(command)) { if (presentation_clip_stack.size() > 1) presentation_clip_stack.pop_back(); }
        else if (const auto* layer = std::get_if<xrender::PushLayer>(&command)) presentation_opacity.push_back(presentation_opacity.back() * layer->opacity);
        else if (std::holds_alternative<xrender::PopLayer>(command)) { if (presentation_opacity.size() > 1) presentation_opacity.pop_back(); }
        else if (const auto* frame = std::get_if<xrender::NodeFrame>(&command); frame && frame->node < by_index.size() && alive(by_index[frame->node])) {
            Node& node = at(by_index[frame->node]);
            if (!(frame->opacity * presentation_opacity.back() > 0)) continue;
            const Rect border = presentation_transforms.back().map(frame->rect);
            Rect visible = border.intersection({0, 0, size.width, size.height});
            const int clip = presentation_clip_stack.back();
            if (clip >= 0) visible = visible.intersection(presentation_clips[static_cast<std::size_t>(clip)].bounds);
            changed = changed || node.presented_epoch != previous_epoch || node.presented_bounds != border || node.presented_visible != visible;
            node.presented_epoch = presentation_epoch; node.presented_bounds = border; node.presented_visible = visible; node.presented_clip = clip;
            // Components receive coordinates in their current local border box,
            // normalized from the sampled border. This is also the hit inverse.
            const float sx = node.frame.width > 0 ? frame->rect.width / node.frame.width : 1;
            const float sy = node.frame.height > 0 ? frame->rect.height / node.frame.height : 1;
            node.presented_world = presentation_transforms.back() * Affine::translate(frame->rect.x, frame->rect.y) * Affine::scale(sx, sy);
            node.presented_inverse = node.presented_world.inverse();
        }
    }
    for (const auto id : by_index) if (alive(id) && at(id).presented_epoch == previous_epoch) { at(id).presented_inverse.reset(); changed = true; }
    if (changed) {
        // This frame already carries the new geometry. Publish its revision
        // without requesting a redundant animation frame after it settles.
        dirty |= dirty_semantics;
        ++semantic_revision;
    }
}

bool Runtime::Impl::presentation_contains(const Node& node, Point window) const {
    if (node.presented_epoch != presentation_epoch || !node.presented_inverse || !Rect{0, 0, size.width, size.height}.contains(window)) return false;
    for (int at = node.presented_clip; at >= 0; at = presentation_clips[static_cast<std::size_t>(at)].parent) {
        const auto& clip = presentation_clips[static_cast<std::size_t>(at)];
        if (!clip.bounds.contains(window) || !clip.inverse) return false;
        if (clip.rect) {
            const auto p = clip.inverse->apply(window); const auto& rect = clip.rect->rect;
            if (!rect.contains(p)) return false;
            const auto radii = clip.rect->radii.fitted(rect.size());
            auto corner = [&](float radius, bool left, bool top) {
                const Point center{left ? rect.x + radius : rect.right() - radius, top ? rect.y + radius : rect.bottom() - radius};
                if (!(left ? p.x < center.x : p.x > center.x) || !(top ? p.y < center.y : p.y > center.y)) return true;
                const float x = p.x - center.x, y = p.y - center.y; return x * x + y * y <= radius * radius;
            };
            if (!corner(radii.top_left, true, true) || !corner(radii.top_right, false, true) ||
                !corner(radii.bottom_left, true, false) || !corner(radii.bottom_right, false, false)) return false;
        } else if (clip.path) {
            // Use the renderer's existing curve flattener/coverage convention,
            // not a separately approximated path or any GPU/pixel readback.
            const auto& path = clip.path->path;
            presentation_path_verbs.resize(path.verbs.size());
            for (std::size_t i = 0; i < path.verbs.size(); ++i) presentation_path_verbs[i] = static_cast<xrender::raster::Verb>(path.verbs[i]);
            const xrender::raster::PathData data{presentation_path_verbs, path.points};
            const auto matrix = Affine::scale(scale, scale) * clip.world;
            xrender::raster::flatten(data, matrix, presentation_path_polygons);
            const int x = static_cast<int>(window.x * scale), y = static_cast<int>(window.y * scale); // window is inside the nonnegative viewport
            presentation_path_rasterizer.rasterize(presentation_path_polygons, static_cast<xrender::raster::FillRule>(clip.path->rule), {x, y, 1, 1}, presentation_path_coverage);
            if (presentation_path_coverage.empty() || (x < presentation_path_coverage.bounds.x || y < presentation_path_coverage.bounds.y || x >= presentation_path_coverage.bounds.x + presentation_path_coverage.bounds.width || y >= presentation_path_coverage.bounds.y + presentation_path_coverage.bounds.height) || presentation_path_coverage.at(x, y) < 128) return false;
        }
    }
    return true;
}

NodeId Runtime::Impl::hit(NodeId id, Point window, std::optional<Rect>) const {
    const Node& n = at(id);
    if (!n.visual.visible || n.visual[Field::opacity] <= 0 || n.state.removing) return {};
    if (!n.visual.hit) return {};  // pointer_events: none — it and all inside it take no pointer (a HUD, a picture over a field)
    if (layout.style(n.layout).display == mlibs::layout::Display::none) return {};
    const bool presented = presentation_epoch != 0;
    if (presented && n.presented_epoch != presentation_epoch) return {};
    if (presented && !presentation_contains(n, window)) return {};
    Point p = local_point(n, window);
    // An effect that moves what is drawn (a bulging screen): the point seen
    // is where it was drawn from, for this node and what is inside it.
    if (const Node::PixelEffectState* fx = n.pixel_effect ? &*n.pixel_effect : (n.styled_effect ? &*n.styled_effect : nullptr))
        if (const xrender::EffectInfo* info = xrender::effects().info(fx->effect); info && info->source_of) {
            p = info->source_of(p, {0, 0, n.frame.width, n.frame.height}, fx->params);
            window = (presented ? n.presented_world : n.world).apply(p);
        }
    const bool inside = p.x >= 0 && p.y >= 0 && p.x < n.frame.width && p.y < n.frame.height;
    if (!presented && (n.visual.clip || n.type->clips) && !inside) return {};
    for (std::size_t i = n.children.size(); i-- > 0;) {
        if (at(n.children[i]).type->overlay) continue;
        if (NodeId h = hit(n.children[i], window, std::nullopt)) return h;
    }
    if (inside && n.visual.hit_self) return id;
    return {};
}

NodeId Runtime::hit_test(Point window) const {
    const Impl& d = *impl_;
    // Overlays first, top to bottom; a modal one hides what is below it.
    for (auto it = d.overlays.rbegin(); it != d.overlays.rend(); ++it) {
        if (!d.alive(*it) || !d.displayed(*it)) continue;
        const Node& o = d.at(*it);
        bool inside_boundary = true;
        if (d.presentation_epoch != 0) inside_boundary = o.presented_epoch == d.presentation_epoch && d.presentation_contains(o, window);
        else if (d.alive(o.overlay_boundary)) {
            inside_boundary = o.overlay_area.contains(window);
            for (NodeId ancestor = o.parent; inside_boundary && d.alive(ancestor); ancestor = d.at(ancestor).parent) {
                const Node& parent = d.at(ancestor);
                if (!(parent.visual.clip || parent.type->clips)) continue;
                const Point local = local_point(parent, window);
                inside_boundary = Rect{0, 0, parent.frame.width, parent.frame.height}.contains(local);
                if (!inside_boundary) break;
                const CornerRadii corners = parent.visual.corners(parent.frame.size());
                auto corner = [&](float radius, bool left, bool top) {
                    const Point center{left ? radius : parent.frame.width - radius, top ? radius : parent.frame.height - radius};
                    if (!(left ? local.x < center.x : local.x > center.x) || !(top ? local.y < center.y : local.y > center.y)) return true;
                    const float dx = local.x - center.x, dy = local.y - center.y;
                    return dx * dx + dy * dy <= radius * radius;
                };
                inside_boundary = corner(corners.top_left, true, true) && corner(corners.top_right, false, true) &&
                                  corner(corners.bottom_left, true, false) && corner(corners.bottom_right, false, false);
            }
        }
        if (inside_boundary)
            if (NodeId h = d.hit(*it, window, std::nullopt)) return h;
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
