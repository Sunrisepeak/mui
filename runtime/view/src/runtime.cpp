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
}

Runtime::~Runtime() {
    // Component instances and bindings go first, while the runtime they use exists.
    impl_->destroy(impl_->root);
}

NodeId Runtime::mount(std::string_view slot, View view) {
    Impl& d = *impl_;
    // The slot is the view's top-level id: paths begin with it.
    if (!view.id.empty() && view.id != slot)
        debug("the view mounted at '{}' had the id '{}'; the slot names it", slot, view.id);
    view.id = std::string(slot);
    if (view.key.empty()) view.key = view.id;
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
