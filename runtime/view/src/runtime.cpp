// The public face of the runtime: mounting, tree queries, properties, the
// services components use.
module openmui.view;

import std;
import openmui.core;
import openmui.layout;
import openmui.render;
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
    d.clock = options.clock;
    d.size = options.size;
    d.scale = options.scale;
    d.reduce_motion = options.reduce_motion;
    d.reduce_transparency = options.reduce_transparency;
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
    d.flatten(d.root);
    return id;
}

void Runtime::unmount(std::string_view slot) {
    Impl& d = *impl_;
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
anim::Clock& Runtime::clock() noexcept { return impl_->clock; }
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
    d.fade_start = d.now();
    d.fade_length = length;
    d.frame_requested = true;
    d.touch(dirty_paint);
}
double Runtime::display_hz() const noexcept { return impl_->display_hz; }

bool Runtime::deterministic() const { return impl_->clock.kind() != anim::ClockKind::interactive; }
NodeId Runtime::focused() const noexcept { return impl_->focus; }
Verbs& Runtime::verbs() noexcept { return impl_->verbs; }
UiQueue& Runtime::queue() noexcept { return impl_->queue; }

void Runtime::set_theme(Theme theme) {
    Impl& d = *impl_;
    d.theme = std::move(theme);
    std::vector<NodeId> all;
    walk([&](NodeId id, int) { all.push_back(id); });
    for (NodeId id : all) {
        Node& n = d.at(id);
        if (n.type->restyle) n.type->restyle(n.instance);
        if (n.type->measure) d.layout.mark_dirty(n.layout);
        d.sync_style(id);
        d.resolve_visual(id);
    }
    d.touch(dirty_layout | dirty_paint | dirty_semantics);
}

void Runtime::set_reduce_motion(bool reduce) {
    impl_->reduce_motion = reduce;
    impl_->touch(dirty_paint);
}

void Runtime::set_reduce_transparency(bool reduce) {
    impl_->reduce_transparency = reduce;
    impl_->touch(dirty_paint);
}

bool Runtime::reduce_transparency() const { return impl_->reduce_transparency; }

const Theme& Runtime::theme() const { return impl_->theme; }
typeset::FontCollection& Runtime::fonts() { return *impl_->fonts; }
render::ImageStore& Runtime::images() { return impl_->images; }
anim::seconds Runtime::now() const { return impl_->now(); }
Rect Runtime::bounds(NodeId node) const { return valid(node) ? impl_->at(node).bounds : Rect{}; }
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
    rs.pending = untracked([&] { return d.at(node).type->compose(d.at(node).instance); });
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
anim::Set& Runtime::animations() { return impl_->anims; }
std::optional<Rect> Runtime::text_input_rect() const noexcept { return impl_->text_input; }

}  // namespace omui
