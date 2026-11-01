// Universal motion in the frame (D-93; universal motion §5, §6): after the
// paint, each node whose update slot holds a morph — a root — is compared
// with what it drew the frame before (its range of the display list, its
// nested roots' ranges left out: they move themselves). When it drew
// something else, a morph starts from what was on screen (the last frame's
// drawing, or the morph under way caught where it is) to what it draws now,
// and the range is drawn by the morph until it rests. Components write no
// animation: a toggle's thumb, a tab's lens, a list's order, a button's
// colour and a label's text all move by this one rule.
//
// Not morphed: a node held by the pointer (a slider dragged follows the
// hand), a node moving by itself (its own transitions, glyph motion,
// effect, a component's motion said), the first drawing of a node (its
// enter slot's), and anything while motion is off or the time is held.
module openmui.view;

import std;
import openmui.core;
import openmui.hash;
import openmui.render.vector;
import openmui.motion;
import :impl;

namespace omui {

namespace {

constexpr std::size_t none = static_cast<std::size_t>(-1);

motion::Options options_of(const MotionSpec& spec, MotionLevel level) {
    motion::Options o;
    if (level == MotionLevel::reduced) {
        o.curve.kind = motion::Curve::Kind::tween;
        o.curve.duration = 0.12;
        o.curve.ease = hanim::ease::standard;
        o.rise = spec.rise * 0.25f;
        o.grow = 1 - (1 - spec.grow) * 0.25f;
        return o;
    }
    o.curve.kind = spec.curve.kind == Transition::Kind::spring ? motion::Curve::Kind::spring : motion::Curve::Kind::tween;
    o.curve.duration = spec.curve.duration.count();
    o.curve.ease = spec.curve.ease;
    o.curve.spring = spec.curve.spring;
    o.order = spec.order == MotionSpec::Order::document   ? motion::Order::document
              : spec.order == MotionSpec::Order::distance ? motion::Order::distance
                                                          : motion::Order::together;
    o.stagger = spec.stagger;
    o.glyphs = spec.glyphs;
    o.semantic = spec.meaning;
    o.rise = spec.rise;
    o.grow = spec.grow;
    o.stretch = spec.stretch;
    return o;
}

}  // namespace

const MotionSpec* Runtime::Impl::slot_motion(NodeId id, std::size_t slot) {
    static constexpr std::array<std::string_view, 3> slots{"update", "enter", "exit"};
    Node& n = at(id);
    if (n.motion_gen != motion_gen) {
        n.motion_gen = motion_gen;
        for (auto& m : n.slot_motions) m.reset();
        const Theme& theme = theme_of(id);
        const auto own = n.props.find("motion");
        // A map by slot ({update, enter, exit}), or one motion: the update slot's.
        const bool by_slot = own != n.props.end() && own->second.map() && !own->second.get("use") &&
                             (own->second.get("update") || own->second.get("enter") || own->second.get("exit"));
        Role role = n.type->contract ? n.type->contract->role : n.type->role;
        if (const auto r = n.props.find("role"); r != n.props.end())
            if (auto parsed = spec::parse_role(r->second.as_string())) role = *parsed;
        const std::string by_role = std::format("role:{}", spec::name(role));
        for (std::size_t k = 0; k < slots.size(); ++k) {
            const Value* v = own == n.props.end() ? nullptr : by_slot ? own->second.get(slots[k]) : k == 0 ? &own->second : nullptr;
            if (v) {
                if (auto spec = parse_motion(*v, theme)) n.slot_motions[k] = *spec;
                else {
                    constexpr Log motion_log{"motion"};
                    motion_log.warn("{}: a motion is \"none\", \"morph\", \"morph <curve>\" or {{use, curve, order, stagger, glyphs, meaning, rise, grow}}, not {}",
                                    self.path(id), v->json());
                }
                continue;
            }
            if (own != n.props.end() && !by_slot) continue;  // one motion given: the other slots hold none
            const MotionRule* best = nullptr;
            int rank = 0;
            for (const MotionRule& rule : theme.motion_rules) {
                if (rule.slot != slots[k]) continue;
                const int r = rule.target == n.type->name ? 3 : rule.target == by_role ? 2 : rule.target == "*" ? 1 : 0;
                if (r > rank) best = &rule, rank = r;
            }
            if (best) n.slot_motions[k] = best->spec;
        }
    }
    const auto& m = n.slot_motions[slot];
    return m && m->kind == MotionSpec::Kind::morph ? &*m : nullptr;
}

void Runtime::Impl::emit_motion_range(std::size_t begin, std::size_t end, render::DisplayList& out, double t) {
    const auto cmds = list.commands();
    for (std::size_t i = begin; i < end; ++i) {
        if (const auto* b = std::get_if<render::BeginNode>(&cmds[i]); b && b->node < marker_ranges.size() && marker_ranges[b->node].first == i) {
            const NodeId id = b->node < by_index.size() ? by_index[b->node] : NodeId{};
            if (alive(id) && at(id).morph) {
                out.begin_node(b->node);
                const std::size_t w = b->node < marker_wrapper.size() ? marker_wrapper[b->node] : none;
                const auto* live = w != none ? std::get_if<render::PushLayer>(&cmds[w]) : nullptr;
                at(id).morph->draw(t, out, [this, t](std::uint32_t marker, render::DisplayList& into) {
                    if (marker < marker_ranges.size() && marker_ranges[marker].first != none)
                        emit_motion_range(marker_ranges[marker].first, marker_ranges[marker].second, into, t);
                }, live);
                out.end_node();
                i = marker_ranges[b->node].second - 1;
                continue;
            }
        }
        out.append_command(cmds[i]);
    }
}

void Runtime::Impl::run_motion() {
    const double t = now().count();
    const bool off = reduce_motion || motion_level == MotionLevel::off ||
                     (time_control.controlled && (time_control.paused || time_control.rate != 1 || time_control.seek));
    if (!repainted) return;
    const auto cmds = list.commands();
    const std::size_t markers = by_index.size();

    // Where each marker's range is this frame.
    marker_ranges.assign(markers, {none, none});
    marker_flags.assign(markers, 0);
    marker_stack.clear();
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        if (const auto* b = std::get_if<render::BeginNode>(&cmds[i])) {
            if (b->node < markers) marker_ranges[b->node].first = i;
            marker_stack.push_back(b->node);
        } else if (std::holds_alternative<render::EndNode>(cmds[i]) && !marker_stack.empty()) {
            const std::size_t m = marker_stack.back();
            marker_stack.pop_back();
            if (m < markers) marker_ranges[m].second = i + 1;
        }
    }
    // Who moves by itself, and who is a root.
    auto flag = [&](NodeId id, std::uint8_t f) {
        if (alive(id) && at(id).index < markers) marker_flags[at(id).index] |= f;
    };
    for (const NodeId id : animating) flag(id, 2);
    for (const NodeId id : moving_said) flag(id, 2);
    for (std::size_t m = 1; m < markers; ++m) {
        if (marker_ranges[m].first == none || marker_ranges[m].second == none) continue;
        const NodeId id = by_index[m];
        if (!alive(id)) continue;
        const Node& n = at(id);
        // An effect's own transition (a style's pressed glass) is not a drawing's change: the morph draws it live.
        if (n.glyphs_moving(now()) || n.props_moving()) marker_flags[m] |= 2;
        if (update_motion(id) || slot_motion(id, 1) || n.exiting) marker_flags[m] |= 1;
    }

