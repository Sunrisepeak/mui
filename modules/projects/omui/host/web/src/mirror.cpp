module omui.host.web;

import std;
import omui.core;
import omui.view;
import omui.semantic;

namespace omui {

namespace {

std::string value_text(const Value& v) {
    switch (v.type()) {
        case Value::Type::text: return std::string(v.as_string());
        case Value::Type::integer: return std::format("{}", v.as_int());
        case Value::Type::real: return std::format("{}", v.as_double());
        default: return {};
    }
}

// What the live region says of a status: its name and its value.
std::string spoken(const spec::SemanticNode& n) {
    const std::string v = value_text(n.value);
    if (n.name.empty()) return v;
    return v.empty() ? n.name : std::format("{}: {}", n.name, v);
}

std::int64_t round(float v) { return static_cast<std::int64_t>(std::lround(v)); }

}  // namespace

DomMirror::DomMirror(Runtime& runtime) : tree_(runtime) {}

std::optional<Value> DomMirror::sync() {
    // Nothing read while nothing changed: most frames cost a comparison.
    const std::uint64_t revision = tree_.runtime().semantic_revision();
    if (!first_ && revision == synced_) return std::nullopt;
    synced_ = revision;
    spec::SemanticDelta delta = tree_.changes();
    if (delta.empty()) return std::nullopt;
    const bool first = std::exchange(first_, false);

    Value batch;
    if (first && !delta.added.empty()) batch.set("root", delta.added.front().id);

    Value::List removed;
    for (const std::string& id : delta.removed)
        if (elements_.erase(id)) removed.emplace_back(id);
    if (!elements_.contains(main_)) main_.clear();
    if (!removed.empty()) batch.set("remove", Value(std::move(removed)));

    std::vector<Value> out;  // one per element that changed, in the tree's order
    std::unordered_map<std::string, std::size_t> at;
    auto entry = [&](const std::string& id) -> Value& {
        auto [it, made] = at.try_emplace(id, out.size());
        if (made) out.push_back(Value{}.set("id", id));
        return out[it->second];
    };
    std::vector<std::string> regrouped, moved;
    std::string live, alert;
    std::string focus = elements_.contains(focused_) ? focused_ : std::string{};
    std::string focus_name = focused_name_;

    // The nodes of this batch by id, and each one's parent as the batch says it: an element is the one the static
    // projection writes for the node, which depends on what it stands in and, for its text's links, on its children.
    std::unordered_map<std::string_view, const spec::SemanticNode*> changed;
    std::unordered_map<std::string_view, std::string_view> parent_of;
    for (const auto* nodes : {&delta.added, &delta.updated})
        for (const spec::SemanticNode& n : *nodes) {
            changed.emplace(n.id, &n);
            for (const std::string& child : n.children) parent_of.emplace(child, n.id);
        }
    std::deque<spec::SemanticNode> looked_up;  // nodes outside the batch, read from the tree
    const semantic::NodeLookup find = [&](std::string_view id) -> const spec::SemanticNode* {
        if (const auto here = changed.find(id); here != changed.end()) return here->second;
        if (auto node = tree_.node(id)) {
            looked_up.push_back(std::move(*node));
            return &looked_up.back();
        }
        return nullptr;
    };
    const auto tag_around = [&](const std::string& id) -> std::string_view {
        std::string_view parent;
        if (const auto said = parent_of.find(id); said != parent_of.end()) parent = said->second;
        else if (const auto known = elements_.find(id); known != elements_.end()) parent = known->second.parent;
        const auto around = parent.empty() ? elements_.end() : elements_.find(std::string(parent));
        return around == elements_.end() ? std::string_view{} : std::string_view(around->second.tag);
    };
    // A piece of an element's own text for the page: a run a string, an element {"tag","c"}, a child node standing
    // in the text {"tag","node","c"} (its attributes are its own entry's).
    const auto piece = [](this auto const& self, const semantic::Inline& from) -> Value {
        if (from.tag.empty()) return Value(from.text);
        Value out = Value{}.set("tag", from.tag);
        if (!from.node.empty()) out.set("node", from.node);
        else if (!from.attributes.empty()) {
            Value set = Value(Value::Map{});
            for (const auto& [key, value] : from.attributes) set.set(key, value);
            out.set("set", std::move(set));
        }
        if (!from.children.empty()) {
            Value::List inner;
            for (const semantic::Inline& child : from.children) inner.push_back(self(child));
            out.set("c", Value(std::move(inner)));
        }
        return out;
    };
    std::vector<std::string> shown_again;  // children an element's text placed and places no more

    auto take = [&](const spec::SemanticNode& n, bool added) {
        const std::string_view inside = tag_around(n.id);
        Element& e = elements_[n.id];
        // What the element is and holds, as the static page has it (omui.semantic:html, D-236, D-286).
        semantic::HtmlElement made = semantic::html_element(n, inside, find, semantic::HtmlFor::live);
        // A tag that changed is another element: all of it is sent again.
        const bool fresh = added || made.tag != e.tag;
        if (fresh) {
            if (!e.inlined) entry(n.id).set("tag", made.tag);  // what stands in a text is the element its text makes it
            e.tag = made.tag;
            e.attrs.clear();
            e.text.clear();
        }
        std::vector<std::pair<std::string, std::string>> attrs = std::move(made.attributes);
        if (fresh || attrs != e.attrs) {
            Value set = Value(Value::Map{});
            for (const auto& [k, v] : attrs)
                if (std::ranges::find(e.attrs, std::pair{k, v}) == e.attrs.end()) set.set(k, v);
            Value::List unset;
            for (const auto& [k, v] : e.attrs)
                if (std::ranges::find(attrs, k, &std::pair<std::string, std::string>::first) == attrs.end()) unset.emplace_back(k);
            Value& o = entry(n.id);
            if (!set.map()->empty()) o.set("set", std::move(set));
            if (!unset.empty()) o.set("unset", Value(std::move(unset)));
            e.attrs = std::move(attrs);
        }
        if (n.role == Role::main) main_ = n.id;
        else if (main_ == n.id) main_.clear();
        // Its own text: one run as `text`, the elements its marks make as `content`. What stands in its parent's
        // text has its words there.
        if (!e.inlined) {
            Value::List pieces;
            for (const semantic::Inline& from : made.content) pieces.push_back(piece(from));
            const bool plain = pieces.empty() || (pieces.size() == 1 && pieces.front().type() == Value::Type::text);
            std::string text = pieces.empty() ? std::string{} : Value(pieces).json();
            if (fresh ? !text.empty() : text != e.text) {
                if (plain) entry(n.id).set("text", pieces.empty() ? std::string{} : std::string(pieces.front().as_string()));
                else entry(n.id).set("content", Value(std::move(pieces)));
                e.text = std::move(text);
            }
        }
        // The children its text places stand there; the others are the element's children.
        if (made.placed != e.placed) {
            for (const std::string& id : e.placed)
                if (!std::ranges::contains(made.placed, id))
                    if (auto child = elements_.find(id); child != elements_.end()) {
                        child->second.inlined = false;
                        child->second.sent.reset();
                        entry(id).set("inline", false);
                        shown_again.push_back(id);
                    }
            for (const std::string& id : made.placed)
                if (!std::ranges::contains(e.placed, id)) {
                    Element& child = elements_[id];
                    child.inlined = true;
                    child.sent.reset();
                    entry(id).set("inline", true);
                }
            e.placed = std::move(made.placed);
        }
        std::vector<std::string> block;
        for (const std::string& child : n.children)
            if (!std::ranges::contains(e.placed, child)) block.push_back(child);
        if (fresh ? !block.empty() : block != e.block) {
            Value::List kids;
            for (const std::string& c : block) kids.emplace_back(c);
            entry(n.id).set("children", Value(std::move(kids)));
            e.block = std::move(block);
        }
        if (added ? !n.children.empty() : n.children != e.children) {
            e.children = n.children;
            regrouped.push_back(n.id);
        }
        std::optional<Box> bounds;
        if (n.bounds) bounds = Box{round(n.bounds->x), round(n.bounds->y), round(n.bounds->width), round(n.bounds->height)};
        if (added || bounds != e.bounds) {
            e.bounds = bounds;
            moved.push_back(n.id);
        }
        if (n.states.has(State::focused)) focus = n.id, focus_name = n.name;
        else if (focus == n.id) focus.clear();
        if (focus == n.id) focus_name = n.name;
        std::string says = spoken(n);
        if (!first && n.role == Role::status && !says.empty() && (added || says != e.spoken)) live = says;
        if (!first && added && n.role == Role::alert_dialog && !n.name.empty()) alert = n.name;
        e.spoken = std::move(says);
    };
    for (const spec::SemanticNode& n : delta.added) take(n, true);
    for (const spec::SemanticNode& n : delta.updated) take(n, false);
    // What a text placed and places no more is an element of its own again: its tag, its text, its box.
    for (const std::string& id : shown_again) {
        const auto known = elements_.find(id);
        if (known == elements_.end() || known->second.inlined) continue;
        known->second.tag.clear();
        if (const spec::SemanticNode* node = find(id)) take(*node, false);
        moved.push_back(id);
    }

    // Children know their parents again; a child that changed parent moves.
    for (const std::string& id : regrouped) {
        const auto it = elements_.find(id);
        if (it == elements_.end()) continue;
        for (const std::string& c : it->second.children)
            if (auto child = elements_.find(c); child != elements_.end() && child->second.parent != id) {
                child->second.parent = id;
                moved.push_back(c);
            }
    }
    // Boxes, parents first: a box is relative to the parent's.
    auto depth = [](const std::string& id) { return std::ranges::count(id, '/'); };
    std::ranges::sort(moved, [&](const std::string& a, const std::string& b) {
        const auto da = depth(a), db = depth(b);
        return da != db ? da < db : a < b;
    });
    moved.erase(std::unique(moved.begin(), moved.end()), moved.end());
    for (const std::string& id : moved) place(id, out, at);

    if (!out.empty()) {
        Value::List nodes;
        for (Value& v : out) {
            if (v.map() && v.map()->size() > 1) nodes.push_back(std::move(v));
        }
        if (!nodes.empty()) batch.set("nodes", Value(std::move(nodes)));
    }
    if (focus != focused_) batch.set("focus", focus.empty() ? Value{} : Value(focus));
    focused_ = std::move(focus);
    focused_name_ = focused_.empty() ? std::string{} : std::move(focus_name);
    // The skip link: past what comes before the main landmark, to the first
    // thing in it a person can focus.
    {
        const std::string to = main_.empty() ? std::string{} : first_focusable(main_);
        std::string name;
        if (!to.empty())
            for (const auto& [k, v] : elements_.at(main_).attrs)
                if (k == "aria-label") name = v;
        if (std::string key = to.empty() ? std::string{} : to + '\x1f' + name; key != skip_) {
            batch.set("skip", to.empty() ? Value{} : Value{}.set("to", to).set("name", name));
            skip_ = std::move(key);
        }
    }
    if (!live.empty()) batch.set("live", live);
    if (!alert.empty()) batch.set("alert", alert);
    if (!batch.map() || batch.map()->empty()) return std::nullopt;
    batch.set("revision", static_cast<std::int64_t>(delta.revision));
    return batch;
}

// The first node in the subtree a person can focus (tree order), enabled.
std::string DomMirror::first_focusable(const std::string& id) const {
    const auto it = elements_.find(id);
    if (it == elements_.end()) return {};
    const auto& attrs = it->second.attrs;
    const auto has = [&](std::string_view k) { return std::ranges::find(attrs, k, &std::pair<std::string, std::string>::first) != attrs.end(); };
    if (has("tabindex") && !has("aria-disabled")) return id;
    for (const std::string& c : it->second.children)
        if (std::string found = first_focusable(c); !found.empty()) return found;
    return {};
}

// A node's box over the canvas, relative to its parent's (each element is
// placed in its parent's); where it moved, its children's move with it.
void DomMirror::place(const std::string& id, std::vector<Value>& out, std::unordered_map<std::string, std::size_t>& at) {
    const auto it = elements_.find(id);
    if (it == elements_.end()) return;
    Element& e = it->second;
    const auto parent = elements_.find(e.parent);
    const Box base = parent != elements_.end() ? parent->second.box : Box{};
    const Box before = e.box;
    e.box = e.bounds.value_or(base);
    const Box rel{e.box[0] - base[0], e.box[1] - base[1], e.box[2], e.box[3]};
    // What stands in its parent's text is placed by the text: no box of its own.
    if (!e.inlined && e.sent != rel) {
        e.sent = rel;
        auto [slot, made] = at.try_emplace(id, out.size());
        if (made) out.push_back(Value{}.set("id", id));
        out[slot->second].set("box", Value(Value::List{Value(rel[0]), Value(rel[1]), Value(rel[2]), Value(rel[3])}));
    }
    if (e.box != before)
        for (const std::string& c : e.children) place(c, out, at);
}

}  // namespace omui
