module openmui.host.web;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;

namespace omui {

namespace {

// ARIA prohibits naming these: their name is their text.
bool named_by_text(std::string_view aria) { return aria == "none" || aria == "generic" || aria == "paragraph" || aria == "code"; }

bool ranged(Role r) {
    return r == Role::slider || r == Role::spin_button || r == Role::progress || r == Role::meter || r == Role::scroll_bar || r == Role::timeline;
}

bool checkable(Role r) {
    return r == Role::checkbox || r == Role::radio || r == Role::switch_ || r == Role::menu_item_checkbox || r == Role::menu_item_radio;
}

bool selectable(Role r) { return r == Role::tab || r == Role::option || r == Role::tree_item; }

std::string value_text(const Value& v) {
    switch (v.type()) {
        case Value::Type::text: return std::string(v.as_string());
        case Value::Type::integer: return std::format("{}", v.as_int());
        case Value::Type::real: return std::format("{}", v.as_double());
        default: return {};
    }
}

// The text an element holds: the name where ARIA lets it have none, and the
// value where it is not a range's (a text box's text).
std::string text_of(const spec::SemanticNode& n) {
    std::string out;
    if (n.id != "app" && named_by_text(spec::roles[static_cast<std::size_t>(n.role)].aria)) out = n.name;
    if (!ranged(n.role)) {
        const std::string v = value_text(n.value);
        if (!v.empty() && v != out) out = out.empty() ? v : std::format("{} {}", out, v);
    }
    return out;
}

// What the live region says of a status: its name and its value.
std::string spoken(const spec::SemanticNode& n) {
    const std::string v = value_text(n.value);
    if (n.name.empty()) return v;
    return v.empty() ? n.name : std::format("{}: {}", n.name, v);
}

std::int64_t round(float v) { return static_cast<std::int64_t>(std::lround(v)); }

}  // namespace

std::vector<std::pair<std::string, std::string>> aria_attributes(const spec::SemanticNode& n) {
    std::vector<std::pair<std::string, std::string>> a;
    auto put = [&](std::string_view k, std::string_view v) { a.emplace_back(std::string(k), std::string(v)); };
    const std::string_view aria = spec::roles[static_cast<std::size_t>(n.role)].aria;
    // The root is no `application`: that role turns a screen reader's
    // reading off for all inside it, and the nodes inside say what they are.
    if (n.id != "app" && aria != "generic") put("role", aria);
    if (n.id != "app" && !n.name.empty() && !named_by_text(aria)) put("aria-label", n.name);
    if (!n.description.empty()) put("aria-description", n.description);
    const spec::StateSet s = n.states;
    if (s.has(State::disabled)) put("aria-disabled", "true");
    if (checkable(n.role)) put("aria-checked", s.has(State::mixed) ? "mixed" : s.has(State::checked) ? "true" : "false");
    if (n.role == Role::toggle_button) put("aria-pressed", s.has(State::mixed) ? "mixed" : s.has(State::checked) ? "true" : "false");
    if (selectable(n.role)) put("aria-selected", s.has(State::selected) ? "true" : "false");
    else if (s.has(State::selected)) put("aria-selected", "true");
    if (s.has(State::collapsible) || s.has(State::expanded)) put("aria-expanded", s.has(State::expanded) ? "true" : "false");
    if (s.has(State::busy)) put("aria-busy", "true");
    if (s.has(State::invalid)) put("aria-invalid", "true");
    if (s.has(State::required)) put("aria-required", "true");
    if (s.has(State::readonly)) put("aria-readonly", "true");
    if (s.has(State::multiline)) put("aria-multiline", "true");
    if (s.has(State::multiselectable)) put("aria-multiselectable", "true");
    if (s.has(State::modal)) put("aria-modal", "true");
    if (ranged(n.role)) {
        if (n.value.is_number()) put("aria-valuenow", value_text(n.value));
        else if (const std::string v = value_text(n.value); !v.empty()) put("aria-valuetext", v);
    }
    // The page's live region speaks for a status (once, not twice).
    if (n.role == Role::status) put("aria-live", "off");
    if (s.has(State::focusable)) put("tabindex", "-1");
    std::ranges::sort(a);
    return a;
}

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

    auto take = [&](const spec::SemanticNode& n, bool added) {
        Element& e = elements_[n.id];
        std::vector<std::pair<std::string, std::string>> attrs = aria_attributes(n);
        if (added || attrs != e.attrs) {
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
        if (std::string text = text_of(n); added ? !text.empty() : text != e.text) {
            entry(n.id).set("text", text);
            e.text = std::move(text);
        }
        if (added ? !n.children.empty() : n.children != e.children) {
            Value::List kids;
            for (const std::string& c : n.children) kids.emplace_back(c);
            entry(n.id).set("children", Value(std::move(kids)));
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
    if (!live.empty()) batch.set("live", live);
    if (!alert.empty()) batch.set("alert", alert);
    if (!batch.map() || batch.map()->empty()) return std::nullopt;
    batch.set("revision", static_cast<std::int64_t>(delta.revision));
    return batch;
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
    if (e.sent != rel) {
        e.sent = rel;
        auto [slot, made] = at.try_emplace(id, out.size());
        if (made) out.push_back(Value{}.set("id", id));
        out[slot->second].set("box", Value(Value::List{Value(rel[0]), Value(rel[1]), Value(rel[2]), Value(rel[3])}));
    }
    if (e.box != before)
        for (const std::string& c : e.children) place(c, out, at);
}

}  // namespace omui
