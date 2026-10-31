// The public face of the runtime: mounting, tree queries, properties, the
// services components use.
module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import openmui.render.vector;
import openmui.text;
import :impl;

namespace omui {

namespace {

struct TasksParallel final : render::Parallel {
    void run(std::size_t count, FunctionRef<void(std::size_t)> body) override {
        Tasks::shared().parallel_for(count, 1, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) body(i);
        });
    }
};

}  // namespace

render::Parallel& raster_parallel() noexcept {
    static TasksParallel p;
    return p;
}

namespace {

Value default_of(const spec::DefaultValue& d) {
    using K = spec::DefaultValue::Kind;
    switch (d.kind) {
        case K::none: return {};
        case K::boolean: return Value(d.b);
        case K::integer: return Value(d.i);
        case K::real: return Value(d.d);
        case K::text: return Value(d.s);
        case K::list: {
            Value::List l;
            for (std::string_view s : d.list) l.emplace_back(s);
            return Value(std::move(l));
        }
    }
    return {};
}

}  // namespace

Runtime::Runtime(RuntimeOptions options) : impl_(std::make_unique<Impl>(*this)) {
    Impl& d = *impl_;
    d.theme = std::move(options.theme);
    d.make_opaque();
    d.clock = options.clock;
    d.complete_frames = options.complete_frames;
    d.size = options.size;
    d.scale = options.scale;
    d.reduce_motion = options.reduce_motion;
    d.reduce_transparency = options.reduce_transparency;
    d.make_opaque();
    if (options.fonts) {
        d.fonts = options.fonts;
    } else {
        d.own_fonts = std::make_unique<typeset::FontCollection>();
        if (auto r = d.own_fonts->add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        d.fonts = d.own_fonts.get();
    }
    d.options = std::move(options);
    d.by_index.emplace_back();  // index 0 marks no node
    Element root;
    root.type = &root_type();
    d.root = d.create(root, {});
    d.build_pipeline();
}

Runtime::~Runtime() {
    // Component instances and bindings go first, while the runtime they use exists.
    impl_->destroy(impl_->root);
}

Signal<std::optional<Element>> Runtime::outlet(std::string_view name) {
    Impl& d = *impl_;
    auto it = d.outlets.find(name);
    if (it == d.outlets.end()) it = d.outlets.emplace(std::string(name), Signal<std::optional<Element>>{}).first;
    return it->second;
}

void Runtime::Impl::reconcile_outlets() {
    for (auto& [name, o] : outlets) {
        auto view = mounted_views.find(name);
        if (view == mounted_views.end()) continue;
        if (o.observed() && !in_outlet.contains(name)) {
            // A region claims the name: what is at the top level moves into it.
            if (auto it = std::ranges::find(mounted, name, &std::pair<std::string, NodeId>::first); it != mounted.end()) {
                const NodeId id = it->second;
                mounted.erase(it);
                std::erase_if(at(root).slots, [&](const ChildSlot& s) { return s.node == id; });
                destroy(id);
                flatten(root);
            }
            in_outlet.insert(name);
            o.set(view->second);
        } else if (!o.observed() && in_outlet.contains(name)) {
            // The region went: the view goes back to the top level.
            in_outlet.erase(name);
            o.set(std::nullopt);
            Element e = view->second;
            const NodeId id = create(e, root);
            at(root).slots.push_back({id, {}});
            mounted.emplace_back(name, id);
            flatten(root);
        }
    }
}

NodeId Runtime::mount(std::string_view slot, View view) {
    Impl& d = *impl_;
    // The slot is the view's top-level id: paths begin with it.
    if (!view.id.empty() && view.id != slot)
        omui::debug("the view mounted at '{}' had the id '{}'; the slot names it", slot, view.id);
    view.id = std::string(slot);
    if (view.key.empty()) view.key = view.id;
    d.mounted_views.insert_or_assign(std::string(slot), view);
    // A region shows this name: the view goes there (see reconcile_outlets).
    if (auto o = d.outlets.find(slot); o != d.outlets.end() && o->second.observed()) {
        if (std::ranges::find(d.mounted, slot, &std::pair<std::string, NodeId>::first) != d.mounted.end()) unmount(slot);
        d.mounted_views.insert_or_assign(std::string(slot), view);  // unmount forgot it
        d.in_outlet.insert(std::string(slot));
        o->second.set(std::move(view));
        return NodeId{};
    }
    auto it = std::ranges::find(d.mounted, slot, &std::pair<std::string, NodeId>::first);
    if (it != d.mounted.end() && d.alive(it->second) && d.at(it->second).type == (view.type ? view.type : find_type("box"))) {
        // The same kind of view again: update it in place (state, focus and animations survive).
        NodeId id = it->second;
        d.update_from(id, view);
        return id;
    }
    if (it != d.mounted.end()) unmount(slot);
    NodeId id = d.create(view, d.root);
    d.at(d.root).slots.push_back({id, {}});
    d.mounted.emplace_back(std::string(slot), id);
    if (d.slot_insets.contains(slot)) d.sync_style(id);
    d.flatten(d.root);
    return id;
}

NodeId Runtime::mount_beneath(std::string_view slot, View view) {
    Impl& d = *impl_;
    const NodeId id = mount(slot, std::move(view));
    if (!d.alive(id)) return id;
    auto& slots = d.at(d.root).slots;
    if (auto it = std::ranges::find(slots, id, &ChildSlot::node); it != slots.end() && it != slots.begin()) {
        ChildSlot moved = std::move(*it);
        slots.erase(it);
        slots.insert(slots.begin(), std::move(moved));
    }
    if (auto it = std::ranges::find(d.mounted, slot, &std::pair<std::string, NodeId>::first); it != d.mounted.end() && it != d.mounted.begin())
        std::rotate(d.mounted.begin(), it, it + 1);
    d.flatten(d.root);
    return id;
}

void Runtime::set_slot_insets(std::string_view slot, Insets insets) {
    Impl& d = *impl_;
    const bool none = insets.top == 0 && insets.right == 0 && insets.bottom == 0 && insets.left == 0;
    if (none) {
        if (auto it = d.slot_insets.find(slot); it != d.slot_insets.end()) d.slot_insets.erase(it);
    } else {
        d.slot_insets.insert_or_assign(std::string(slot), insets);
    }
    if (auto it = std::ranges::find(d.mounted, slot, &std::pair<std::string, NodeId>::first); it != d.mounted.end() && d.alive(it->second))
        d.sync_style(it->second);
}

Insets Runtime::slot_insets(std::string_view slot) const {
    const auto it = impl_->slot_insets.find(slot);
    return it == impl_->slot_insets.end() ? Insets{} : it->second;
}

void Runtime::unmount(std::string_view slot) {
    Impl& d = *impl_;
    d.forget_history(slot);  // what was done in it goes with it (S-12)
    if (auto v = d.mounted_views.find(slot); v != d.mounted_views.end()) d.mounted_views.erase(v);
    if (auto o = d.in_outlet.find(slot); o != d.in_outlet.end()) {
        d.in_outlet.erase(o);
        d.outlets.at(std::string(slot)).set(std::nullopt);
        return;
    }
    auto it = std::ranges::find(d.mounted, slot, &std::pair<std::string, NodeId>::first);
    if (it == d.mounted.end()) return;
    NodeId id = it->second;
    d.mounted.erase(it);
    std::erase_if(d.at(d.root).slots, [&](const ChildSlot& s) { return s.node == id; });
    d.destroy(id);
    d.flatten(d.root);
}

NodeId Runtime::root() const noexcept { return impl_->root; }
bool Runtime::valid(NodeId node) const noexcept { return impl_->alive(node); }

NodeId Runtime::find(std::string_view path) const {
    Impl& d = *impl_;
    if (path.empty() || path == "app") return d.root;
    d.rebuild_paths();
    auto it = d.path_index.find(std::string(path));
    return it == d.path_index.end() ? NodeId{} : it->second;
}

std::string Runtime::path(NodeId node) const {
    const Impl& d = *impl_;
    if (!d.alive(node) || node == d.root) return {};
    std::vector<std::string_view> parts;
    for (NodeId c = node; d.alive(c) && c != d.root; c = d.at(c).parent) parts.push_back(d.at(c).segment);
    std::string out;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (!out.empty()) out += '/';
        out += *it;
    }
    return out;
}