    // Each root: did it draw something else?
    for (std::size_t m = 1; m < markers; ++m) {
        if (!(marker_flags[m] & 1)) continue;
        const NodeId id = by_index[m];
        Node& n = at(id);
        const auto [b, e] = marker_ranges[m];
        std::uint64_t h = hash::Fnv1a::offset;
        bool by_itself = (marker_flags[m] & 2) != 0;
        for (std::size_t i = b; i < e; ++i) {
            // An effect's numbers are left out: their own transition is not a change of the drawing.
            if (const auto* l = std::get_if<render::PushLayer>(&cmds[i]); l && l->effect.id != 0) {
                render::PushLayer bare = *l;
                bare.params = {};
                h = render::hash_command(render::Command(bare), h);
                continue;
            }
            if (const auto* inner = std::get_if<render::BeginNode>(&cmds[i]); inner && i != b && inner->node < markers) {
                if (marker_flags[inner->node] & 1) {  // a nested root: its own
                    h = render::hash_command(cmds[i], h);
                    i = marker_ranges[inner->node].second - 1;
                    continue;
                }
                if (marker_flags[inner->node] & 2) by_itself = true;
            }
            h = render::hash_command(cmds[i], h);
        }
        // The resolver gives each marker its node's keys; a nested root's range is drawn whole.
        const std::size_t root_marker = m;
        motion::Resolver resolver{
            .keys = [this](std::uint32_t marker, std::string& key, std::string& meaning) {
                const NodeId who = marker < by_index.size() ? by_index[marker] : NodeId{};
                if (!alive(who)) return;
                const Node& w = at(who);
                key = who == root ? std::string("app") : self.path(who);
                Role role = w.type->contract ? w.type->contract->role : w.type->role;
                if (const auto r = w.props.find("role"); r != w.props.end())
                    if (auto parsed = spec::parse_role(r->second.as_string())) role = *parsed;
                if (const auto l = w.props.find("label"); l != w.props.end() && role != Role::generic && role != Role::none)
                    meaning = std::format("{}|{}", spec::name(role), l->second.as_string());
            },
            .nested = [this, root_marker](std::uint32_t marker) {
                return marker != root_marker && marker < marker_flags.size() && (marker_flags[marker] & 1);
            }};
        auto start = [&](std::optional<motion::Matter> from, motion::Matter to, const MotionSpec& spec, double velocity) {
            motion::Options options = options_of(spec, motion_level);
            options.curve.velocity = velocity;
            n.morph = std::make_shared<const motion::Morph>(std::move(*from), std::move(to), std::move(options), t);
            if (std::ranges::find(morphing, id) == morphing.end()) morphing.push_back(id);
            touch(dirty_semantics);
        };
        // Removed, leaving by its exit slot's motion: from what was on screen to nothing.
        if (n.exiting) {
            if (!n.exit_begun) {
                n.exit_begun = true;
                const MotionSpec* exit = slot_motion(id, 2);
                const auto was = motion::find_range(previous_list.commands(), static_cast<std::uint32_t>(m));
                if (exit && was && !off) start(motion::Matter::extract(previous_list, was->first, was->second, resolver), motion::Matter{}, *exit, 0);
            }
            continue;
        }
        const bool drawn_before = m < drawn_by.size() && drawn_by[m] == id;
        if (!n.drawn || (!drawn_before && (!n.morph || n.ghost))) {
            // New, its first drawing: its enter slot's, when it comes into a window already drawn. A node that
            // was there, hidden, drawn again (a dialog opening): its update slot's, from nothing.
            const bool fresh = !n.shown;
            n.drawn = true;
            n.drawn_hash = h;
            const MotionSpec* spec = fresh ? slot_motion(id, 1) : update_motion(id);
            const NodeId parent = n.parent;
            const bool into_drawn = n.type->overlay || (alive(parent) && at(parent).index < drawn_by.size() && drawn_by[at(parent).index] == parent);
            // Hidden, then drawn again before its ghost had vanished: from where the ghost is.
            std::optional<motion::Matter> from;
            if (n.ghost && n.morph) from = n.morph->snapshot(t);
            else from = motion::Matter{};
            n.ghost = false;
            if (spec && into_drawn && !off) start(std::move(from), motion::Matter::extract(list, b, e, resolver), *spec, 0);
            else n.morph.reset();
            continue;
        }
        if (h == n.drawn_hash || !update_motion(id)) {
            n.drawn_hash = h;
            continue;
        }
        n.drawn_hash = h;
        // Changing on and on (a meter, a playhead, a scroll gliding, a camera flying): it moves by itself, shown as it
        // is; a morph is for a change, not for a motion.
        const bool continuous = t - n.changed_at < 0.05;
        n.changed_at = t;
        if (continuous) {
            if (n.morph) n.morph.reset(), touch(dirty_semantics);
            continue;
        }
        // Held by the pointer (dragged), moving by itself, or motion off: shown as it is.
        const bool held = alive(capture) && (capture == id || contains(id, capture));
        if (off || held || by_itself) {
            if (n.morph) n.morph.reset(), touch(dirty_semantics);
            continue;
        }
        // From what was on screen: the morph under way where it is, else the last frame's drawing of it.
        // The last frame's markers drawn by nodes that are gone: theirs, not whoever took the marker since.
        motion::Resolver before = resolver;
        before.keys = [this, keys = resolver.keys](std::uint32_t marker, std::string& key, std::string& meaning) {
            const NodeId now_by = marker < by_index.size() ? by_index[marker] : NodeId{};
            const NodeId then_by = marker < drawn_by.size() ? drawn_by[marker] : NodeId{};
            if (!(now_by == then_by)) {
                key = std::format("gone:{}", marker);
                return;
            }
            keys(marker, key, meaning);
        };
        std::optional<motion::Matter> from;
        double velocity = 0;
        if (n.morph) {
            velocity = std::clamp(n.morph->rate(t), 0.0, 20.0);
            from = n.morph->snapshot(t, [this, t](std::uint32_t marker, render::DisplayList& into) {
                if (marker < marker_ranges.size() && marker_ranges[marker].first != none)
                    emit_motion_range(marker_ranges[marker].first, marker_ranges[marker].second, into, t);
            });
        } else if (const auto was = motion::find_range(previous_list.commands(), static_cast<std::uint32_t>(m))) {
            from = motion::Matter::extract(previous_list, was->first, was->second, before);
        }
        if (!from) continue;
        start(std::move(from), motion::Matter::extract(list, b, e, resolver), *update_motion(id), velocity);
    }

