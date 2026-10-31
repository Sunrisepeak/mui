module openmui.semantic;

import std;
import openmui.core;
import openmui.view;
import openmui.hash;

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
                    if (auto at = index_.find(v.id); at != index_.end()) {
                        // Materialised (a canvas's live node, a list's row):
                        // the node is the item — what the holder says of it
                        // fills what the node does not say itself.
                        spec::SemanticNode& real = nodes_[at->second];
                        if (real.role == Role::generic || real.role == Role::none) real.role = v.role;
                        if (real.name.empty()) real.name = v.name;
                        if (real.description.empty()) real.description = v.description;
                        real.states.bits |= v.states.bits;
                        for (spec::ActionDesc& a : v.actions)
                            if (!real.action(a.name)) real.actions.push_back(std::move(a));
                        continue;
                    }
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
    // A materialised one (a canvas's live node) keeps its holder's actions:
    // those the node does not have itself go to the holder.
    const NodeId real = runtime_.find(id);
    const bool own = runtime_.valid(real) && runtime_.semantic_node(real).action(action) != nullptr;
    if (!own)
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

Envelope Tree::transaction(std::span<const Step> steps, const Invocation& call, std::string label) {
    auto first_error = [](const Envelope& e) -> std::pair<std::string, std::string> {
        for (const spec::Record& r : e.records())
            if (r.kind == spec::RecordKind::error)
                return {std::string(r.body.get("code") ? r.body.get("code")->as_string() : spec::error_code::failed),
                        std::string(r.body.get("text") ? r.body.get("text")->as_string() : std::string_view{})};
        return {std::string(spec::error_code::failed), {}};
    };
    auto label_of = [](const Step& s) { return std::format("{} on '{}'", s.action, s.id); };
    auto call_of = [&](const Step& s) {
        Invocation c = call;
        c.args = s.args;
        if (!s.answer.empty()) c.answer = s.answer;
        return c;
    };
    Envelope e;
    if (steps.empty()) {
        e.error(spec::error_code::invalid_argument, "a transaction needs steps").result(spec::exit_code::failure);
        return e;
    }
    if (call.dry_run) {
        // Each step against the tree as it is now: what it would do, and
        // whether a transaction may hold it.
        Value::List outcomes;
        std::string outcome = "run";
        Effect strongest = Effect::read;
        for (const Step& s : steps) {
            Envelope d = act(s.id, s.action, call_of(s));
            Value v = d.data_value() ? *d.data_value() : Value{};
            v.set("id", s.id).set("action", s.action);
            // A step that cannot even be tried (no such node, no such action) would fail: the whole would.
            if (d.exit_code() != spec::exit_code::success && !v.get("outcome")) {
                const auto [code, message] = first_error(d);
                v.set("outcome", "fail").set("error", Value{}.set("code", code).set("text", message));
            }
            if (v.get("undoable") && !v.get("undoable")->as_bool() && v.get("outcome") && v.get("outcome")->as_string() == "run") {
                v.set("outcome", "refused");
                v.set("error", Value{}.set("code", spec::error_code::refused)
                                   .set("text", std::format("{} gives no way back: a transaction holds only what it can take back (S-13)", label_of(s))));
            }
            const std::string_view effect = v.get("effect") ? v.get("effect")->as_string() : std::string_view{};
            const Effect fx = spec::parse_effect(effect).value_or(Effect::external);
            strongest = std::max(strongest, fx);
            if (v.get("outcome") && v.get("outcome")->as_string() == "run" && spec::requires_answer(fx)) {
                v.set("outcome", "refused");
                v.set("error", Value{}.set("code", spec::error_code::refused)
                                   .set("text", std::format("{} is {}: a transaction holds only actions that can be taken back (S-13)",
                                                               label_of(s), effect)));
            }
            if (outcome == "run" && v.get("outcome") && v.get("outcome")->as_string() != "run") outcome = std::string(v.get("outcome")->as_string());
            outcomes.push_back(std::move(v));
        }
        return dry_run_answer(outcome, strongest, Value{}.set("steps", Value(std::move(outcomes))));
    }
    // Eligibility first, nothing changed yet: a step that would give no way back (its preview says so) refuses the
    // whole, rather than staying done after a later step fails while the answer says it was taken back.
    {
        Invocation preview = call;
        preview.dry_run = true;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            Invocation c = call_of(steps[i]);
            c.dry_run = true;
            Envelope d = act(steps[i].id, steps[i].action, c);
            const Value* v = d.data_value();
            if (v && v->get("undoable") && !v->get("undoable")->as_bool() && v->get("outcome") && v->get("outcome")->as_string() == "run") {
                e.error(spec::error_code::refused,
                        std::format("step {} ({}) gives no way back: a transaction holds only what it can take back (S-13); nothing was done", i + 1,
                                    label_of(steps[i])),
                        Value{}.set("step", static_cast<std::int64_t>(i)).set("id", steps[i].id).set("action", steps[i].action))
                    .result(spec::exit_code::failure);
                return e;
            }
        }
        (void)preview;
    }
    runtime_.begin_transaction(std::move(label));
    Value::List results;
    for (std::size_t i = 0; i < steps.size(); ++i) {
        Envelope r = act(steps[i].id, steps[i].action, call_of(steps[i]));
        if (r.exit_code() != spec::exit_code::success) {
            Envelope back = runtime_.rollback_transaction(call);
            auto [code, message] = first_error(r);
            Value extra;
            extra.set("step", static_cast<std::int64_t>(i)).set("id", steps[i].id).set("action", steps[i].action);
            if (const Value* d = back.data_value()) {
                if (const Value* n = d->get("rolled_back")) extra.set("rolled_back", *n);
                if (const Value* k = d->get("kept")) extra.set("kept", *k);
            }
            // The step's own options (the answer it asks for) stay with it.
            for (const spec::Record& rec : r.records())
                if (rec.kind == spec::RecordKind::error)
                    for (std::string_view k : {"answer", "hint", "args", "actions"})
                        if (const Value* v = rec.body.get(k)) extra.set(k, *v);
            std::string why = std::format("step {} ({}): {}", i + 1, label_of(steps[i]), message);
            const Value* kept = back.data_value() ? back.data_value()->get("kept") : nullptr;
            const bool some_kept = kept && kept->list() && !kept->list()->empty();
            if (back.exit_code() != spec::exit_code::success) why += std::format("; taking back the steps before it failed: {}", first_error(back).second);
            else if (some_kept) {
                // Said as it is: what gave no way back stays done.
                std::string names;
                for (const Value& k : *kept->list()) names += (names.empty() ? "" : ", ") + std::string(k.as_string());
                why += std::format("; of the steps before it, these could not be taken back and stay done: {}", names);
            } else if (i > 0) why += std::format("; the {} step{} before it {} taken back", i, i == 1 ? "" : "s", i == 1 ? "was" : "were");
            e.error(code, why, std::move(extra)).result(r.exit_code().value_or(spec::exit_code::failure));
            return e;
        }
        results.push_back(r.data_value() ? *r.data_value() : Value{});
    }
    Envelope done = runtime_.commit_transaction();
    Value data = done.data_value() ? *done.data_value() : Value{};
    data.set("results", Value(std::move(results)));
    return e.data(std::move(data)).result(spec::exit_code::success);
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
    hash::Fnv1a h;
    for (const spec::SemanticNode& n : nodes_) h.text(spec::to_value(n).json());
    return h.value();
}

}  // namespace omui::semantic