NodeId Runtime::parent(NodeId node) const { return valid(node) ? impl_->at(node).parent : NodeId{}; }
std::vector<NodeId> Runtime::children(NodeId node) const { return valid(node) ? impl_->at(node).children : std::vector<NodeId>{}; }
const ComponentType& Runtime::type(NodeId node) const { return valid(node) ? *impl_->at(node).type : root_type(); }
std::source_location Runtime::origin(NodeId node) const { return valid(node) ? impl_->at(node).where : std::source_location{}; }

Value Runtime::get(NodeId node, std::string_view property) const {
    if (!valid(node)) return {};
    const Node& n = impl_->at(node);
    if (n.type->get_prop) {
        Value v = n.type->get_prop(n.instance, property);
        if (!v.is_null()) return v;
    }
    auto it = n.props.find(property);
    if (it != n.props.end()) return it->second;
    if (n.type->contract)
        if (const spec::PropInfo* p = n.type->contract->prop(property)) return default_of(p->default_value);
    return {};
}

void Runtime::set(NodeId node, std::string_view property, const Value& value) {
    Impl& d = *impl_;
    if (!d.alive(node)) return;
    Node& n = d.at(node);
    if (auto it = n.sources.find(property); it != n.sources.end() && it->second.write) {
        auto write = it->second.write;
        write(value);  // two-way: the source learns it, the binding applies it
        return;
    }
    if (auto it = n.bindings.find(property); it != n.bindings.end()) n.bindings.erase(it);
    d.apply(node, property, value);
}

