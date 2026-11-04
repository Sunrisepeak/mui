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

motion::Options options_of(const MotionSpec& spec, MotionLevel level, const Theme& theme) {
    motion::Options o;
    o.glyphs = spec.glyphs;
    o.text = spec.text == MotionSpec::TextTransition::automatic ? motion::TextTransition::automatic
           : spec.text == MotionSpec::TextTransition::replace ? motion::TextTransition::replace
                                                           : motion::TextTransition::glyphs;
    o.text_rise = level == MotionLevel::reduced ? 0 : spec.text_rise;
    o.semantic = spec.meaning;
    o.minimum_text_contrast = spec.minimum_text_contrast;
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
    o.rise = spec.rise;
    o.grow = spec.grow;
    o.stretch = spec.stretch;
    o.fade_out = spec.fade_out;
    o.fade_in = spec.fade_in;
    o.local_presence = spec.local_presence;
    if (spec.travel)
        if (const auto ref = render::effects().find(spec.travel->effect)) {
            motion::Options::Travel t{.effect = *ref};
            t.params = render::effects().params(*ref, {});
            if (const render::EffectInfo* info = render::effects().info(*ref)) {
                for (const auto& [k, x] : spec.travel->params)
                    for (std::size_t i = 0; i < info->params.size() && i < t.params.size(); ++i)
                        if (info->params[i].name == k) t.params[i] = x;
                for (std::size_t i = 0; i < info->params.size(); ++i) {
                    if (info->params[i].name == spec.travel->param) t.param = static_cast<int>(i);
                    // Its colour by the theme's name: the accent of whatever theme it is drawn in.
                    if (!spec.travel->color.empty() && i + 2 < t.params.size() && info->params[i].name.ends_with("_r") &&
                        info->params[i + 1].name.ends_with("_g") && info->params[i + 2].name.ends_with("_b")) {
                        const Color c = theme.color(spec.travel->color);
                        t.params[i] = c.r, t.params[i + 1] = c.g, t.params[i + 2] = c.b;
                    }
                }
            }
            t.peak = spec.travel->peak;
            o.travel = std::move(t);
        }
    return o;
}

}  // namespace

