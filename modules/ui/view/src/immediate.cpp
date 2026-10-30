module openmui.view;

import std;
import openmui.core;
import openmui.layout;

namespace omui {

// What an immediate root shares with its builder and the handlers of its
// nodes: the build function, what happened since the last build, and the
// tick whose change runs the build again.
struct ImState {
    Signal<std::uint64_t>                       tick{0};
    std::function<void(Ui&)>                    build;
    Services*                                   services = nullptr;
    NodeId                                      node;
    std::unordered_set<std::string>             activated;
    std::unordered_map<std::string, Value>      changed;
    std::unordered_map<std::string, std::vector<std::pair<std::string, Value>>> events;
    std::unordered_map<std::string, std::set<std::string, std::less<>>>          wanted;
    std::uint64_t                               timer = 0;
    int                                         consumed = 0;  // what the current build took in

    void poke() { tick.set(tick.peek() + 1); }
};

namespace {

struct ImRootNode {
    static constexpr std::string_view name = "immediate";
    std::shared_ptr<ImState> state;

    ImRootNode() = default;
    ImRootNode(const ImRootNode&) = delete;
    ImRootNode& operator=(const ImRootNode&) = delete;
    ~ImRootNode() {
        if (state && state->timer && state->services) state->services->cancel(state->timer);
    }