void Runtime::bind(NodeId node, std::string property, Source source) {
    Impl& d = *impl_;
    if (!d.alive(node)) return;
    d.set_source(node, property, source);
}

std::uint64_t Runtime::on(NodeId node, std::string event, Handler handler) {
    Impl& d = *impl_;
    std::uint64_t token = d.next_token++;
    d.runtime_handlers.push_back({token, node, std::move(event), std::move(handler)});
    d.touch(dirty_semantics);
    return token;
}

void Runtime::off(std::uint64_t token) {
    std::erase_if(impl_->runtime_handlers, [&](const Impl::RuntimeHandler& h) { return h.token == token; });
    impl_->touch(dirty_semantics);
}

void Runtime::walk(FunctionRef<void(NodeId, int)> visit) const {
    const Impl& d = *impl_;
    auto go = [&](auto& self, NodeId id, int depth) -> void {
        visit(id, depth);
        for (NodeId c : d.at(id).children) self(self, c, depth + 1);
    };
    go(go, d.root, 0);
}

std::size_t Runtime::node_count() const noexcept { return impl_->nodes.size(); }

NodeId Runtime::node_at_index(std::uint32_t index) const {
    const Impl& d = *impl_;
    return index < d.by_index.size() ? d.by_index[index] : NodeId{};
}

const render::DisplayList& Runtime::display_list() const noexcept { return impl_->list; }

void Runtime::resize(Size size, float scale) {
    Impl& d = *impl_;
    if (size.width == d.size.width && size.height == d.size.height && scale == d.scale) return;
    d.size = size;
    d.scale = scale;
    d.layout.mark_dirty(d.at(d.root).layout);
    d.touch(dirty_layout | dirty_paint | dirty_semantics);
}

Size Runtime::size() const noexcept { return impl_->size; }
float Runtime::scale() const noexcept { return impl_->scale; }
hanim::Clock& Runtime::clock() noexcept { return impl_->clock; }
void* Runtime::instance_of(NodeId node, const ComponentType& type) noexcept {
    if (!valid(node)) return nullptr;
    const Node& n = impl_->at(node);
    return n.type == &type ? n.instance : nullptr;
}

const void* Runtime::instance_of(NodeId node, const ComponentType& type) const noexcept {
    if (!valid(node)) return nullptr;
    const Node& n = impl_->at(node);
    return n.type == &type ? n.instance : nullptr;
}

double Runtime::frame_rate() const noexcept {
    const auto now = std::chrono::steady_clock::now();
    return static_cast<double>(std::ranges::count_if(impl_->drawn, [&](auto t) {
        return t != std::chrono::steady_clock::time_point{} && now - t <= std::chrono::seconds(1);
    }));
}

