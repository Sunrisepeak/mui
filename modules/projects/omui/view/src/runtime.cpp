// The public face of the runtime: mounting, tree queries, properties, the
// services components use.
module omui.view;

import std;
import omui.core;
import mlibs.agent;
import mlibs.layout;
import xrender.vector;
import mlibs.text;
import :impl;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::memory::FunctionRef;
using mlibs::log::warn;
}

namespace omui {

namespace {

struct TasksParallel final : xrender::Parallel {
    void run(std::size_t count, FunctionRef<void(std::size_t)> body) override {
        Tasks::shared().parallel_for(count, 1, [&](std::size_t begin, std::size_t end) {
            for (std::size_t i = begin; i < end; ++i) body(i);
        });
    }
};

}  // namespace

xrender::Parallel& raster_parallel() noexcept {
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

std::expected<void, std::string> validate_input_attribution(const FrameInput& input) {
    std::size_t previous_end = 0;
    for (const auto& range : input.attribution) {
        if (range.begin >= range.end)
            return std::unexpected(std::format("input attribution [{},{}) must be nonempty", range.begin, range.end));
        if (range.end > input.events.size())
            return std::unexpected(std::format("input attribution [{},{}) exceeds {} events", range.begin, range.end, input.events.size()));
        if (range.begin < previous_end)
            return std::unexpected("input attribution ranges must be ordered and non-overlapping");
        previous_end = range.end;
    }
    return {};
}

Runtime::Runtime(RuntimeOptions options) : impl_(std::make_unique<Impl>(*this)) {
    Impl& d = *impl_;
    d.verbs.set_invocation_context(&d.invocation_context);
    set_agent_access(options.access);
    d.theme = std::move(options.theme);
    d.make_opaque();
    d.clock = options.clock;
    d.complete_frames = options.complete_frames;
    d.size = options.size;
    d.scale = options.scale;
    d.reduce_motion = options.reduce_motion;
    d.reduce_transparency = options.reduce_transparency;
    d.agent_players = options.agent_players;
    d.make_opaque();
    if (options.fonts) {
        d.fonts = options.fonts;
    } else {
        d.own_fonts = std::make_unique<mlibs::text::FontCollection>();
        if (auto r = d.own_fonts->add_defaults(); !r) warn("the default fonts did not load: {}", r.error());
        d.fonts = d.own_fonts.get();
    }
    d.options = std::move(options);
    d.by_index.emplace_back();  // index 0 marks no node
    Element root;
    root.type = &root_type();
    d.root = d.create(root, {});
    d.build_pipeline();
    // The person's controls over every agent (S-14), theirs alone (S-10).
    (void)d.verbs.add({.name = "agents.take_over", .summary = "Take over from every agent: their changes refused until you hand back",
                       .effect = Effect::read, .person = true},
                      [this](const Value&) -> VerbResult {
                          take_over();
                          return Value(true);
                      });
    (void)d.verbs.add({.name = "agents.hand_back", .summary = "Hand back to the agents what you took over", .effect = Effect::read, .person = true},
                      [this](const Value&) -> VerbResult {
                          hand_back();
                          return Value(false);
                      });
}

void Runtime::take_over() {
    Impl& d = *impl_;
    if (d.taken_over) return;
    d.taken_over = true;
    d.held_claims.clear();  // what the agents held is the person's now (D-160)
    d.actions_changed();  // what shows the agents (their presence) shows it
}

void Runtime::hand_back() {
    Impl& d = *impl_;
    if (!d.taken_over) return;
    d.taken_over = false;
    d.actions_changed();  // what shows the agents (their presence) shows it
}

bool Runtime::taken_over() const { return impl_->taken_over; }

void Runtime::note(AgentNote note) {
    Impl& d = *impl_;
    std::erase_if(d.agent_notes, [&](const AgentNote& each) { return each.actor == note.actor; });
    note.at = d.now().count();
    if (!note.text.empty()) d.agent_notes.push_back(std::move(note));
    d.actions_changed();
}

std::span<const AgentNote> Runtime::agent_notes() const noexcept { return impl_->agent_notes; }

std::optional<Envelope> Runtime::Impl::held_by_person(const Invocation& call, Effect effect, std::string_view what) const {
    if (!taken_over || effect == Effect::read || replaying != 0 || !(call.remote || call.actor.kind == "agent")) return std::nullopt;
    Envelope env;
    env.error(spec::error_code::conflict, std::format("the person has taken over: {} waits until they hand back (S-14)", what))
        .result(spec::exit_code::failure);
    return env;
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

bool Runtime::claimed(std::string_view slot) const {
    const auto it = impl_->outlets.find(slot);
    return it != impl_->outlets.end() && it->second.observed();
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
        mlibs::log::debug("the view mounted at '{}' had the id '{}'; the slot names it", slot, view.id);
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

std::string_view Runtime::built_at(NodeId node) const { return valid(node) ? std::string_view(impl_->built_places[impl_->at(node).built_at]) : std::string_view(); }

std::vector<NodeId> Runtime::built_on(std::string_view file, std::uint32_t line) const {
    // The places on that line (file:line:column), then the nodes built at one of them.
    const std::string prefix = std::format("{}:{}:", file, line);
    std::vector<bool> wanted(impl_->built_places.size(), false);
    bool any = false;
    for (std::size_t index = 1; index < impl_->built_places.size(); ++index)
        if (impl_->built_places[index].starts_with(prefix)) wanted[index] = any = true;
    std::vector<NodeId> out;
    if (!any) return out;
    const auto visit = [&](this const auto& self, NodeId node) -> void {
        if (!valid(node)) return;
        if (wanted[impl_->at(node).built_at]) out.push_back(node);
        for (NodeId child : children(node)) self(child);
    };
    visit(root());
    return out;
}

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

const xrender::DisplayList& Runtime::display_list() const noexcept { return impl_->list; }

void Runtime::resize(Size size, float scale) {
    Impl& d = *impl_;
    if (size.width == d.size.width && size.height == d.size.height && scale == d.scale) return;
    constraints_changed(d.root);
    d.size = size;
    d.scale = scale;
    d.layout.mark_dirty(d.at(d.root).layout);
    d.touch(dirty_layout | dirty_paint | dirty_semantics);
}

void Runtime::constraints_changed(NodeId boundary) {
    Impl& d = *impl_;
    if (!d.alive(boundary)) return;
    if (std::ranges::find(d.constraint_boundaries, boundary) == d.constraint_boundaries.end())
        d.constraint_boundaries.push_back(boundary);
    ++d.constraint_epoch;
    d.layout.mark_dirty(d.at(boundary).layout);
    d.touch(dirty_layout | dirty_paint);
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

void Runtime::frame_shown(ShownFrame f) noexcept {
    impl_->shown.push(f);
    if (f.cost_ms >= 0) impl_->govern(f.cost_ms);  // what the frame budget's governor weighs (when it is on)
}

const ShownFrames& Runtime::shown_frames() const noexcept { return impl_->shown; }

void Runtime::set_renderer(std::string name) { impl_->renderer = std::move(name); }

std::string_view Runtime::renderer() const noexcept { return impl_->renderer; }

void Runtime::set_input_actor(std::optional<Actor> actor, std::shared_ptr<const AgentSession> session) {
    Impl& d = *impl_;
    d.input_actor = std::move(actor);
    d.input_call.reset();
    if (d.input_actor) {
        Invocation call;
        call.actor = *d.input_actor;
        call.remote = true;
        call.access = d.access;
        call.session = std::move(session);
        call.operation = "input";
        d.input_call = std::move(call);
    }
}
void Runtime::set_agent_access(std::shared_ptr<AgentAccess> access) {
    impl_->access = std::move(access);
    impl_->verbs.set_agent_access(impl_->access);
}
AgentAccess& Runtime::agent_access() noexcept { return impl_->access ? *impl_->access : omui::agent_access(); }
const AgentAccess& Runtime::agent_access() const noexcept { return impl_->access ? *impl_->access : omui::agent_access(); }
std::shared_ptr<AgentAccess> Runtime::agent_access_binding() const noexcept { return impl_->access; }

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
    xrender::DisplayList before;
    // Keep the visible snapshot's original markers for renderer ownership.
    // paint records it as one historical range; motion never indexes that
    // range as current nodes, even if this snapshot interrupts another fade.
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

void Runtime::want_fonts() noexcept { impl_->fonts_wanted = true; }
bool Runtime::take_font_request() noexcept { return std::exchange(impl_->fonts_wanted, false); }

void Runtime::fonts_changed() {
    Impl& d = *impl_;
    // Active Morph callbacks retain this exact cache address.
    {
        const std::scoped_lock guard(d.motion_glyph_mutex);
        if (d.motion_glyphs) d.motion_glyphs->clear();
    }
    // Their paragraphs are stale (their fonts' revision moved): each measured, laid out and drawn again.
    walk([&](NodeId id, int) { d.layout.mark_dirty(d.at(id).layout); });
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
    ++motion_gen;  // the slots resolve again against the new theme or scope
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

void Runtime::moving(NodeId node) {
    if (!valid(node)) return;
    Impl& d = *impl_;
    if (std::ranges::find(d.moving_said, node) == d.moving_said.end()) d.moving_said.push_back(node);
    d.frame_requested = true;
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
void Runtime::set_agent_players(bool allowed) {
    if (impl_->agent_players == allowed) return;
    impl_->agent_players = allowed;  // read by components when agents ask (Services::agent_players)
}
bool Runtime::agent_players() const { return impl_->agent_players; }
void Runtime::add_subject(mlibs::agent::Subject& subject) {
    if (std::ranges::find(impl_->subjects, &subject) == impl_->subjects.end()) impl_->subjects.push_back(&subject);
}
void Runtime::remove_subject(mlibs::agent::Subject& subject) { std::erase(impl_->subjects, &subject); }
std::span<mlibs::agent::Subject* const> Runtime::subjects() const noexcept { return impl_->subjects; }
std::uint64_t Runtime::add_picture(std::function<std::optional<xrender::raster::ImageView>()> pixels) {
    if (!pixels) return 0;
    const std::uint32_t handle = impl_->next_picture++;
    impl_->pictures.push_back({handle, std::move(pixels)});
    return static_cast<std::uint64_t>(handle) << 32;
}
void Runtime::remove_picture(std::uint64_t number) {
    std::erase_if(impl_->pictures, [&](const Impl::ShownPicture& one) { return one.handle == static_cast<std::uint32_t>(number >> 32); });
}
std::optional<xrender::raster::ImageView> Runtime::picture(std::uint64_t number) const {
    for (const Impl::ShownPicture& one : impl_->pictures)
        if (one.handle == static_cast<std::uint32_t>(number >> 32)) return one.pixels();
    return std::nullopt;
}
bool Runtime::motion_reduced() const { return impl_->reduce_motion || impl_->motion_now() != MotionLevel::full; }
bool Runtime::reduce_motion() const noexcept { return impl_->reduce_motion; }

const Theme& Runtime::theme() const { return impl_->current_theme ? *impl_->current_theme : impl_->theme; }
mlibs::text::FontCollection& Runtime::fonts() { return *impl_->fonts; }
xrender::ImageStore& Runtime::images() { return impl_->images; }
hanim::seconds Runtime::now() const { return impl_->now(); }
Rect Runtime::bounds(NodeId node) const { return valid(node) ? impl_->at(node).bounds : Rect{}; }
Rect Runtime::screen_bounds(NodeId node) const { return valid(node) ? impl_->at(node).screen : Rect{}; }
Rect Runtime::drawn_bounds(NodeId node) const {
    if (!valid(node)) return {};
    const auto& n = impl_->at(node);
    if (impl_->presentation_epoch != 0) return n.presented_epoch == impl_->presentation_epoch ? n.presented_bounds : Rect{};
    return n.world.map({0, 0, n.frame.width, n.frame.height});
}
std::optional<Point> Runtime::pointer() const { return impl_->pointer_inside ? std::optional<Point>(impl_->pointer) : std::nullopt; }
void Runtime::watch_pointer(NodeId node) {
    if (valid(node) && std::ranges::find(impl_->pointer_watchers, node) == impl_->pointer_watchers.end()) impl_->pointer_watchers.push_back(node);
}
// ── text selection ──

void Runtime::Impl::select(NodeId anchor_node, std::size_t anchor, NodeId focus_node, std::size_t focus) {
    selection.anchor_node = anchor_node;
    selection.anchor = anchor;
    selection.focus_node = focus_node;
    selection.focus = focus;
    selected.clear();
    touch(dirty_paint | dirty_semantics);
    if (!alive(anchor_node) || !alive(focus_node) || !alive(root)) return;
    // The selectable text in document order, from the anchor's node to the focus's (either way round).
    std::vector<NodeId> order, stack{root};
    while (!stack.empty()) {
        const NodeId id = stack.back();
        stack.pop_back();
        const Node& n = at(id);
        if (n.type->text_of) order.push_back(id);
        for (auto child = n.children.rbegin(); child != n.children.rend(); ++child)
            if (alive(*child)) stack.push_back(*child);
    }
    const auto first = std::ranges::find(order, anchor_node), last = std::ranges::find(order, focus_node);
    if (first == order.end() || last == order.end()) return;
    const bool forward = first < last || (first == last && anchor <= focus);
    const auto from = forward ? first : last, to = forward ? last : first;
    const std::size_t from_offset = forward ? anchor : focus, to_offset = forward ? focus : anchor;
    for (auto it = from; it != std::next(to); ++it) {
        const Node& n = at(*it);
        const std::size_t length = n.type->text_of(n.instance).size();
        const std::size_t begin = it == from ? std::min(from_offset, length) : 0;
        const std::size_t end = it == to ? std::min(to_offset, length) : length;
        if (end > begin) selected.push_back({*it, {begin, end}});
    }
}

void Runtime::Impl::clear_selection() {
    if (selected.empty() && !alive(selection.anchor_node)) return;
    selection = {};
    selected.clear();
    touch(dirty_paint | dirty_semantics);
}

std::optional<std::size_t> Runtime::Impl::text_offset_at(NodeId node, Point window) const {
    if (!alive(node)) return std::nullopt;
    const Node& n = at(node);
    if (!n.type->text_offset) return std::nullopt;
    return n.type->text_offset(n.instance, local_point(n, window));
}

std::optional<std::pair<std::size_t, std::size_t>> Runtime::text_selection(NodeId node) const {
    for (const auto& [id, range] : impl_->selected)
        if (id == node) return range;
    return std::nullopt;
}
void Runtime::select_text(NodeId node, std::size_t anchor, std::size_t focus) { impl_->select(node, anchor, node, focus); }
std::string Runtime::selected_text() const {
    std::string out;
    for (const auto& [id, range] : impl_->selected) {
        if (!impl_->alive(id)) continue;
        const Node& n = impl_->at(id);
        if (!out.empty()) out += '\n';
        out += n.type->text_of(n.instance).substr(range.first, range.second - range.first);
    }
    return out;
}
void Runtime::clear_text_selection() { impl_->clear_selection(); }

NodeState Runtime::state(NodeId node) const { return valid(node) ? impl_->at(node).state : NodeState{}; }

void Runtime::emit(NodeId node, std::string_view event, const Value& args) { impl_->call_handlers(node, event, args); }

void Runtime::set_route(std::string route) {
    Impl& d = *impl_;
    if (d.route == route) return;
    d.route = std::move(route);
    ++d.route_revision;
    d.frame_requested = true;
}

const std::string& Runtime::route() const noexcept { return impl_->route; }
std::uint64_t Runtime::route_revision() const noexcept { return impl_->route_revision; }

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
            ++m.changes;
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
std::optional<TextLayout> Runtime::text_layout(NodeId node) const {
    const Impl& d = *impl_;
    if (!d.alive(node)) return std::nullopt;
    const auto& n = d.at(node);
    if (!n.type->text_layout) return std::nullopt;
    return n.type->text_layout(n.instance);
}

void Runtime::scroll_to_start(NodeId node) {
    if (impl_->alive(node)) impl_->reveal(node, true);
}
void Runtime::focus(NodeId node) { impl_->set_focus(node, impl_->focus_visible); }
void Runtime::focus_visibly(NodeId node) { impl_->set_focus(node, true); }
void Runtime::capture_pointer(NodeId node) { impl_->capture = node; }
void Runtime::release_pointer() { impl_->capture = {}; }

bool Runtime::perform(NodeId node, std::string_view action, const Value& args) {
    Invocation call;
    call.actor = {"person", "user", {}};  // the input being handled is a person's (openas §2.7)
    call.args = args;
    call.answer = impl_->own_answer(node, action);  // their own press is their answer to what reaches outside (S-2a), as a click is
    return act(node, action, call).exit_code() == std::optional<int>(0);
}

void Runtime::changed_outside_history(NodeId node) {
    Impl& d = *impl_;
    if (d.replaying > 0 || !d.alive(node)) return;  // an undo or a redo putting it back is not a new change
    const NodeId scope = d.scope_of(node);
    for (Impl::ScopeHistory& h : d.histories)
        if (h.scope == scope && !h.redo.empty()) {
            h.redo.clear();
            d.touch(dirty_semantics);
        }
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
    d.record(d.scope_of(node), {node, where, {}, std::string(action), args, std::move(inverse), std::format("{} on '{}'", action, where), d.journal_next()},
             Effect::reversible);
    d.journal_add(where, action, call, 0);
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
    // Retain border-local caret geometry. The host reads it after the frame's
    // presentation is committed, including automatic and external motion.
    d.text_input = *caret;
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

std::uint64_t Runtime::open_web(std::string_view url, std::function<void(const WebPage&)> changed) {
    Impl& d = *impl_;
    if (!d.native_surfaces.web_open) return 0;
    // The host may tell of a change on its own thread: posted, and heard only while the page is open.
    auto heard = std::make_shared<std::uint64_t>(0);
    const std::uint64_t page = d.native_surfaces.web_open(url, [this, heard](const WebPage& now) {
        queue().post([this, heard, now] {
            if (const auto it = impl_->web_pages.find(*heard); it != impl_->web_pages.end() && it->second) it->second(now);
        });
    });
    if (page == 0) return 0;
    *heard = page;
    d.web_pages[page] = std::move(changed);
    return page;
}

void Runtime::place_web(std::uint64_t page, Rect bounds, bool shown) {
    if (impl_->web_pages.contains(page) && impl_->native_surfaces.web_place) impl_->native_surfaces.web_place(page, bounds, shown);
}

bool Runtime::command_web(std::uint64_t page, std::string_view command, std::string_view argument) {
    return impl_->web_pages.contains(page) && impl_->native_surfaces.web_command && impl_->native_surfaces.web_command(page, command, argument);
}

void Runtime::close_web(std::uint64_t page) {
    if (impl_->web_pages.erase(page) && impl_->native_surfaces.web_close) impl_->native_surfaces.web_close(page);
}

bool Runtime::focusable(NodeId node) const { return valid(node) && impl_->is_focusable(node); }

void Runtime::set_url_opener(std::function<bool(std::string_view)> open) { impl_->url_opener = std::move(open); }

void Runtime::set_native_surfaces(NativeSurfaces surfaces) { impl_->native_surfaces = std::move(surfaces); }
const Runtime::NativeSurfaces& Runtime::native_surfaces() const noexcept { return impl_->native_surfaces; }

const std::vector<std::string>& Runtime::opened() const noexcept { return impl_->opened; }

bool Runtime::offer_file(OfferedFile file) {
    if (file.name.empty() || file.name.find_first_of("/\\") != std::string::npos) return false;
    const bool taken = impl_->file_offer && impl_->file_offer(file);
    impl_->offered_files.push_back(std::move(file));
    if (impl_->offered_files.size() > offered_kept) impl_->offered_files.pop_front();
    return taken;
}

void Runtime::set_file_offer(std::function<bool(const OfferedFile&)> offer) { impl_->file_offer = std::move(offer); }

const std::deque<Runtime::OfferedFile>& Runtime::offered_files() const noexcept { return impl_->offered_files; }

std::expected<std::string, std::string> Runtime::capture_svg(std::optional<Rect> region) {
    if (!impl_->svg_capture) return std::unexpected("this host does not draw in vectors");
    return impl_->svg_capture(region.value_or(Rect{0, 0, size().width, size().height}));
}

void Runtime::set_svg_capture(std::function<std::expected<std::string, std::string>(Rect)> capture) { impl_->svg_capture = std::move(capture); }

std::expected<std::vector<std::uint8_t>, std::string> Runtime::capture_png(std::optional<Rect> region) {
    if (!impl_->png_capture) return std::unexpected("this host draws no pixels");
    return impl_->png_capture(region.value_or(Rect{0, 0, size().width, size().height}));
}

void Runtime::set_png_capture(std::function<std::expected<std::vector<std::uint8_t>, std::string>(Rect)> capture) { impl_->png_capture = std::move(capture); }

std::expected<Value, std::string> Runtime::reload_sources(const Value& request) {
    if (!impl_->source_reloader) return std::unexpected("nothing here reloads: the application is not run from sources (a player runs them)");
    return impl_->source_reloader(request);
}

void Runtime::set_source_reloader(std::function<std::expected<Value, std::string>(const Value&)> reloader) {
    impl_->source_reloader = std::move(reloader);
}

// ── what others did here, and the person's own agent ──

void Runtime::Impl::journal_add(std::string where, std::string_view action, const Invocation& call, int exit_code) {
    if (previews > 0) return;  // a preview's steps and its rollback are no one's record
    journal.push_back({std::move(where), std::string(action), call.actor.text(), exit_code, call.args, journal_next(), clock.now().count(), cause});
    actions_changed();
}

void Runtime::Impl::actions_changed() {
    std::erase_if(action_followers, [&](NodeId n) { return !alive(n); });
    for (NodeId n : action_followers) layout.mark_dirty(at(n).layout);
    if (!action_followers.empty()) touch(dirty_layout | dirty_paint | dirty_semantics);
}

NodeId Runtime::undoes(std::uint64_t entry) const {
    const Impl& d = *impl_;
    if (entry == 0) return {};
    for (const Impl::ScopeHistory& h : d.histories) {
        if (h.undo.empty() || h.undo.back().barrier) continue;
        const auto& steps = h.undo.back().steps;
        if (std::ranges::any_of(steps, [&](const Impl::UndoStep& s) { return s.entry == entry; })) return h.scope;
    }
    return {};
}

bool Runtime::undo_entry(std::uint64_t entry, const Actor* actor) {
    const NodeId scope = undoes(entry);
    if (!valid(scope)) return false;
    Invocation call;
    call.actor = actor ? *actor : Actor{"person", "user", {}};
    return act(scope, "undo", call).exit_code() == std::optional<int>(0);
}

std::string Runtime::link_agent() {
    Impl& d = *impl_;
    return d.agent_linker.link ? d.agent_linker.link() : std::string();
}

void Runtime::unlink_agent() {
    if (impl_->agent_linker.unlink) impl_->agent_linker.unlink();
}

const AgentLink& Runtime::agent_link() const noexcept { return impl_->agent_link; }
std::uint64_t Runtime::agent_link_revision() const noexcept { return impl_->agent_link_revision; }

void Runtime::set_agent_linker(AgentLinker linker) {
    Impl& d = *impl_;
    d.agent_linker = std::move(linker);
    agent_link_changed({d.agent_linker.link ? "idle" : "unavailable", {}, {}, {}, 0});
}

void Runtime::agent_link_changed(AgentLink link) {
    Impl& d = *impl_;
    d.agent_link = std::move(link);
    ++d.agent_link_revision;
    d.actions_changed();
    d.frame_requested = true;
}

void Runtime::follow_actions(NodeId node, bool follow) {
    Impl& d = *impl_;
    std::erase(d.action_followers, node);
    if (follow && d.alive(node)) d.action_followers.push_back(node);
}

void Runtime::point_out(NodeId by, std::string_view path) {
    Impl& d = *impl_;
    if (d.pointed_out == path && d.pointed_by == by) return;
    d.pointed_out = std::string(path);
    d.pointed_by = path.empty() ? NodeId{} : by;
    d.touch(dirty_paint);
}

const std::string& Runtime::pointed_out() const noexcept { return impl_->pointed_out; }

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
hanim::Engine& Runtime::engine() { return impl_->anims.engine(); }
std::optional<Rect> Runtime::text_input_rect() const noexcept {
    const auto& d=*impl_;
    if(!d.text_input || !d.alive(d.text_input_node))return std::nullopt;
    const auto& node=d.at(d.text_input_node);
    if(d.presentation_epoch) {
        if(node.presented_epoch!=d.presentation_epoch || node.presented_visible.empty())return std::nullopt;
        return node.presented_world.map(*d.text_input);
    }
    return node.world.map(*d.text_input);
}

}  // namespace omui