const MotionSpec* Runtime::Impl::slot_motion(NodeId id, std::size_t slot, bool fallback) {
    Node& n = at(id);
    if (n.motion_gen != motion_gen) {
        // Its slots resolved again (a property, a theme, a level, a tier, a choice changed), the layers over each
        // other (universal motion §5.2): the framework's defaults and the style's rules (the theme's), the
        // application's (its rules there too), the node's own `motion`; the person's choice and the device below.
        n.motion_gen = motion_gen;
        for (auto& m : n.slot_motions) m.clear();
        const Theme& theme = theme_of(id);
        const auto own = n.props.find("motion");
        // A map by slot ({update, enter, …}), or one motion (or candidates): the update slot's.
        const bool by_slot = own != n.props.end() && own->second.map() && !own->second.get("use") &&
                             std::ranges::any_of(motion_slots, [&](std::string_view k) { return own->second.get(k) != nullptr; });
        Role role = n.type->contract ? n.type->contract->role : n.type->role;
        if (const auto r = n.props.find("role"); r != n.props.end())
            if (auto parsed = spec::parse_role(r->second.as_string())) role = *parsed;
        const std::string by_role = std::format("role:{}", spec::name(role));
        for (std::size_t k = 0; k < motion_slots.size(); ++k) {
            const Value* v = own == n.props.end() ? nullptr : by_slot ? own->second.get(motion_slots[k]) : k == 0 ? &own->second : nullptr;
            if (v) {
                if (auto specs = parse_motions(*v, theme)) n.slot_motions[k] = std::move(*specs);
                else {
                    constexpr Log motion_log{"motion"};
                    motion_log.warn("{}: a motion is \"none\", \"morph\", a library's name (\"liquid\"), either with a curve after it, "
                                    "{{use, curve, order, stagger, glyphs, text, text_rise, text_contrast, meaning, rise, grow, stretch, travel}}, or a list of them; not {}",
                                    self.path(id), v->json());
                }
                continue;
            }
            if (own != n.props.end() && !by_slot) continue;  // one motion given: the other slots hold none
            const MotionRule* best = nullptr;
            int rank = 0;
            for (const MotionRule& rule : theme.motion_rules) {
                if (rule.slot != motion_slots[k]) continue;
                const int specificity = rule.target == n.type->name ? 3 : rule.target == by_role ? 2 : rule.target == "*" ? 1 : 0;
                const int r = specificity ? 4 * static_cast<int>(rule.layer) + specificity : 0;
                if (r > 0 && r >= rank) best = &rule, rank = r;
            }
            if (best) {
                n.slot_motions[k].push_back(best->spec);
                n.slot_motions[k].insert(n.slot_motions[k].end(), best->instead.begin(), best->instead.end());
            }
        }
    }
    const auto& candidates = n.slot_motions[slot];
    if (candidates.empty()) return fallback && slot >= 3 ? slot_motion(id, 0) : nullptr;
    // Fits: its travel effect runs at the effects' tier now (else the next is taken).
    const pipeline::Tier tier = effects_now();
    auto fits = [&](const MotionSpec& m) {
        if (!m.travel) return true;
        const auto ref = render::effects().find(m.travel->effect);
        const render::EffectInfo* info = ref ? render::effects().info(*ref) : nullptr;
        return info && (info->tiers & pipeline::tier_bit(tier));
    };
    const MotionSpec* chosen = nullptr;
    if (!motion_choice.empty())
        for (const MotionSpec& m : candidates)
            if (m.name == motion_choice && fits(m)) {
                chosen = &m;
                break;
            }
    if (!chosen)
        for (const MotionSpec& m : candidates)
            if (fits(m)) {
                chosen = &m;
                break;
            }
    return chosen && chosen->kind == MotionSpec::Kind::morph ? chosen : nullptr;
}