void Runtime::frame_shown(ShownFrame f) noexcept { impl_->shown.push(f); }

const ShownFrames& Runtime::shown_frames() const noexcept { return impl_->shown; }

void Runtime::set_renderer(std::string name) { impl_->renderer = std::move(name); }

std::string_view Runtime::renderer() const noexcept { return impl_->renderer; }

void Runtime::set_input_actor(std::optional<Actor> actor) { impl_->input_actor = std::move(actor); }

FrameDemandLease Runtime::demand_frames(FrameDemand d) {
    const std::uint64_t token = impl_->next_lease++;
    impl_->leases.emplace_back(token, std::move(d));
    request_frame();
    return {this, token, impl_->life};
}

void Runtime::release_frames(std::uint64_t token) noexcept {
    std::erase_if(impl_->leases, [&](const auto& l) { return l.first == token; });
}

const FrameDemand& Runtime::frame_demand() const noexcept { return impl_->demand; }

void Runtime::set_effect_rules(std::vector<EffectRule> rules) {
    Impl& d = *impl_;
    d.effect_rules = std::move(rules);
    std::vector<NodeId> all;
    walk([&](NodeId id, int) { all.push_back(id); });
    d.apply_rules(d.root);
    for (NodeId id : all) d.apply_rules(id);
    d.touch(dirty_paint);
}

const std::vector<EffectRule>& Runtime::effect_rules() const noexcept { return impl_->effect_rules; }

void Runtime::crossfade(seconds length) {
    Impl& d = *impl_;
    if (!(length.count() > 0) || d.list.commands().empty()) return;
    render::DisplayList before;
    before.append(d.list);
    d.fading = std::move(before);
    d.fade_clip.reset();
    d.fade_start = d.now();
    d.fade_length = length;
    d.frame_requested = true;
    d.touch(dirty_paint | dirty_semantics);  // the window animating until the fade is done (an agent's wait for a still waits)
}

void Runtime::crossfade(seconds length, std::string_view scope) {
    Impl& d = *impl_;
    const std::uint32_t s = d.scope_named(scope);
    if (s == 0) return;
    // The area of the nodes that join the scope, as last laid out.
    std::optional<Rect> area;
    walk([&](NodeId id, int) {
        if (d.at(id).scope == s && d.scope_root(id)) area = area ? area->united(d.at(id).bounds) : d.at(id).bounds;
    });
    if (!area) return;
    crossfade(length);
    if (d.fading) d.fade_clip = area;
}
double Runtime::display_hz() const noexcept { return impl_->display_hz; }

bool Runtime::deterministic() const { return impl_->clock.kind() != hanim::ClockKind::interactive; }
bool Runtime::complete_frames() const { return impl_->complete_frames; }
NodeId Runtime::focused() const noexcept { return impl_->focus; }
Verbs& Runtime::verbs() noexcept { return impl_->verbs; }
UiQueue& Runtime::queue() noexcept { return impl_->queue; }

void Runtime::set_theme(Theme theme) {
    Impl& d = *impl_;
    d.theme = std::move(theme);
    d.make_opaque();
    std::vector<NodeId> all;
    walk([&](NodeId id, int) { all.push_back(id); });
    for (NodeId id : all)
        if (d.at(id).scope == 0) d.restyle(id);  // a scope's nodes keep their own
    d.touch(dirty_layout | dirty_paint | dirty_semantics);
}

// ── theme scopes ──

const Theme& Runtime::Impl::theme_of(NodeId id) const {
    const std::uint32_t s = alive(id) ? at(id).scope : 0;
    if (reduce_transparency) return s == 0 ? opaque_theme : scopes[s].opaque;
    return s == 0 ? theme : scopes[s].theme;
}