    void style(const Theme&, layout::Style& s) const { s.direction = layout::Direction::column; }
    void mount(Services& s, NodeId n) {
        if (!state) return;
        state->services = &s;
        state->node = n;
    }
};

std::uint32_t fnv1a(std::string_view s) noexcept {
    std::uint32_t h = 2166136261u;
    for (unsigned char c : s) {
        h ^= c;
        h *= 16777619u;
    }
    return h;
}

}  // namespace

Ui::Ui(ImState& state) : state_(state) { open_.push_back(&top_); }

std::string Ui::next_id(std::string_view type, std::string_view explicit_id, const std::source_location& at) {
    if (!explicit_id.empty()) return std::string(explicit_id);
    std::string where;
    for (const std::string& part : ids_) {
        where += part;
        where += '/';
    }
    // The file by its own name and the line: where the source was checked
    // out (an absolute path, as the compiler was given it) is no part of an
    // id, nor is the column (GCC and Clang count it from different places in
    // a call), so the ids — and the tree's hash — are the same on every
    // machine and compiler. Two calls of a type on one line are told apart
    // below, in order, as a loop's are.
    std::string_view file = at.file_name();
    if (const auto slash = file.find_last_of("/\\"); slash != std::string_view::npos) file.remove_prefix(slash + 1);
    where += std::format("{}:{}", file, at.line());
    std::string id = std::format("{}:i{:08x}", type, fnv1a(where));
    const int n = seen_[id]++;
    if (n > 0) id += std::format(".{}", n);  // the same call site in a loop
    return id;
}

Element& Ui::put(Element e) {
    if (e.key.empty()) e.key = e.id;
    std::vector<Element>& list = *open_.back();
    list.push_back(std::move(e));
    return list.back();
}

void Ui::container(Element e, FunctionRef<void()> body) {
    Element& placed = put(std::move(e));
    // The open list is the element's children: nothing is added to its
    // parent's list until the body has run, so the reference stays valid.
    open_.push_back(&placed.children);
    body();
    open_.pop_back();
}

// Events a build asked about (`ui.event`) reach it from then on: the node
// gets a handler that records them and builds again.
void Ui::listen(Element& e, std::span<const std::string_view>) {
    auto it = state_.wanted.find(e.id);
    if (it == state_.wanted.end()) return;
    for (const std::string& name : it->second) {
        e.on(name, [st = &state_, id = e.id, name](const Value& args) {
            st->events[id].emplace_back(name, args);
            st->poke();
        });
    }
}

namespace {

// A builder's element under the id the immediate build gave it.
template <class Builder, class Options>
Element tagged(Builder&& make, Options o, std::string id, const std::source_location& at) {
    const bool generated = o.id.empty();
    o.id = {};
    Element e = make(std::move(o));
    e.id = std::move(id);
    e.generated_id = generated;
    e.where = at;
    return e;
}

}  // namespace

void Ui::column(BoxOptions o, FunctionRef<void()> body, std::source_location at) {
    std::string id = next_id("column", o.id, at);
    Element e = tagged([&](BoxOptions x) { return omui::column(std::move(x), {}, at); }, std::move(o), std::move(id), at);
    listen(e, {});
    container(std::move(e), body);
}

void Ui::row(BoxOptions o, FunctionRef<void()> body, std::source_location at) {
    std::string id = next_id("row", o.id, at);
    Element e = tagged([&](BoxOptions x) { return omui::row(std::move(x), {}, at); }, std::move(o), std::move(id), at);
    listen(e, {});
    container(std::move(e), body);
}

void Ui::stack(BoxOptions o, FunctionRef<void()> body, std::source_location at) {
    std::string id = next_id("stack", o.id, at);
    Element e = tagged([&](BoxOptions x) { return omui::stack(std::move(x), {}, at); }, std::move(o), std::move(id), at);
    listen(e, {});
    container(std::move(e), body);
}

void Ui::box(BoxOptions o, FunctionRef<void()> body, std::source_location at) {
    std::string id = next_id("box", o.id, at);
    Element e = tagged([&](BoxOptions x) { return omui::box(std::move(x), {}, at); }, std::move(o), std::move(id), at);
    listen(e, {});
    container(std::move(e), body);
}

void Ui::grid(BoxOptions o, std::string columns, FunctionRef<void()> body, std::source_location at) {
    std::string id = next_id("grid", o.id, at);
    Element e = tagged([&](BoxOptions x) { return omui::grid(std::move(x), std::move(columns), {}, at); }, std::move(o), std::move(id), at);
    listen(e, {});
    container(std::move(e), body);
}

void Ui::scroll(ScrollOptions o, FunctionRef<void()> body, std::source_location at) {
    std::string id = next_id("scroll_view", o.id, at);
    Element e = tagged([&](ScrollOptions x) { return omui::scroll(std::move(x), {}, at); }, std::move(o), std::move(id), at);
    listen(e, {});
    container(std::move(e), body);
}

void Ui::text(std::string_view text, TextOptions o, std::source_location at) {
    std::string id = next_id("text", o.id, at);
    o.text = std::string(text);
    Element e = tagged([&](TextOptions x) { return omui::text(std::move(x), at); }, std::move(o), std::move(id), at);
    listen(e, {});
    put(std::move(e));
}

void Ui::image(ImageOptions o, std::source_location at) {
    std::string id = next_id("image", o.id, at);
    Element e = tagged([&](ImageOptions x) { return omui::image(std::move(x), at); }, std::move(o), std::move(id), at);
    listen(e, {});
    put(std::move(e));
}

void Ui::spacer(float grow, std::source_location at) {
    Element e = omui::spacer(grow, at);
    e.id = next_id("spacer", {}, at);
    e.generated_id = true;
    put(std::move(e));
}

void Ui::add(Element e, std::source_location at) {
    const std::string_view type = e.type ? e.type->name : std::string_view("box");
    if (e.id.empty()) {
        e.id = next_id(type, {}, at);
        e.generated_id = true;
    }
    listen(e, {});
    put(std::move(e));
}

namespace {

// A control's element: the base component when it is linked, else a plain
// stand-in built from primitives so immediate code runs anywhere.
Element control(std::string_view type_name, std::string id, bool generated, const std::source_location& at) {
    Element e;
    e.type = find_type(type_name);
    e.id = std::move(id);
    e.generated_id = generated;
    e.where = at;
    return e;
}

Element stand_in(std::string id, bool generated, const std::source_location& at, std::string label, bool disabled) {
    Element e = omui::row({.padding = Edges(6.0f, 12.0f), .background = "surface_variant", .radius = 6}, {
                              omui::text({.text = std::move(label), .style = "label"}),
                          },
                          at);
    e.id = std::move(id);
    e.generated_id = generated;
    if (disabled) e.prop("disabled", Source{Value(true), {}, {}});
    return e;
}

Source two_way(ImState& st, const std::string& id, Value current) {
    ImState* s = &st;
    return Source{std::move(current), {}, [s, id](const Value& v) {
                      s->changed.insert_or_assign(id, v);
                      s->poke();
                  }};
}

}  // namespace

bool Ui::button(std::string_view label, im::ButtonOptions o, std::source_location at) {
    const bool generated = o.id.empty();
    std::string id = next_id("button", o.id, at);
    Element e = control("button", id, generated, at);
    if (e.type) {
        e.prop("text", Source{Value(label), {}, {}});
        if (!o.variant.empty()) e.prop("variant", Source{Value(o.variant), {}, {}});
        if (!o.icon.empty()) e.prop("icon", Source{Value(o.icon), {}, {}});
        if (o.disabled) e.prop("disabled", Source{Value(true), {}, {}});
    } else {
        e = stand_in(id, generated, at, std::string(label), o.disabled);
    }
    if (!o.verb.empty()) e.prop("verb", Source{Value(o.verb), {}, {}});
    ImState* s = &state_;
    e.on("activate", [s, id](const Value&) {
        s->activated.insert(id);
        s->poke();
    });
    listen(e, {});
    put(std::move(e));
    const bool hit = state_.activated.erase(id) > 0;
    state_.consumed += hit;
    return hit;
}

namespace {

bool toggle_like(ImState& st, std::string_view type, std::string id, bool generated, std::string_view label,
                 bool& value, const im::ToggleOptions& o, const std::source_location& at, Element& out) {
    Element e = control(type, id, generated, at);
    if (e.type) {
        e.prop("label", Source{Value(label), {}, {}});
        e.prop("checked", two_way(st, id, Value(value)));
        if (o.indeterminate) e.prop("indeterminate", Source{Value(true), {}, {}});
        if (o.disabled) e.prop("disabled", Source{Value(true), {}, {}});
    } else {
        e = stand_in(id, generated, at, std::format("{} {}", value ? "[x]" : "[ ]", label), o.disabled);
        ImState* s = &st;
        const bool next = !value;
        e.on("activate", [s, id, next](const Value&) {
            s->changed.insert_or_assign(id, Value(next));
            s->poke();
        });
    }
    out = std::move(e);
    auto it = st.changed.find(id);
    if (it == st.changed.end()) return false;
    const bool v = it->second.as_bool(value);
    st.changed.erase(it);
    if (v == value) return false;
    value = v;
    ++st.consumed;
    return true;
}

}  // namespace

bool Ui::checkbox(std::string_view label, bool& value, im::ToggleOptions o, std::source_location at) {
    const bool generated = o.id.empty();
    std::string id = next_id("checkbox", o.id, at);
    Element e;
    bool changed = toggle_like(state_, "checkbox", id, generated, label, value, o, at, e);
    if (changed) e.prop("checked", two_way(state_, id, Value(value)));
    listen(e, {});
    put(std::move(e));
    return changed;
}

bool Ui::toggle(std::string_view label, bool& value, im::ToggleOptions o, std::source_location at) {
    const bool generated = o.id.empty();
    std::string id = next_id("toggle", o.id, at);
    Element e;
    bool changed = toggle_like(state_, "toggle", id, generated, label, value, o, at, e);
    if (changed) e.prop("checked", two_way(state_, id, Value(value)));
    listen(e, {});
    put(std::move(e));
    return changed;
}

bool Ui::slider(std::string_view label, double& value, im::SliderOptions o, std::source_location at) {
    const bool generated = o.id.empty();
    std::string id = next_id("slider", o.id, at);
    bool changed = false;
    if (auto it = state_.changed.find(id); it != state_.changed.end()) {
        const double v = std::clamp(it->second.as_double(value), o.min, o.max);
        state_.changed.erase(it);
        changed = v != value;
        value = v;
        state_.consumed += changed;
    }
    Element e = control("slider", id, generated, at);
    if (e.type) {
        e.prop("label", Source{Value(label), {}, {}});
        e.prop("value", two_way(state_, id, Value(value)));
        e.prop("min", Source{Value(o.min), {}, {}}).prop("max", Source{Value(o.max), {}, {}}).prop("step", Source{Value(o.step), {}, {}});
        if (o.disabled) e.prop("disabled", Source{Value(true), {}, {}});
    } else {
        e = stand_in(id, generated, at, std::format("{}: {}", label, value), o.disabled);
    }
    listen(e, {});
    put(std::move(e));
    return changed;
}

bool Ui::text_box(std::string_view label, std::string& value, im::TextBoxOptions o, std::source_location at) {
    const bool generated = o.id.empty();
    std::string id = next_id("text_box", o.id, at);
    bool changed = false;
    if (auto it = state_.changed.find(id); it != state_.changed.end()) {
        std::string v(it->second.as_string());
        state_.changed.erase(it);
        changed = v != value;
        value = std::move(v);
        state_.consumed += changed;
    }
    Element e = control("text_box", id, generated, at);
    if (e.type) {
        e.prop("label", Source{Value(label), {}, {}});
        e.prop("text", two_way(state_, id, Value(value)));
        if (!o.placeholder.empty()) e.prop("placeholder", Source{Value(o.placeholder), {}, {}});
        if (o.multiline) e.prop("multiline", Source{Value(true), {}, {}});
        if (o.password) e.prop("password", Source{Value(true), {}, {}});
        if (o.max_length) e.prop("max_length", Source{Value(o.max_length), {}, {}});
        if (o.disabled) e.prop("disabled", Source{Value(true), {}, {}});
    } else {
        e = stand_in(id, generated, at, std::format("{}: {}", label, value), o.disabled);
    }
    listen(e, {});
    put(std::move(e));
    return changed;
}

void Ui::push_id(std::string_view part) { ids_.emplace_back(part); }
void Ui::push_id(std::int64_t part) { ids_.push_back(std::format("{}", part)); }
void Ui::pop_id() {
    if (!ids_.empty()) ids_.pop_back();
}
Ui::Scope Ui::scope(std::string_view part) {
    push_id(part);
    return Scope(this);
}
Ui::Scope Ui::scope(std::int64_t part) {
    push_id(part);
    return Scope(this);
}

seconds Ui::now() const { return state_.services ? state_.services->now() : seconds(0); }

void Ui::rebuild_after(seconds delay) {
    if (!state_.services) return;
    if (state_.timer) state_.services->cancel(state_.timer);
    ImState* s = &state_;
    state_.timer = state_.services->after(delay, [s] {
        s->timer = 0;
        s->poke();
    });
}

void Ui::rebuild_next_frame() { rebuild_after(seconds(0)); }

std::optional<Value> Ui::event(std::string_view id, std::string_view name) const {
    auto& want = state_.wanted[std::string(id)];
    if (!want.contains(name)) want.emplace(name);
    auto it = state_.events.find(std::string(id));
    if (it == state_.events.end()) return std::nullopt;
    for (const auto& [n, args] : it->second)
        if (n == name) {
            ++state_.consumed;
            return args;
        }
    return std::nullopt;
}

struct ImBuild {
    static std::vector<Element> run(ImState& st) {
        (void)st.tick.get();  // what makes it build again
        // A build that took in an event changed state that what it declared
        // before may show ("clicked 1 times" above the button): it runs once
        // more, so the declarations settle without waiting for another event.
        std::vector<Element> out;
        for (int pass = 0; pass < 3; ++pass) {
            st.consumed = 0;
            Ui ui(st);
            if (st.build) st.build(ui);
            out = std::move(ui.top_);
            // What happened was for this build; what no declaration asked for is dropped.
            st.activated.clear();
            st.changed.clear();
            st.events.clear();
            if (st.consumed == 0) break;
        }
        return out;
    }
};

Element immediate(std::function<void(Ui&)> build, BoxOptions options, std::source_location where) {
    auto st = std::make_shared<ImState>();
    st->build = std::move(build);
    Element e = omui::column(std::move(options), {}, where);
    e.type = &component_type<ImRootNode>();
    e.configure = [st](void* p) {
        auto* root = static_cast<ImRootNode*>(p);
        if (root->state && root->state != st) {
            // A rebuilt parent hands over a new builder: keep what happened, take the new function.
            st->services = root->state->services;
            st->node = root->state->node;
        }
        root->state = st;
    };
    e.children.push_back(dynamic([st] { return ImBuild::run(*st); }, where));
    return e;
}

}  // namespace omui