void Runtime::Impl::emit_motion_range(std::size_t begin, std::size_t end, render::DisplayList& out, double t) {
    const auto cmds = list.commands();
    for (std::size_t i = begin; i < end; ++i) {
        if (history_range.second > history_range.first && i == history_range.first) {
            // The old frame remains a balanced visual layer with its original
            // renderer ownership. Never replace its repeated node markers by
            // current live morphs; copy the whole layer without interpreting it.
            emitted_history_range.first = out.size();
            for (std::size_t j = i; j < history_range.second; ++j) out.append_command(cmds[j]);
            emitted_history_range.second = out.size();
            i = history_range.second - 1;
            continue;
        }
        if (const auto* b = std::get_if<render::BeginNode>(&cmds[i]); b && b->node < marker_ranges.size() && marker_ranges[b->node].first == i) {
            const NodeId id = b->node < by_index.size() ? by_index[b->node] : NodeId{};
            if (alive(id) && at(id).morph) {
                out.begin_node(b->node);
                const std::size_t w = b->node < marker_wrapper.size() ? marker_wrapper[b->node] : none;
                const auto* live = w != none ? std::get_if<render::PushLayer>(&cmds[w]) : nullptr;
                at(id).morph->draw(at(id).clock.local(t), out, [this, t](std::uint32_t marker, render::DisplayList& into) {
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

bool Runtime::Impl::motion_moving(NodeId id) const {
    if (time_control.controlled && (time_control.paused || time_control.rate == 0)) return false;
    if (!alive(id)) return false;
    const auto& node = at(id);
    if (!node.morph || node.clock.paused || node.clock.rate == 0) return false;
    const double global_rate = time_control.controlled ? time_control.rate : 1;
    const double local = node.clock.local(now().count());
    return node.clock.rate * global_rate > 0 ? local < node.morph->end() : local > node.morph->start();
}

bool Runtime::Impl::motions_moving() const {
    return std::ranges::any_of(morphing, [&](NodeId id) { return motion_moving(id); });
}

void Runtime::Impl::run_motion() {
    const double t = now().count();
    const MotionLevel level = motion_now();
    const bool off = reduce_motion || level == MotionLevel::off ||
                     (time_control.controlled && (time_control.paused || time_control.rate != 1 || time_control.seek));
    if (!repainted) return;
    const auto cmds = list.commands();
    const std::size_t markers = by_index.size();
    auto previous_range = [&](std::uint32_t marker) -> std::optional<std::pair<std::size_t, std::size_t>> {
        const auto before = previous_list.commands();
        if (previous_history_range.second > previous_history_range.first) {
            // Current overlays follow history in paint order. Searching only
            // the first prefix would lose those current ranges; searching the
            // entire list would instead find their historical copies first.
            if (auto range = motion::find_range(before.first(previous_history_range.first), marker)) return range;
            return motion::find_range(before, marker, previous_history_range.second);
        }
        return motion::find_range(before, marker);
    };
    auto snapshot_context = [&](std::uint32_t marker) {
        motion::SnapshotContext context;
        if (const auto range = previous_range(marker)) context.prefix = previous_list.commands().first(range->first);
        return context;
    };

    // Where each marker's range is this frame.
    marker_ranges.assign(markers, {none, none});
    marker_flags.assign(markers, 0);
    marker_stack.clear();
    for (std::size_t i = 0; i < cmds.size(); ++i) {
        if (history_range.second > history_range.first && i == history_range.first) {
            i = history_range.second - 1;
            continue;
        }
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
        if (moves_on_change(id) || slot_motion(id, 1) || n.exiting) marker_flags[m] |= 1;
    }

    // Each root: did it draw something else?
    for (std::size_t m = 1; m < markers; ++m) {
        if (!(marker_flags[m] & 1)) continue;
        const NodeId id = by_index[m];
        Node& n = at(id);
        const auto [b, e] = marker_ranges[m];
        std::uint64_t h = hash::Fnv1a::offset;
        bool by_itself = (marker_flags[m] & 2) != 0;
        // Its subtree's property writes and interaction states (which slot a change is: a state's, or an update's).
        auto state_bits = [](const Node& x) -> std::uint64_t {
            return (x.state.hovered ? 1u : 0u) | (x.state.pressed ? 2u : 0u) | (x.state.focused ? 4u : 0u) | (x.state.focus_visible ? 8u : 0u);
        };
        std::uint64_t changes = n.changes, states = state_bits(n);
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
                    const NodeId child = by_index[inner->node];
                    if (alive(child)) {
                        const MotionSpec* spec = update_motion(child);
                        if (spec && spec->placement == MotionSpec::PlacementPolicy::follow_layout) {
                            // Default feedback owns state paint, while this
                            // root owns the child's layout envelope. Without
                            // the allocation in this comparison, a row/column
                            // reflow containing only default-motion controls
                            // looks unchanged and snaps directly to its end.
                            // Do not include animated visual properties here:
                            // hover/press feedback must remain independent.
                            const Rect allocation = at(child).frame;
                            h = hash::Fnv1a(h).f32(allocation.x).f32(allocation.y)
                                .f32(allocation.width).f32(allocation.height).value();
                        }
                    }
                    i = marker_ranges[inner->node].second - 1;
                    continue;
                }
                if (marker_flags[inner->node] & 2) by_itself = true;
                if (const NodeId who = by_index[inner->node]; alive(who)) {
                    changes += at(who).changes;
                    states = states * 1099511628211ull ^ state_bits(at(who));
                }
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
            },
            .nested_placement = [this](std::uint32_t marker) {
                const NodeId child = marker < by_index.size() ? by_index[marker] : NodeId{};
                if (!alive(child)) return false;
                const MotionSpec* spec = update_motion(child);
                return spec && spec->placement == MotionSpec::PlacementPolicy::follow_layout;
            }};
        const bool external_layout = std::ranges::any_of(constraint_boundaries, [&](NodeId boundary) {
            return alive(boundary) && (contains(boundary, id) || contains(id, boundary));
        });
        const bool rebase_existing = external_layout && n.constraint_epoch != constraint_epoch && n.morph;
        n.constraint_epoch = constraint_epoch;
        auto start = [&](std::optional<motion::Matter> from, motion::Matter to, const MotionSpec& spec, double velocity) {
            // More pieces than the device's tier moves in a frame (performance tiers §3): shown as it is.
            const std::size_t pieces = (from ? from->pieces().size() : 0) + to.pieces().size();
            constexpr std::array<std::size_t, 5> most{0, 300, 3000, 20000, 60000};
            if (pieces > most[static_cast<std::size_t>(adaptation.tier)]) {
                n.morph.reset();
                return;
            }
            motion::Options options = options_of(spec, level, theme_of(id));
            options.proof_scale = scale;
            options.current_proof_scale = [this] { return scale; };
            if (options.minimum_text_contrast > 0) {
                if (!motion_glyphs) motion_glyphs = std::make_unique<typeset::GlyphCache>(*fonts);
                options.glyph_bounds = [cache = motion_glyphs.get(), lock = &motion_glyph_mutex](const render::DrawGlyphs& run, const Affine& transform) {
                    const std::scoped_lock guard(*lock);
                    return motion::glyph_ink_bounds(run, transform, *cache);
                };
            }
            options.curve.velocity = velocity;
            options.rebase_layout = spec.placement == MotionSpec::PlacementPolicy::follow_layout ||
                (spec.placement == MotionSpec::PlacementPolicy::automatic && external_layout);
            if (options.rebase_layout && spec.placement == MotionSpec::PlacementPolicy::automatic &&
                std::ranges::find(constraint_boundaries, root) == constraint_boundaries.end())
                for (NodeId boundary : constraint_boundaries)
                    if (alive(boundary) && (contains(boundary, id) || contains(id, boundary))) options.layout_scopes.push_back(self.path(boundary));
            const auto previous_clock = n.clock;
            const bool keep_control = external_layout && n.morph && n.clock.held;
            n.morph = std::make_shared<const motion::Morph>(std::move(*from), std::move(to), std::move(options), t);
            n.clock = {};  // a new content change follows runtime time again
            if (keep_control) {
                n.clock = previous_clock;
                n.clock.at = t;
                n.clock.at_local = t; // rebased source is sampled exactly, even while held
            }
            if (std::ranges::find(morphing, id) == morphing.end()) morphing.push_back(id);
            touch(dirty_semantics);
        };
        // Removed, leaving by its exit slot's motion: from what was on screen to nothing.
        if (n.exiting) {
            if (!n.exit_begun) {
                n.exit_begun = true;
                const MotionSpec* exit = slot_motion(id, 2);
                const auto was = previous_range(static_cast<std::uint32_t>(m));
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
            n.drawn_hash = h, n.drawn_changes = changes, n.drawn_states = states, n.drawn_children = n.children;
            const MotionSpec* spec = fresh ? slot_motion(id, 1) : update_motion(id);
            const NodeId parent = n.parent;
            const bool into_drawn = n.type->overlay || (alive(parent) && at(parent).index < drawn_by.size() && drawn_by[at(parent).index] == parent);
            // Hidden, then drawn again before its ghost had vanished: from where the ghost is.
            std::optional<motion::Matter> from;
            if (n.ghost && n.morph) from = n.morph->snapshot(n.clock.local(t), {}, snapshot_context(static_cast<std::uint32_t>(m)));
            else from = motion::Matter{};
            n.ghost = false;
            if (spec && into_drawn && !off) start(std::move(from), motion::Matter::extract(list, b, e, resolver), *spec, 0);
            else n.morph.reset();
            continue;
        }
        // Which slot the change is: an order (the same children in another), a replacement (none of the children
        // it had), a state (only hover, press, focus changed in it), else an update.
        std::size_t slot = 0;
        if (h != n.drawn_hash) {
            const auto& was = n.drawn_children;
            const auto& is = n.children;
            if (was.size() == is.size() && was.size() <= 256 && was != is && std::ranges::is_permutation(was, is)) slot = 4;
            else if (!was.empty() && !is.empty() && is.size() <= 256 &&
                     std::ranges::none_of(is, [&](NodeId c) { return std::ranges::find(was, c) != was.end(); }))
                slot = 5;
            else if (changes == n.drawn_changes && states != n.drawn_states) slot = 3;
        }
        const MotionSpec* spec = slot_motion(id, slot);
        if ((h == n.drawn_hash && !rebase_existing) || !spec) {
            // A direct content update supersedes a focus/hover transition.
            // In particular, typing must not replay an older text drawing.
            if (!spec && h != n.drawn_hash && n.morph) {
                n.morph.reset();
                touch(dirty_semantics);
            }
            n.drawn_hash = h, n.drawn_changes = changes, n.drawn_states = states, n.drawn_children = n.children;
            continue;
        }
        n.drawn_hash = h, n.drawn_changes = changes, n.drawn_states = states, n.drawn_children = n.children;
        // Changing on and on (a meter, a playhead, a scroll gliding, a camera flying): it moves by itself, shown as it
        // is; a morph is for a change, not for a motion.
        const bool continuous = t - n.changed_at < 0.05;
        n.changed_at = t;
        if (continuous && !external_layout) {
            if (n.morph) n.morph.reset(), touch(dirty_semantics);
            continue;
        }
        // Held by the pointer (dragged), moving by itself, or motion off: shown as it is.
        const bool held = alive(capture) && (capture == id || contains(id, capture));
        const bool rebase_held = external_layout && !reduce_motion && level != MotionLevel::off;
        if ((off && !rebase_held) || held || by_itself) {
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
            velocity = std::clamp(n.morph->rate(n.clock.local(t)), 0.0, 20.0);
            from = n.morph->snapshot(n.clock.local(t), [this, t](std::uint32_t marker, render::DisplayList& into) {
                if (marker < marker_ranges.size() && marker_ranges[marker].first != none)
                    emit_motion_range(marker_ranges[marker].first, marker_ranges[marker].second, into, t);
            }, snapshot_context(static_cast<std::uint32_t>(m)));
        } else if (const auto was = previous_range(static_cast<std::uint32_t>(m))) {
            from = motion::Matter::extract(previous_list, was->first, was->second, before);
        }
        if (!from) continue;
        start(std::move(from), motion::Matter::extract(list, b, e, resolver), *spec, velocity);
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
        const auto was = previous_range(static_cast<std::uint32_t>(m));
        if (!was) continue;
        motion::Resolver keys{.keys = [this](std::uint32_t marker, std::string& key, std::string&) {
                                  const NodeId who = marker < by_index.size() ? by_index[marker] : NodeId{};
                                  if (alive(who)) key = self.path(who);
                              }};
        std::optional<motion::Matter> from;
        if (n.morph) from = n.morph->snapshot(n.clock.local(t), {}, snapshot_context(static_cast<std::uint32_t>(m)));
        else from = motion::Matter::extract(previous_list, was->first, was->second, keys);
        motion::Options options = options_of(*update_motion(id), level, theme_of(id));
        n.morph = std::make_shared<const motion::Morph>(std::move(*from), motion::Matter{}, std::move(options), t);
        n.clock = {};
        n.ghost = true;
        n.drawn = false;  // drawn again later: from nothing
        if (std::ranges::find(morphing, id) == morphing.end()) morphing.push_back(id);
        touch(dirty_semantics);
    }

    // Morphs at rest are done (kept while a tool holds the time — paused, slowed, sought: it may go back).
    const bool held = time_control.controlled && (time_control.paused || time_control.rate != 1 || time_control.seek);
    std::erase_if(morphing, [&](NodeId id) {
        if (!alive(id) || !at(id).morph) return true;
        // Held by a handle: kept, at rest or not, until it is released.
        if (at(id).morph->settled(at(id).clock.local(t)) && !held && !at(id).clock.held) {
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
        emitted_history_range = {};
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
            n.morph->draw(n.clock.local(t), morph_scratch);
            morph_scratch.end_node();
            morph_scratch.pop_transform();
        }
        std::swap(list, morph_scratch);
        history_range = emitted_history_range;
        if (motions_moving()) touch(dirty_paint);  // the next frame draws moving clocks further
    }
    // Who drew each marker: the next frame's "before" is this drawing.
    drawn_by.assign(markers, NodeId{});
    for (std::size_t m = 1; m < markers; ++m)
        if (marker_ranges[m].first != none) drawn_by[m] = by_index[m];
    collect_presentation();
}

void Runtime::set_motion_level(MotionLevel level) {
    Impl& d = *impl_;
    if (d.motion_level == level) return;
    d.motion_level = level;
    ++d.motion_gen;
    d.touch(dirty_paint);
}

MotionLevel Runtime::motion_level() const noexcept { return impl_->motion_level; }

void Runtime::set_motion_choice(std::string name) {
    Impl& d = *impl_;
    if (d.motion_choice == name) return;
    d.motion_choice = std::move(name);
    ++d.motion_gen;
    d.touch(dirty_paint);
}

const std::string& Runtime::motion_choice() const noexcept { return impl_->motion_choice; }

bool Runtime::hold_motion(NodeId node) {
    Impl& d = *impl_;
    if (!d.alive(node) || !d.at(node).morph) return false;
    Node& n = d.at(node);
    if (n.clock.held) return true;
    n.clock.held = true;
    return true;
}

void Runtime::release_motion(NodeId node) {
    Impl& d = *impl_;
    if (!d.alive(node) || !d.at(node).clock.held) return;
    Node& n = d.at(node);
    // On from where it is, at the runtime's pace (at rest already: done at the next frame).
    const double t = d.now().count();
    n.clock = {.at = t, .at_local = n.clock.local(t), .rate = 1, .paused = false, .held = false};
    d.touch(dirty_paint | dirty_semantics);
}

void Runtime::pause_motion(NodeId node) {
    Impl& d = *impl_;
    if (!hold_motion(node)) return;
    Node& n = d.at(node);
    const double t = d.now().count();
    n.clock.at_local = n.clock.local(t), n.clock.at = t, n.clock.paused = true;
    d.touch(dirty_paint);
}

void Runtime::play_motion(NodeId node) {
    Impl& d = *impl_;
    if (!hold_motion(node)) return;
    Node& n = d.at(node);
    n.clock.at = d.now().count(), n.clock.paused = false;
    d.touch(dirty_paint);
}

void Runtime::seek_motion(NodeId node, double seconds) {
    Impl& d = *impl_;
    if (!hold_motion(node)) return;
    Node& n = d.at(node);
    n.clock.at = d.now().count(), n.clock.at_local = n.morph->start() + std::clamp(seconds, 0.0, n.morph->end() - n.morph->start());
    d.touch(dirty_paint);
}

void Runtime::set_motion_rate(NodeId node, double rate) {
    Impl& d = *impl_;
    if (!hold_motion(node)) return;
    Node& n = d.at(node);
    const double t = d.now().count();
    n.clock.at_local = n.clock.local(t), n.clock.at = t, n.clock.rate = rate;
    d.touch(dirty_paint);
}

std::optional<std::pair<double, double>> Runtime::motion_time(NodeId node) const {
    const Impl& d = *impl_;
    if (!d.alive(node) || !d.at(node).morph) return std::nullopt;
    const Node& n = d.at(node);
    return std::pair{n.morph->end() - n.morph->start(), n.clock.local(d.now().count()) - n.morph->start()};
}

std::vector<MotionState> Runtime::motions() const {
    const Impl& d = *impl_;
    std::vector<MotionState> out;
    const double t = d.now().count();
    for (const NodeId id : d.morphing) {
        if (!d.alive(id) || !d.at(id).morph) continue;
        const motion::Morph& m = *d.at(id).morph;
        const auto& st = m.stats();
        out.push_back({id, path(id), m.progress(d.at(id).clock.local(t)), m.start(), m.end(), st.matched, st.from_only, st.to_only, st.glyphs_kept});
    }
    return out;
}

}  // namespace omui