namespace {

// A theme with less transparency: each translucent colour a surface is drawn in — a panel, a bar, a control's
// fill — made solid, as it looks over the theme's background; what lies over things to mark them (focus,
// selection, a scrim, a shadow) keeps its alpha.
Theme opaque_of(const Theme& t) {
    Theme o = t;
    Color under = t.color("background", t.dark ? Color::hex(0x0b1120) : Color::hex(0xffffff));
    if (under.a < 1) {
        const Color base = t.dark ? Color::hex(0x000000) : Color::hex(0xffffff);
        under = {base.r + (under.r - base.r) * under.a, base.g + (under.g - base.g) * under.a, base.b + (under.b - base.b) * under.a, 1};
    }
    for (auto& [name, c] : o.colors) {
        if (c.a >= 1 || name == "focus" || name == "selection" || name.contains("scrim") || name.contains("shadow") || name.contains("overlay")) continue;
        c = {under.r + (c.r - under.r) * c.a, under.g + (c.g - under.g) * c.a, under.b + (c.b - under.b) * c.a, 1};
    }
    return o;
}

}  // namespace

void Runtime::Impl::make_opaque() {
    if (!reduce_transparency) return;
    opaque_theme = opaque_of(theme);
    for (Scope& sc : scopes) sc.opaque = opaque_of(sc.theme);
}

const std::vector<EffectRule>& Runtime::Impl::rules_of(NodeId id) const {
    const std::uint32_t s = alive(id) ? at(id).scope : 0;
    return s == 0 ? effect_rules : scopes[s].rules;
}

std::uint32_t Runtime::Impl::scope_named(std::string_view name) const noexcept {
    if (name.empty()) return 0;
    for (std::uint32_t i = 1; i < scopes.size(); ++i)
        if (scopes[i].name == name) return i;
    return 0;
}

bool Runtime::Impl::scope_root(NodeId id) const {
    const Node& n = at(id);
    return n.scope != 0 && (!alive(n.parent) || at(n.parent).scope != n.scope);
}

void Runtime::Impl::restyle(NodeId id) {
    Node& n = at(id);
    {
        auto use = use_theme(id);
        if (n.type->restyle) n.type->restyle(n.instance);
    }
    if (n.type->measure) layout.mark_dirty(n.layout);
    sync_style(id);
    resolve_visual(id);
}

void Runtime::Impl::rescope(NodeId id, std::uint32_t inherited, std::uint32_t restyle_scope) {
    if (!alive(id)) return;
    std::uint32_t mine = inherited;
    if (auto it = at(id).props.find("theme_scope"); it != at(id).props.end())
        if (const std::uint32_t named = scope_named(it->second.as_string()); named != 0) mine = named;
    const bool changed = at(id).scope != mine;
    at(id).scope = mine;
    if (changed || (restyle_scope != 0 && mine == restyle_scope)) {
        restyle(id);
        apply_rules(id);  // the scope's rules, or the runtime's again
    }
    const std::vector<NodeId> children = at(id).children;
    for (NodeId c : children) rescope(c, mine, restyle_scope);
}

void Runtime::set_scope(std::string_view name, Theme theme, std::vector<EffectRule> rules) {
    Impl& d = *impl_;
    if (name.empty()) return;
    std::uint32_t s = d.scope_named(name);
    if (s == 0) {
        s = static_cast<std::uint32_t>(d.scopes.size());
        for (std::uint32_t i = 1; i < d.scopes.size(); ++i)
            if (d.scopes[i].name.empty()) {
                s = i;
                break;
            }
        if (s == d.scopes.size()) d.scopes.emplace_back();
        d.scopes[s].name = std::string(name);
    }
    d.scopes[s].theme = std::move(theme);
    d.scopes[s].rules = std::move(rules);
    d.make_opaque();
    if (d.alive(d.root)) d.rescope(d.root, 0, s);
    d.touch(dirty_layout | dirty_paint | dirty_semantics);
}

void Runtime::remove_scope(std::string_view name) {
    Impl& d = *impl_;
    const std::uint32_t s = d.scope_named(name);
    if (s == 0) return;
    d.scopes[s] = {};
    if (d.alive(d.root)) d.rescope(d.root, 0);
    d.touch(dirty_layout | dirty_paint | dirty_semantics);
}

const Theme* Runtime::scope_theme(std::string_view name) const {
    const std::uint32_t s = impl_->scope_named(name);
    return s == 0 ? nullptr : &impl_->scopes[s].theme;
}

const std::vector<EffectRule>* Runtime::scope_rules(std::string_view name) const {
    const std::uint32_t s = impl_->scope_named(name);
    return s == 0 ? nullptr : &impl_->scopes[s].rules;
}

const Theme& Runtime::theme_of(NodeId node) const { return impl_->theme_of(node); }

