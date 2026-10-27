module openmui.semantic;

import std;
import openmui.core;
import openmui.view;

namespace omui::semantic {

namespace {

// Nodes that carry nothing for a reader: their children take their place.
bool meaningless(const spec::SemanticNode& n) {
    if (n.id == "app") return false;
    if (n.role == Role::none) return true;
    return n.role == Role::generic && n.name.empty() && n.description.empty() && n.value.is_null() && n.actions.empty() &&
           !n.states.has(State::focusable);
}

bool interactive(const spec::SemanticNode& n) {
    if (n.states.has(State::focusable)) return true;
    return std::ranges::any_of(n.actions, [](const spec::ActionDesc& a) { return a.effect != Effect::read; });
}

std::uint64_t fnv(std::string_view s, std::uint64_t h = 14695981039346656037ull) noexcept {
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace

Value Snapshot::to_value() const {
    Value::List list;
    list.reserve(nodes.size());
    for (const spec::SemanticNode& n : nodes) list.push_back(spec::to_value(n));
    Value v;
    v.set("revision", static_cast<std::int64_t>(revision));
    v.set("total", static_cast<std::int64_t>(total));
    v.set("nodes", Value(std::move(list)));
    return v;
}

Tree::Tree(Runtime& runtime, TreeOptions options) : runtime_(runtime), options_(options) {}

void Tree::refresh() {
    if (fresh_ && read_ == runtime_.semantic_revision()) return;
    nodes_.clear();
    index_.clear();
    // Pre-order; a lifted node's children are added to its nearest kept ancestor.
    auto visit = [&](auto& self, NodeId id, std::optional<std::size_t> parent) -> void {
        spec::SemanticNode n = runtime_.semantic_node(id);
        // Hidden content (a closed menu) is not read, as aria-hidden content is not.
        if (options_.compact && n.states.has(State::hidden) && n.id != "app") return;
        std::optional<std::size_t> here = parent;
        const bool kept = !(options_.compact && meaningless(n));
        if (kept) {
            n.children.clear();
            const std::size_t i = nodes_.size();
            if (parent) nodes_[*parent].children.push_back(n.id);
            index_.emplace(n.id, i);
            nodes_.push_back(std::move(n));
            here = i;
        }
        for (NodeId c : runtime_.children(id)) self(self, c, here);
        // A virtualising node (a list) states all its items (S-7): those not
        // materialised join as offscreen nodes, and all keep the model's order.
        if (kept && here) {
            auto items = runtime_.virtual_items(id);
            if (!items.empty()) {
                const std::size_t holder = *here;
                std::vector<std::string> order;
                for (spec::SemanticNode& v : items) {
                    order.push_back(v.id);
                    if (index_.contains(v.id)) continue;
                    v.children.clear();
                    index_.emplace(v.id, nodes_.size());
                    nodes_.push_back(std::move(v));
                }
                std::erase_if(order, [&](const std::string& o) { return !index_.contains(o); });
                // Materialised items keep any other child (headers) after them.
                for (const std::string& c : nodes_[holder].children)
                    if (std::ranges::find(order, c) == order.end()) order.push_back(c);
                nodes_[holder].children = std::move(order);
            }
        }
    };
    visit(visit, runtime_.root(), std::nullopt);
    read_ = runtime_.semantic_revision();
    fresh_ = true;
}

std::uint64_t Tree::revision() {
    refresh();
    return read_;
}

Snapshot Tree::snapshot(const SnapshotOptions& o) {
    refresh();
    Snapshot s;
    s.revision = read_;
    // The subtree in pre-order, with depths from the subtree's root.
    std::vector<std::pair<std::size_t, int>> order;
    auto walk = [&](auto& self, std::size_t i, int depth) -> void {
        order.emplace_back(i, depth);
        if (o.depth >= 0 && depth >= o.depth) return;
        for (const std::string& c : nodes_[i].children)
            if (auto it = index_.find(c); it != index_.end()) self(self, it->second, depth + 1);
    };
    if (o.root.empty()) {
        if (!nodes_.empty()) walk(walk, 0, 0);
    } else if (auto it = index_.find(o.root); it != index_.end()) {
        walk(walk, it->second, 0);
    } else {
        // A root the compact tree lifted: its kept descendants nearest to it
        // are the subtree's tops (pre-order keeps them in document order).
        const std::string prefix = o.root + "/";
        std::vector<std::size_t> tops;
        for (std::size_t i = 0; i < nodes_.size(); ++i) {
            if (!nodes_[i].id.starts_with(prefix)) continue;
            const bool nested = std::ranges::any_of(tops, [&](std::size_t t) { return nodes_[i].id.starts_with(nodes_[t].id + "/"); });
            if (!nested) tops.push_back(i);
        }
        for (std::size_t t : tops) walk(walk, t, 1);
    }
    std::vector<const spec::SemanticNode*> picked;
    for (auto [i, depth] : order)
        if (!o.interactive || interactive(nodes_[i])) picked.push_back(&nodes_[i]);
    s.total = picked.size();
    const std::size_t end = o.limit ? std::min(picked.size(), o.offset + o.limit) : picked.size();
    // Children past the depth stay named in `children`, so a reader can ask for them next.
    for (std::size_t i = std::min(o.offset, picked.size()); i < end; ++i) s.nodes.push_back(*picked[i]);
    return s;
}

spec::SemanticDelta Tree::changes() {
    refresh();
    spec::SemanticDelta d;
    d.revision = read_;
    std::unordered_set<std::string_view> present;
    for (const spec::SemanticNode& n : nodes_) {
        present.insert(n.id);
        auto it = seen_.find(n.id);
        if (it == seen_.end()) d.added.push_back(n);
        else if (!(it->second == n)) d.updated.push_back(n);
    }
    for (const auto& [id, n] : seen_)
        if (!present.contains(id)) d.removed.push_back(id);
    std::ranges::sort(d.removed);
    seen_.clear();
    for (const spec::SemanticNode& n : nodes_) seen_.emplace(n.id, n);
    return d;
}

std::vector<spec::SemanticNode> Tree::find(const Query& q) {
    refresh();
    std::vector<spec::SemanticNode> out;
    for (const spec::SemanticNode& n : nodes_) {
        if (!q.matches(n)) continue;
        out.push_back(n);
        if (q.limit && out.size() >= q.limit) break;
    }
    return out;
}

std::optional<spec::SemanticNode> Tree::node(std::string_view id) {
    refresh();
    auto it = index_.find(std::string(id));
    if (it != index_.end()) return nodes_[it->second];
    // A lifted node (compact trees) is still addressable.
    NodeId n = runtime_.find(id);
    if (runtime_.valid(n)) return runtime_.semantic_node(n);
    // An item its holder states only when asked (a canvas's offscreen nodes).
    if (const auto cut = id.rfind('/'); cut != std::string_view::npos)
        if (NodeId holder = runtime_.find(id.substr(0, cut)); runtime_.valid(holder))
            return runtime_.virtual_item(holder, id.substr(cut + 1));
    return std::nullopt;
}

Envelope Tree::act(std::string_view id, std::string_view action, const Invocation& call) {
    // An item a list holds unmaterialised is materialised first (S-7).
    // An item its holder acts on without a node (a canvas's), for the actions
    // the holder states on it; anything else materialises it first.
    if (!runtime_.valid(runtime_.find(id)))
        if (const auto cut = id.rfind('/'); cut != std::string_view::npos)
            if (NodeId holder = runtime_.find(id.substr(0, cut)); runtime_.valid(holder) && runtime_.acts_on_items(holder))
                if (const auto item = runtime_.virtual_item(holder, id.substr(cut + 1)); item && item->action(action))
                    return runtime_.act_item(holder, id.substr(cut + 1), action, call);
    NodeId n = runtime_.reveal(id);
    if (!runtime_.valid(n)) {
        Envelope e;
        e.error(spec::error_code::not_found, std::format("no node '{}'", id),
                Value{}.set("hint", "find nodes with {\"op\":\"find\"}; ids are paths such as main/save"))
            .result(spec::exit_code::failure);
        return e;
    }
    return runtime_.act(n, action, call);
}

WaitState Tree::check(const Condition& c) {
    refresh();
    if (c.query) {
        const bool any = std::ranges::any_of(nodes_, [&](const spec::SemanticNode& n) { return c.query->matches(n); });
        if (any == c.absent)
            return {false, c.absent ? "a node still matches the query" : "no node matches the query yet"};
    }
    if (c.settled) {
        for (const spec::SemanticNode& n : nodes_)
            if (n.states.has(State::animating) || n.states.has(State::removing))
                return {false, std::format("'{}' is still {}", n.id, n.states.has(State::removing) ? "being removed" : "animating")};
    }
    return {true, {}};
}

std::vector<spec::Violation> Tree::violations() {
    refresh();
    std::vector<spec::Violation> out;
    for (const spec::SemanticNode& n : nodes_)
        for (spec::Violation& v : spec::check(n)) out.push_back(std::move(v));
    return out;
}

std::uint64_t Tree::digest() {
    refresh();
    std::uint64_t h = 14695981039346656037ull;
    for (const spec::SemanticNode& n : nodes_) h = fnv(spec::to_value(n).json(), h);
    return h;
}

}  // namespace omui::semantic
