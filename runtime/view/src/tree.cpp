// The node tree: creating nodes from elements, reconciling regions by key,
// structural ids (S-8) and paths, removal with exit transitions (S-4).
module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import :impl;

namespace omui {

namespace {

Size measure_trampoline(void* context, layout::NodeId lnode, std::optional<float> width, std::optional<float> height,
                        layout::Available aw, layout::Available ah) {
    auto* impl = static_cast<Runtime::Impl*>(context);
    NodeId id = impl->by_layout[lnode];
    Node& n = impl->at(id);
    MeasureContext m{impl->self, id};
    return n.type->measure(n.instance, m, width, height, aw, ah);
}

float baseline_trampoline(void* context, layout::NodeId lnode, Size content) {
    auto* impl = static_cast<Runtime::Impl*>(context);
    NodeId id = impl->by_layout[lnode];
    Node& n = impl->at(id);
    MeasureContext m{impl->self, id};
    return n.type->baseline(n.instance, m, content);
}

const ComponentType& type_of(const Element& e) { return e.type ? *e.type : *find_type("box"); }

std::string key_of(const Element& e) { return e.key.empty() ? e.id : e.key; }

}  // namespace

NodeId Runtime::Impl::create(Element& e, NodeId parent) {
    const ComponentType& type = type_of(e);
    Node n;
    n.type = &type;
    n.instance = type.create();
    n.parent = parent;
    n.where = e.where;
    if (!e.id.empty()) {
        if (e.generated_id ? !spec::valid_segment(e.id) : !spec::valid_explicit_segment(e.id))
            warn("invalid explicit id '{}' (spec S-8: 1-128 of [A-Za-z0-9_.-]); at {}:{}", e.id, e.where.file_name(),
                    e.where.line());
        n.segment = e.id;
        n.explicit_id = true;
    }
    n.key = key_of(e);
    n.layout = layout.create();
    if (free_indices.empty()) {
        n.index = static_cast<std::uint32_t>(by_index.size());
        by_index.emplace_back();
    } else {
        n.index = free_indices.back();
        free_indices.pop_back();
    }
    n.handlers = std::move(e.handlers);
    for (auto& [k, t] : e.transitions) n.transitions[k] = t;
    if (e.configure) e.configure(n.instance);

    const std::uint32_t index = n.index;
    const layout::NodeId lnode = n.layout;
    NodeId id = nodes.insert(std::move(n));
    by_index[index] = id;
    if (by_layout.size() <= lnode) by_layout.resize(lnode + 1);
    by_layout[lnode] = id;

    if (type.measure) layout.set_measure(lnode, &measure_trampoline, this, type.baseline ? &baseline_trampoline : nullptr);
    if (type.overlay) overlays.push_back(id);
    if (type.laid_out) watchers.push_back(id);
    if (type.mount) type.mount(at(id).instance, self, id);

    for (auto& [name, source] : e.props) {
        at(id).element_props.push_back(name);
        set_source(id, name, source);
    }
    sync_style(id);

    std::vector<ChildSlot> slots = reconcile(id, {}, e.children);
    at(id).slots = std::move(slots);
    if (type.compose) {
        // The component's own children: a region whose builder asks the component.
        auto rs = std::make_shared<RegionState>();
        rs->parent = id;
        rs->region = std::make_shared<Region>(Region{[this, id] {
            Node& n = at(id);
            return n.type->compose(n.instance);
        }});
        run_region(rs);
        at(id).composed = rs;
        at(id).slots.push_back({{}, rs});
    }
    flatten(id);
    if (at(id).transitions.contains("enter")) enter(id);
    else fresh.push_back(id);
    touch(dirty_tree | dirty_layout | dirty_paint);
    return id;
}

void Runtime::Impl::destroy_slot(ChildSlot& slot) {
    if (slot.region) {
        slot.region->watch.dispose();
        for (ChildSlot& s : slot.region->slots) destroy_slot(s);
        slot.region->slots.clear();
        slot.region->dirty = false;
    } else {
        destroy(slot.node);
    }
}

void Runtime::Impl::destroy(NodeId id) {
    if (!alive(id)) return;
    Node& n = at(id);
    n.bindings.clear();  // no binding fires for a node being destroyed
    std::vector<ChildSlot> slots = std::move(n.slots);
    for (ChildSlot& s : slots) destroy_slot(s);
    // Children still playing their exit transition go with their parent.
    std::vector<NodeId> exiting;
    for (NodeId r : removing)
        if (alive(r) && at(r).parent == id) exiting.push_back(r);
    for (NodeId r : exiting) destroy(r);

    Node& m = at(id);
    if (m.type->destroy) m.type->destroy(m.instance);
    for (anim::Set::Channel c : m.channels)
        if (anims.valid(c)) anims.remove(c);
    layout.destroy(m.layout);
    by_layout[m.layout] = {};
    by_index[m.index] = {};
    free_indices.push_back(m.index);
    if (focus == id) focus = {};
    if (capture == id) capture = {};
    if (pressed == id) pressed = {};
    if (text_input_node == id) {
        text_input_node = {};
        text_input.reset();
    }
    std::erase(hover_chain, id);
    std::erase(animating, id);
    std::erase(removing, id);
    std::erase(fresh, id);
    std::erase(overlays, id);
    std::erase(watchers, id);
    std::erase_if(runtime_handlers, [&](const RuntimeHandler& h) { return h.node == id; });
    nodes.erase(id);
    paths_dirty = true;
    touch(dirty_tree | dirty_layout | dirty_paint);
}

std::vector<ChildSlot> Runtime::Impl::reconcile(NodeId parent, std::vector<ChildSlot> old, std::vector<Element>& elements) {
    std::vector<ChildSlot> out;
    out.reserve(elements.size());
    std::vector<bool> used(old.size(), false);
    // Keys are copied: creating nodes below may move the node storage.
    std::unordered_map<std::string, std::size_t> keyed;
    for (std::size_t i = 0; i < old.size(); ++i)
        if (old[i].node && alive(old[i].node) && !at(old[i].node).key.empty()) keyed.emplace(at(old[i].node).key, i);

    for (Element& e : elements) {
        if (e.region) {
            std::size_t j = 0;
            while (j < old.size() && (used[j] || !old[j].region)) ++j;
            std::shared_ptr<RegionState> rs;
            if (j < old.size()) {
                used[j] = true;
                rs = old[j].region;
                rs->region = e.region;
            } else {
                rs = std::make_shared<RegionState>();
                rs->parent = parent;
                rs->region = e.region;
            }
            run_region(rs);
            out.push_back({{}, std::move(rs)});
            continue;
        }
        const ComponentType& type = type_of(e);
        const std::string key = key_of(e);
        std::optional<std::size_t> match;
        if (!key.empty()) {
            if (auto it = keyed.find(key); it != keyed.end() && !used[it->second] && at(old[it->second].node).type == &type)
                match = it->second;
        } else {
            for (std::size_t j = 0; j < old.size(); ++j)
                if (!used[j] && old[j].node && alive(old[j].node) && at(old[j].node).key.empty() &&
                    at(old[j].node).type == &type) {
                    match = j;
                    break;
                }
        }
        if (match) {
            used[*match] = true;
            update_from(old[*match].node, e);
            out.push_back({old[*match].node, {}});
        } else {
            out.push_back({create(e, parent), {}});
        }
    }
    for (std::size_t j = 0; j < old.size(); ++j) {
        if (used[j]) continue;
        if (old[j].node) start_removal(old[j].node);
        else destroy_slot(old[j]);
    }
    return out;
}

void Runtime::Impl::run_region(const std::shared_ptr<RegionState>& rs) {
    RegionState* raw = rs.get();
    std::weak_ptr<RegionState> weak = rs;
    raw->building = true;
    raw->watch = Watch([this, raw, weak] {
        raw->pending = raw->region->build();
        if (raw->building) return;
        if (!raw->dirty) {
            raw->dirty = true;
            dirty_regions.push_back(weak);
        }
        frame_requested = true;
    });
    raw->building = false;
    raw->dirty = false;
    std::vector<Element> pending = std::move(raw->pending);
    raw->pending.clear();
    raw->slots = reconcile(raw->parent, std::move(raw->slots), pending);
}

void Runtime::Impl::reconcile_region(RegionState& rs) {
    std::vector<Element> pending = std::move(rs.pending);
    rs.pending.clear();
    rs.dirty = false;
    rs.slots = reconcile(rs.parent, std::move(rs.slots), pending);
    flatten(rs.parent);
}

void Runtime::Impl::reconcile_regions() {
    for (int round = 0; round < 64 && !dirty_regions.empty(); ++round) {
        std::vector<std::weak_ptr<RegionState>> work;
        work.swap(dirty_regions);
        for (auto& w : work)
            if (auto rs = w.lock(); rs && rs->dirty && alive(rs->parent)) reconcile_region(*rs);
    }
    if (!dirty_regions.empty()) {
        error("regions kept rebuilding for 64 rounds: a region's builder writes what it reads");
        dirty_regions.clear();
    }
}

void Runtime::Impl::update_from(NodeId id, Element& e) {
    Node& n = at(id);
    if (n.explicit_id != !e.id.empty() || (n.explicit_id && n.segment != e.id)) {
        n.explicit_id = !e.id.empty();
        if (n.explicit_id) n.segment = e.id;
        paths_dirty = true;
    }
    n.where = e.where;
    n.handlers = std::move(e.handlers);
    n.transitions.clear();
    for (auto& [k, t] : e.transitions) n.transitions[k] = t;
    if (e.configure) {
        e.configure(n.instance);
        touch(dirty_paint | dirty_layout);
        layout.mark_dirty(n.layout);
    }

    std::vector<std::string> previous = std::move(n.element_props);
    n.element_props.clear();
    for (auto& [name, source] : e.props) {
        at(id).element_props.push_back(name);
        Node& cur = at(id);
        if (!source.reactive() && !cur.bindings.contains(name) && source.constant) {
            auto it = cur.props.find(name);
            if (it != cur.props.end() && it->second == *source.constant) continue;
        }
        set_source(id, name, source);
    }
    for (const std::string& name : previous)
        if (std::ranges::find(at(id).element_props, name) == at(id).element_props.end()) clear_prop(id, name);

    std::vector<ChildSlot> old = std::move(at(id).slots);
    std::shared_ptr<RegionState> composed = at(id).composed;
    if (composed) std::erase_if(old, [&](const ChildSlot& s) { return s.region == composed; });
    std::vector<ChildSlot> slots = reconcile(id, std::move(old), e.children);
    if (composed) slots.push_back({{}, composed});
    at(id).slots = std::move(slots);
    flatten(id);
}

void Runtime::Impl::flatten(NodeId id) {
    std::vector<NodeId> out;
    auto walk = [&](auto& self, const std::vector<ChildSlot>& slots) -> void {
        for (const ChildSlot& s : slots) {
            if (s.region) self(self, s.region->slots);
            else if (alive(s.node)) out.push_back(s.node);
        }
    };
    walk(walk, at(id).slots);
    for (NodeId r : removing)
        if (alive(r) && at(r).parent == id) out.push_back(r);
    Node& n = at(id);
    if (out != n.children) {
        n.children = std::move(out);
        layout.clear_children(n.layout);
        // Overlays are laid out from the root, wherever they are in the tree.
        const layout::NodeId top = at(root).layout;
        for (NodeId c : n.children) layout.append(at(c).type->overlay ? top : n.layout, at(c).layout);
        if (id == root)
            for (NodeId o : overlays)
                if (alive(o) && o != id) layout.append(top, at(o).layout);
        assign_segments(id);
        touch(dirty_tree | dirty_layout | dirty_paint);
    }
}

void Runtime::Impl::assign_segments(NodeId parent) {
    std::unordered_map<std::string_view, std::size_t> count;
    std::unordered_set<std::string_view> explicit_ids;
    for (NodeId c : at(parent).children) {
        Node& child = at(c);
        if (child.state.removing) continue;
        std::size_t i = count[child.type->name]++;
        if (child.explicit_id) {
            if (!explicit_ids.insert(child.segment).second)
                warn("two siblings have the id '{}' (spec S-8: ids are unique among siblings); at {}:{}", child.segment,
                        child.where.file_name(), child.where.line());
            continue;
        }
        std::string s = spec::structural_segment(child.type->name, i);
        if (s != child.segment) {
            child.segment = std::move(s);
            paths_dirty = true;
        }
    }
    paths_dirty = true;
}

void Runtime::Impl::rebuild_paths() {
    if (!paths_dirty) return;
    path_index.clear();
    auto walk = [&](auto& self, NodeId id, const std::string& prefix) -> void {
        for (NodeId c : at(id).children) {
            const Node& child = at(c);
            if (child.state.removing) continue;
            std::string p = prefix.empty() ? child.segment : std::format("{}/{}", prefix, child.segment);
            path_index.try_emplace(p, c);
            self(self, c, p);
        }
    };
    walk(walk, root, std::string{});
    paths_dirty = false;
}

void Runtime::Impl::enter(NodeId id) {
    Node& n = at(id);
    const float target = n.visual[Field::opacity];
    n.visual[Field::opacity] = 0;
    n.shown = true;  // the entrance itself animates
    animate_field(id, "enter", Field::opacity, target);
}

void Runtime::Impl::start_removal(NodeId id) {
    if (!alive(id)) return;
    Node& n = at(id);
    auto exit = n.transitions.find("exit");
    if (exit == n.transitions.end() || !n.shown || reduce_motion) {
        destroy(id);
        return;
    }
    // Out of the flow at its last place, fading out; queries see it as
    // removing (S-4) and actions on it are refused.
    n.state.removing = true;
    n.bindings.clear();
    layout::Style s = layout.style(n.layout);
    s.position = layout::Position::absolute;
    s.left = layout::pt(n.frame.x);
    s.top = layout::pt(n.frame.y);
    s.width = layout::pt(n.frame.width);
    s.height = layout::pt(n.frame.height);
    s.right = s.bottom = layout::Length::automatic();
    s.margin = {};
    layout.set_style(n.layout, s);
    removing.push_back(id);
    animate_field(id, "exit", Field::opacity, 0);
    if (focus == id) focus = {};
    paths_dirty = true;
    touch(dirty_tree | dirty_layout | dirty_paint);
}

void Runtime::Impl::finish_removals() {
    std::vector<NodeId> done;
    for (NodeId r : removing)
        if (!alive(r) || (at(r).moving & (1u << static_cast<unsigned>(Field::opacity))) == 0) done.push_back(r);
    for (NodeId r : done) {
        if (!alive(r)) {
            std::erase(removing, r);
            continue;
        }
        NodeId parent = at(r).parent;
        destroy(r);
        if (alive(parent)) flatten(parent);
    }
}

}  // namespace omui