void Runtime::set_reduce_motion(bool reduce) {
    impl_->reduce_motion = reduce;
    impl_->touch(dirty_paint);
}

void Runtime::set_reduce_transparency(bool reduce) {
    Impl& d = *impl_;
    if (d.reduce_transparency == reduce) return;
    d.reduce_transparency = reduce;
    d.make_opaque();
    // Every node's colours again, from the theme it now draws with.
    std::vector<NodeId> all;
    walk([&](NodeId id, int) { all.push_back(id); });
    for (NodeId id : all) d.restyle(id);
    d.touch(dirty_layout | dirty_paint | dirty_semantics);
}

bool Runtime::reduce_transparency() const { return impl_->reduce_transparency; }
bool Runtime::reduce_motion() const noexcept { return impl_->reduce_motion; }

const Theme& Runtime::theme() const { return impl_->current_theme ? *impl_->current_theme : impl_->theme; }
typeset::FontCollection& Runtime::fonts() { return *impl_->fonts; }
render::ImageStore& Runtime::images() { return impl_->images; }
hanim::seconds Runtime::now() const { return impl_->now(); }
Rect Runtime::bounds(NodeId node) const { return valid(node) ? impl_->at(node).bounds : Rect{}; }
Rect Runtime::screen_bounds(NodeId node) const { return valid(node) ? impl_->at(node).screen : Rect{}; }
Rect Runtime::drawn_bounds(NodeId node) const {
    if (!valid(node)) return {};
    const auto& n = impl_->at(node);
    return n.world.map({0, 0, n.frame.width, n.frame.height});
}
NodeState Runtime::state(NodeId node) const { return valid(node) ? impl_->at(node).state : NodeState{}; }

void Runtime::emit(NodeId node, std::string_view event, const Value& args) { impl_->call_handlers(node, event, args); }

void Runtime::write_back(NodeId node, std::string_view property, const Value& value) {
    Impl& d = *impl_;
    if (!d.alive(node)) return;
    Node& n = d.at(node);
    if (auto it = n.sources.find(property); it != n.sources.end() && it->second.write) {
        auto write = it->second.write;
        write(value);
    }
    // The node keeps the value whether or not a source took it (an unbound
    // slider still moves).
    if (d.alive(node)) {
        Node& m = d.at(node);
        auto cur = m.props.find(property);
        if (cur == m.props.end() || !(cur->second == value)) {
            m.props.insert_or_assign(std::string(property), value);
            d.touch(dirty_paint | dirty_semantics);
        }
    }
}

void Runtime::invalidate_paint(NodeId) { impl_->touch(dirty_paint); }

void Runtime::invalidate_layout(NodeId node) {
    if (valid(node)) impl_->layout.mark_dirty(impl_->at(node).layout);
    impl_->touch(dirty_layout | dirty_paint);
}

void Runtime::invalidate_semantics(NodeId) { impl_->touch(dirty_semantics); }

void Runtime::restyle(NodeId node) {
    if (valid(node)) impl_->sync_style(node);
}

Model& Runtime::model(std::string_view name) {
    auto& slot = impl_->models[std::string(name)];
    if (!slot) slot = std::make_unique<Model>(std::string(name));
    return *slot;
}

NodeId Runtime::reveal(std::string_view path) {
    Impl& d = *impl_;
    if (NodeId n = find(path); valid(n)) return n;
    // The deepest node on the path that exists, and the segment after it.
    std::size_t cut = path.size();
    while (true) {
        cut = path.rfind('/', cut == 0 ? 0 : cut - 1);
        if (cut == std::string_view::npos) return {};
        const NodeId holder = find(path.substr(0, cut));
        if (!valid(holder)) continue;
        Node& h = d.at(holder);
        if (!h.type->reveal) return {};
        std::string_view rest = path.substr(cut + 1);
        const std::string_view key = rest.substr(0, rest.find('/'));
        if (!h.type->reveal(h.instance, key)) return {};
        d.reconcile_regions();
        d.paths_dirty = true;
        if (NodeId n = find(path); valid(n)) return n;
        // Deeper virtual levels: materialise the next one the same way.
        const NodeId next = find(path.substr(0, cut + 1 + key.size()));
        return valid(next) && next != holder ? reveal(path) : NodeId{};
    }
}