    // Roots drawn the last frame, alive, not drawn now (hidden): their last drawing vanishes as a ghost.
    for (std::size_t m = 1; m < drawn_by.size() && m < markers; ++m) {
        const NodeId id = drawn_by[m];
        if (!(id == by_index[m]) || !alive(id) || marker_ranges[m].first != none) continue;
        Node& n = at(id);
        if (n.ghost || n.exiting || n.state.removing || off || !update_motion(id)) continue;
        // Its parent drawn still (else its whole branch went) — an overlay stands alone.
        const NodeId parent = n.parent;
        if (!n.type->overlay && !(alive(parent) && at(parent).index < markers && marker_ranges[at(parent).index].first != none)) continue;
        const auto was = motion::find_range(previous_list.commands(), static_cast<std::uint32_t>(m));
        if (!was) continue;
        motion::Resolver keys{.keys = [this](std::uint32_t marker, std::string& key, std::string&) {
                                  const NodeId who = marker < by_index.size() ? by_index[marker] : NodeId{};
                                  if (alive(who)) key = self.path(who);
                              }};
        std::optional<motion::Matter> from;
        if (n.morph) from = n.morph->snapshot(t);
        else from = motion::Matter::extract(previous_list, was->first, was->second, keys);
        motion::Options options = options_of(*update_motion(id), motion_level);
        n.morph = std::make_shared<const motion::Morph>(std::move(*from), motion::Matter{}, std::move(options), t);
        n.ghost = true;
        n.drawn = false;  // drawn again later: from nothing
        if (std::ranges::find(morphing, id) == morphing.end()) morphing.push_back(id);
        touch(dirty_semantics);
    }

