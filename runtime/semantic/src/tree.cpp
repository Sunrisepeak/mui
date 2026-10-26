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
        std::optional<std::size_t> here = parent;
        if (!(options_.compact && meaningless(n))) {
            n.children.clear();
            const std::size_t i = nodes_.size();
            if (parent) nodes_[*parent].children.push_back(n.id);
            index_.emplace(n.id, i);
            nodes_.push_back(std::move(n));
            here = i;
        }
        for (NodeId c : runtime_.children(id)) self(self, c, here);
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
    std::size_t start = 0;
    if (!o.root.empty()) {
        auto it = index_.find(o.root);
        if (it == index_.end()) return s;
        start = it->second;
    }
    // The subtree in pre-order, with depths from the subtree's root.
    std::vector<std::pair<std::size_t, int>> order;
    auto walk = [&](auto& self, std::size_t i, int depth) -> void {
        order.emplace_back(i, depth);
        if (o.depth >= 0 && depth >= o.depth) return;
        for (const std::string& c : nodes_[i].children)
            if (auto it = index_.find(c); it != index_.end()) self(self, it->second, depth + 1);
    };
    if (!nodes_.empty()) walk(walk, start, 0);
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
    return std::nullopt;
}

Envelope Tree::act(std::string_view id, std::string_view action, const Invocation& call) {
    NodeId n = runtime_.find(id);
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