std::vector<spec::SemanticNode> Runtime::virtual_items(NodeId node) const {
    if (!valid(node)) return {};
    const Node& n = impl_->at(node);
    if (!n.type->virtual_items) return {};
    return n.type->virtual_items(n.instance, path(node));
}

void Runtime::recompose(NodeId node) {
    Impl& d = *impl_;
    if (!d.alive(node) || !d.at(node).composed) return;
    RegionState& rs = *d.at(node).composed;
    rs.pending = untracked([&] {
        auto use = d.use_theme(node);
        return d.at(node).type->compose(d.at(node).instance);
    });
    if (!rs.dirty) {
        rs.dirty = true;
        d.dirty_regions.push_back(d.at(node).composed);
    }
    d.frame_requested = true;
}
void Runtime::request_frame() { impl_->frame_requested = true; }
void Runtime::scroll_into_view(NodeId node) {
    if (impl_->alive(node)) (void)impl_->perform(node, "scroll_into_view", {}, true);
}
void Runtime::focus(NodeId node) { impl_->set_focus(node, impl_->focus_visible); }
void Runtime::capture_pointer(NodeId node) { impl_->capture = node; }
void Runtime::release_pointer() { impl_->capture = {}; }

bool Runtime::perform(NodeId node, std::string_view action, const Value& args) {
    Invocation call;
    call.actor = {"person", "user", {}};  // the input being handled is a person's (openas §2.7)
    call.args = args;
    return act(node, action, call).exit_code() == std::optional<int>(0);
}

void Runtime::performed(NodeId node, std::string_view action, const Value& args, std::vector<Inverse> inverse) {
    Impl& d = *impl_;
    if (!d.alive(node)) return;
    Invocation given;
    given.actor = {"person", "user", {}};
    given.args = args;
    Invocation copy;
    const Invocation& call = d.attributed(given, copy);  // an agent's input: the agent's
    const std::string where = path(node);
    d.record(d.scope_of(node), {node, where, {}, std::string(action), args, std::move(inverse), std::format("{} on '{}'", action, where)}, Effect::reversible);
    d.journal.push_back({where, std::string(action), call.actor.text(), 0});
}

void Runtime::text_input(NodeId node, std::optional<Rect> caret) {
    Impl& d = *impl_;
    if (!caret) {
        if (d.text_input_node == node) {
            d.text_input_node = {};
            d.text_input.reset();
        }
        return;
    }
    d.text_input_node = node;
    const Rect b = bounds(node);
    d.text_input = caret->offset({b.x, b.y});
}

std::string Runtime::clipboard_text() { return impl_->clipboard_read ? impl_->clipboard_read() : impl_->clipboard; }

void Runtime::set_clipboard_text(std::string_view text) {
    if (impl_->clipboard_write) impl_->clipboard_write(text);
    else impl_->clipboard = std::string(text);
}

void Runtime::set_clipboard(std::function<std::string()> read, std::function<void(std::string_view)> write) {
    impl_->clipboard_read = std::move(read);
    impl_->clipboard_write = std::move(write);
}

bool Runtime::open_url(std::string_view url) {
    const bool allowed = url.starts_with("https://") || url.starts_with("http://") || url.starts_with("mailto:");
    if (!allowed) return false;
    impl_->opened.emplace_back(url);
    return impl_->url_opener && impl_->url_opener(url);
}

void Runtime::set_url_opener(std::function<bool(std::string_view)> open) { impl_->url_opener = std::move(open); }

const std::vector<std::string>& Runtime::opened() const noexcept { return impl_->opened; }

std::string_view Runtime::cursor() const {
    const Impl& d = *impl_;
    for (NodeId id : d.hover_chain) {
        if (!d.alive(id)) continue;
        const Node& n = d.at(id);
        if (auto it = n.props.find("cursor"); it != n.props.end() && !it->second.as_string().empty()) return it->second.as_string();
        if (!n.type->cursor.empty()) return n.type->cursor;
    }
    return "default";
}
hanim::Set& Runtime::animations() { return impl_->anims; }
std::optional<Rect> Runtime::text_input_rect() const noexcept { return impl_->text_input; }

}  // namespace omui