    // Morphs at rest are done (kept while a tool holds the time — paused, slowed, sought: it may go back).
    const bool held = time_control.controlled && (time_control.paused || time_control.rate != 1 || time_control.seek);
    std::erase_if(morphing, [&](NodeId id) {
        if (!alive(id) || !at(id).morph) return true;
        if (at(id).morph->settled(t) && !held) {
            at(id).morph.reset();
            at(id).ghost = false;
            touch(dirty_semantics);
            return true;
        }
        return false;
    });

    // The frame as drawn: each morphing root's range by its morph (with its wrapper as drawn now).
    if (!morphing.empty()) {
        marker_wrapper.assign(markers, none);
        for (const NodeId id : morphing)
            if (alive(id) && at(id).index < markers && marker_ranges[at(id).index].first != none) {
                const auto [rb, re] = marker_ranges[at(id).index];
                if (const auto w = motion::wrapper_of(cmds, rb, re)) marker_wrapper[at(id).index] = *w;
            }
        morph_scratch.clear();
        morph_scratch.reserve(cmds.size() + 64);
        emit_motion_range(0, cmds.size(), morph_scratch, t);
        // Ghosts over it all, each under its parent's transform as it was laid out.
        for (const NodeId id : morphing) {
            if (!alive(id) || !at(id).ghost || !at(id).morph) continue;
            const Node& n = at(id);
            const Affine parent = n.type->overlay || !alive(n.parent) ? Affine{} : at(n.parent).world;
            morph_scratch.push_transform(parent);
            morph_scratch.begin_node(n.index);
            n.morph->draw(t, morph_scratch);
            morph_scratch.end_node();
            morph_scratch.pop_transform();
        }
        std::swap(list, morph_scratch);
        touch(dirty_paint);  // the next frame draws them further
    }
    // Who drew each marker: the next frame's "before" is this drawing.
    drawn_by.assign(markers, NodeId{});
    for (std::size_t m = 1; m < markers; ++m)
        if (marker_ranges[m].first != none) drawn_by[m] = by_index[m];
}

void Runtime::set_motion_level(MotionLevel level) {
    Impl& d = *impl_;
    if (d.motion_level == level) return;
    d.motion_level = level;
    ++d.motion_gen;
    d.touch(dirty_paint);
}

MotionLevel Runtime::motion_level() const noexcept { return impl_->motion_level; }

std::vector<MotionState> Runtime::motions() const {
    const Impl& d = *impl_;
    std::vector<MotionState> out;
    const double t = d.now().count();
    for (const NodeId id : d.morphing) {
        if (!d.alive(id) || !d.at(id).morph) continue;
        const motion::Morph& m = *d.at(id).morph;
        const auto& st = m.stats();
        out.push_back({id, path(id), m.progress(t), m.start(), m.end(), st.matched, st.from_only, st.to_only, st.glyphs_kept});
    }
    return out;
}

}  // namespace omui
