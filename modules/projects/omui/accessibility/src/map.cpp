module omui.accessibility;

import std;
import omui.core;
import omui.view;
import omui.semantic;
import mlibs.hash;
import mlibs.trace;

namespace omui {  // names this file uses unqualified, from their owner
using mlibs::log::Log;
}

namespace omui::accessibility {

namespace {

constexpr Log log_{"accessibility"};

// A line of scrolling, for a screen reader's scroll up / down without a page.
constexpr double line = 40;

bool toggles(Role r) noexcept {
    switch (r) {
        case Role::checkbox:
        case Role::switch_:
        case Role::toggle_button:
        case Role::radio:
        case Role::menu_item_checkbox:
        case Role::menu_item_radio: return true;
        default: return false;
    }
}

bool ranged(Role r) noexcept {
    switch (r) {
        case Role::slider:
        case Role::spin_button:
        case Role::progress:
        case Role::meter:
        case Role::timeline:
        case Role::scroll_bar:
        case Role::splitter: return true;
        default: return false;
    }
}

// Selection is a state of these, so "not selected" is said too.
bool selectable(Role r) noexcept {
    switch (r) {
        case Role::tab:
        case Role::option:
        case Role::tree_item:
        case Role::row:
        case Role::cell: return true;
        default: return false;
    }
}

std::optional<double> number_at(const Value& v, std::string_view key) {
    const Value* f = v.get(key);
    if (f && f->is_number()) return f->as_double();
    return std::nullopt;
}

std::string text_of(const Value& v) {
    switch (v.type()) {
        case Value::Type::text: return std::string(v.as_string());
        case Value::Type::integer: return std::format("{}", v.as_int());
        case Value::Type::real: return std::format("{}", v.as_double());
        case Value::Type::boolean: return v.as_bool() ? "true" : "false";
        default: return {};
    }
}

}  // namespace

std::uint64_t key_of(std::string_view id) noexcept {
    const std::uint64_t k = mlibs::hash::fnv1a(id);
    return k == 0 ? 1 : k;
}

std::string_view click_action(const spec::SemanticNode& node) noexcept {
    for (std::string_view a : {"activate", "toggle", "select"})
        if (node.action(a)) return a;
    return {};
}

Node describe(const spec::SemanticNode& n) {
    Node out;
    out.key = key_of(n.id);
    const auto ri = static_cast<std::size_t>(n.role);
    out.role = ri < spec::roles.size() ? spec::roles[ri].accesskit : std::string_view{"GenericContainer"};
    // AccessKit reads a label's name from its value.
    if (out.role == "Label") out.value = n.name;
    else out.label = n.name;
    out.description = n.description;

    const Value& v = n.value;
    if (n.role == Role::heading && v.is_number()) {
        out.level = static_cast<std::size_t>(std::max<std::int64_t>(1, v.as_int()));
    } else if (toggles(n.role)) {
        if (v.type() == Value::Type::boolean) out.toggled = v.as_bool() ? Toggled::on : Toggled::off;
        else if (v.as_string() == "mixed") out.toggled = Toggled::mixed;
        else out.toggled = Toggled::off;
        if (n.states.has(State::checked)) out.toggled = Toggled::on;
        if (n.states.has(State::mixed)) out.toggled = Toggled::mixed;
    } else if (n.role == Role::link) {
        out.url = std::string(v.as_string());
    } else if (n.role == Role::scroll_view && v.map()) {
        out.scroll_x = number_at(v, "x");
        out.scroll_y = number_at(v, "y");
        out.scroll_x_max = number_at(v, "max_x");
        out.scroll_y_max = number_at(v, "max_y");
    } else if (v.map()) {
        out.number = number_at(v, "value");
        out.min = number_at(v, "min");
        out.max = number_at(v, "max");
        out.step = number_at(v, "step");
        if (!out.number)
            if (const Value* f = v.get("value")) out.value = text_of(*f);
    } else if (v.is_number() && ranged(n.role)) {
        out.number = v.as_double();
    } else if (out.role != "Label") {
        out.value = text_of(v);
    }
    if (n.role == Role::progress && out.number && !out.max) {
        out.min = 0;
        out.max = 1;
    }

    const StateSet s = n.states;
    out.disabled = s.has(State::disabled);
    out.readonly = s.has(State::readonly);
    out.required = s.has(State::required);
    out.invalid = s.has(State::invalid);
    out.busy = s.has(State::busy);
    out.hidden = s.has(State::hidden);
    out.modal = s.has(State::modal);
    out.multiselectable = s.has(State::multiselectable);
    out.visited = s.has(State::visited);
    if (s.has(State::selected)) out.selected = true;
    else if (selectable(n.role)) out.selected = false;
    if (s.has(State::expanded)) out.expanded = true;
    else if (s.has(State::collapsible) || n.action("expand")) out.expanded = false;
    if (n.role == Role::status || n.role == Role::timer) out.live = Live::polite;
    else if (n.role == Role::alert_dialog) out.live = Live::assertive;

    // Actions: AccessKit's name where it has one, else a custom action.
    const std::string_view click = click_action(n);
    if (!click.empty()) out.add(Action::click);
    if (s.has(State::focusable) || n.action("focus")) out.add(Action::focus);
    for (const spec::ActionDesc& a : n.actions) {
        const std::string_view name = a.name;
        if (name == click || name == "focus") continue;
        if (name == "blur") out.add(Action::blur);
        else if (name == "expand") out.add(Action::expand);
        else if (name == "collapse") out.add(Action::collapse);
        else if (name == "increment") out.add(Action::increment);
        else if (name == "decrement") out.add(Action::decrement);
        else if (name == "set_value" || name == "seek") out.add(Action::set_value);
        else if (name == "replace_selection") out.add(Action::replace_selected_text);
        else if (name == "scroll_into_view") out.add(Action::scroll_into_view);
        else if (name == "scroll_to") out.add(Action::set_scroll_offset);
        else if (name == "scroll_by") {
            out.add(Action::scroll_up);
            out.add(Action::scroll_down);
            out.add(Action::scroll_left);
            out.add(Action::scroll_right);
        } else if (name == "open_menu") out.add(Action::show_context_menu);
        else if (a.args.empty()) out.custom.push_back(a.name);  // dismiss, deselect, a second of the clicks
    }
    if (!out.custom.empty()) out.add(Action::custom);

    out.bounds = n.bounds;
    out.children.reserve(n.children.size());
    for (const std::string& c : n.children) out.children.push_back(key_of(c));
    return out;
}

// ── the mirror ──

Mirror::Mirror(Runtime& runtime, MirrorOptions options) : tree_(runtime), options_(std::move(options)) {}

Node Mirror::root_node(const spec::SemanticNode* app) const {
    Node root = app ? describe(*app) : Node{};
    root.role = "Window";
    if (!options_.name.empty()) root.label = options_.name;
    if (app) root.key = key_of(app->id);
    return root;
}

void Mirror::rename(std::string name) {
    std::lock_guard hold(mutex_);
    options_.name = std::move(name);
    if (auto it = nodes_.find(root_); it != nodes_.end() && !options_.name.empty()) it->second.label = options_.name;
}

namespace {

bool text_field(Role r) noexcept { return r == Role::text_box || r == Role::search_box || r == Role::combo_box; }

bool same_run(const Node& a, const Node& b) {
    auto same_rect = [](const std::optional<spec::Rect>& x, const std::optional<spec::Rect>& y) {
        return x.has_value() == y.has_value() && (!x || (x->x == y->x && x->y == y->y && x->width == y->width && x->height == y->height));
    };
    return a.key == b.key && a.value == b.value && a.lengths == b.lengths && a.positions == b.positions && a.widths == b.widths && a.word_starts == b.word_starts &&
           a.rtl == b.rtl && a.next_on_line == b.next_on_line && a.previous_on_line == b.previous_on_line && same_rect(a.bounds, b.bounds);
}

bool same_place(const std::optional<TextSelection>& a, const std::optional<TextSelection>& b) {
    return a.has_value() == b.has_value() &&
           (!a || (a->anchor.run == b->anchor.run && a->anchor.index == b->anchor.index && a->focus.run == b->focus.run &&
                   a->focus.index == b->focus.index));
}

}  // namespace

void Mirror::text_of(const std::string& id, Node& node, std::vector<Node>& out, bool force) {
    Runtime& rt = tree_.runtime();
    const NodeId n = rt.find(id);
    const std::optional<TextLayout> layout = rt.valid(n) ? rt.text_layout(n) : std::nullopt;
    Text now;
    if (layout) {
        std::size_t first = 0;  // the run's first grapheme
        for (std::size_t i = 0; i < layout->lines.size(); ++i) {
            const TextLine& l = layout->lines[i];
            Node run;
            run.key = key_of(std::format("{}\x1frun:{}", id, i));
            run.role = "TextRun";
            run.value = layout->text.substr(l.begin, l.end - l.begin);
            run.lengths = l.lengths;
            run.positions = l.positions;
            run.widths = l.widths;
            run.word_starts = l.word_starts;
            run.rtl = l.rtl;
            run.bounds = spec::Rect{l.bounds.x, l.bounds.y, l.bounds.width, l.bounds.height};
            run_of_[run.key] = {node.key, first};
            first += run.lengths.size();
            if (i > 0 && layout->lines[i - 1].continues) {
                run.previous_on_line = now.runs.back().key;
                now.runs.back().next_on_line = run.key;
            }
            now.runs.push_back(std::move(run));
        }
        // Graphemes to a run and a character in it: the end of the text is the end of the last run.
        auto place = [&](std::size_t g) {
            std::size_t start = 0;
            for (const Node& r : now.runs) {
                if (g < start + r.lengths.size()) return TextPosition{r.key, g - start};
                start += r.lengths.size();
            }
            return now.runs.empty() ? TextPosition{} : TextPosition{now.runs.back().key, now.runs.back().lengths.size()};
        };
        if (!now.runs.empty()) now.selection = TextSelection{place(layout->anchor), place(layout->focus)};
    }
    Text& was = texts_[node.key];
    for (const Node& r : now.runs) {
        const bool known = std::ranges::any_of(was.runs, [&](const Node& w) { return same_run(w, r); });
        if (force || !known) {
            out.push_back(r);
            nodes_[r.key] = r;
        }
    }
    for (const Node& w : was.runs)
        if (std::ranges::none_of(now.runs, [&](const Node& r) { return r.key == w.key; })) {
            nodes_.erase(w.key);
            run_of_.erase(w.key);
        }
    for (const Node& r : now.runs) node.children.push_back(r.key);
    node.selection = now.selection;
    if (node.selection) node.add(Action::set_text_selection);
    was = std::move(now);
}

Mirror::Changes Mirror::refresh() {
    mlibs::trace::Span span("accessibility.refresh", "accessibility");
    spec::SemanticDelta delta = tree_.changes();
    Changes out;
    std::lock_guard hold(mutex_);
    // A text field's caret moves, and its line scrolls, with no change to the tree: the focused one is read again.
    const bool focused_text = texts_.contains(focus_);
    if (delta.empty() && !focused_text) return out;
    if (root_ == 0)
        if (const semantic::Snapshot top = tree_.snapshot({.depth = 0}); !top.nodes.empty()) {
            app_id_ = top.nodes.front().id;
            root_ = key_of(app_id_);
        }
    std::uint64_t focused = 0;
    bool          lost = false;
    std::vector<Node> runs;
    auto keep = [&](const spec::SemanticNode& n) {
        Node node = n.id == app_id_ ? root_node(&n) : describe(n);
        if (n.states.has(State::focused)) focused = node.key;
        else if (node.key == focus_) lost = true;
        if (text_field(n.role)) text_of(n.id, node, runs, true);
        ids_[node.key] = n.id;
        out.nodes.push_back(node);
        nodes_[node.key] = std::move(node);
    };
    for (const spec::SemanticNode& n : delta.added) keep(n);
    for (const spec::SemanticNode& n : delta.updated) keep(n);
    for (const std::string& id : delta.removed) {
        const std::uint64_t k = key_of(id);
        if (k == focus_) lost = true;
        if (auto t = texts_.find(k); t != texts_.end()) {
            for (const Node& r : t->second.runs) {
                nodes_.erase(r.key);
                run_of_.erase(r.key);
            }
            texts_.erase(t);
        }
        nodes_.erase(k);
        ids_.erase(k);
    }
    // The focused field, unchanged in the tree: its caret or its runs may have moved.
    if (focused_text && !lost && !focused)
        if (auto n = nodes_.find(focus_); n != nodes_.end() && std::ranges::none_of(out.nodes, [&](const Node& o) { return o.key == focus_; })) {
            Node node = n->second;
            const auto before = node.selection;
            node.children.erase(std::remove_if(node.children.begin(), node.children.end(), [&](std::uint64_t c) { return run_of_.contains(c); }),
                                node.children.end());
            const std::size_t sent = runs.size();
            text_of(ids_[focus_], node, runs, false);
            if (runs.size() != sent || !same_place(before, node.selection) || node.children != n->second.children) {
                out.nodes.push_back(node);
                n->second = std::move(node);
            }
        }
    for (Node& r : runs) out.nodes.push_back(std::move(r));
    // The focus: the focused node, else the window.
    const std::uint64_t focus = focused ? focused : (lost || focus_ == 0) ? root_ : focus_;
    out.focus = focus;
    out.focus_moved = focus != focus_;
    focus_ = focus;
    if (!out.nodes.empty())
        log_.debug("{} nodes to send ({} added, {} updated, {} removed)", out.nodes.size(), delta.added.size(),
                   delta.updated.size(), delta.removed.size());
    return out;
}

void Mirror::each(const std::function<void(const Node&)>& fn) const {
    std::lock_guard hold(mutex_);
    for (const auto& [k, n] : nodes_) fn(n);
}

std::uint64_t Mirror::focus() const {
    std::lock_guard hold(mutex_);
    return focus_ ? focus_ : root_;
}

std::size_t Mirror::size() const {
    std::lock_guard hold(mutex_);
    return nodes_.size();
}

std::optional<std::string> Mirror::id_of(std::uint64_t key) const {
    std::lock_guard hold(mutex_);
    if (auto it = ids_.find(key); it != ids_.end()) return it->second;
    return std::nullopt;
}

void Mirror::queue(Request request) {
    std::function<void()> wake;
    {
        std::lock_guard hold(mutex_);
        requests_.push_back(std::move(request));
        wake = wake_;
    }
    if (wake) wake();
}

void Mirror::set_wake(std::function<void()> wake) {
    std::lock_guard hold(mutex_);
    wake_ = std::move(wake);
}

std::size_t Mirror::waiting() const {
    std::lock_guard hold(mutex_);
    return requests_.size();
}

std::vector<Envelope> Mirror::serve() {
    std::vector<Request> taken;
    {
        std::lock_guard hold(mutex_);
        taken.swap(requests_);
    }
    std::vector<Envelope> out;
    out.reserve(taken.size());
    for (const Request& r : taken) out.push_back(perform(r));
    return out;
}

Envelope Mirror::perform(const Request& r) {
    mlibs::trace::Span span("accessibility.perform", "accessibility");
    auto failed = [](std::string_view code, std::string text) {
        Envelope e;
        e.error(code, text).result(spec::exit_code::failure);
        return e;
    };
    const std::optional<std::string> id = id_of(r.target);
    if (!id) return failed(spec::error_code::not_found, std::format("no node with key {:#x}", r.target));
    const std::optional<spec::SemanticNode> node = tree_.node(*id);
    if (!node) return failed(spec::error_code::not_found, std::format("no node '{}'", *id));

    std::string action;
    Value       args;
    switch (r.action) {
        case Action::click: action = click_action(*node); break;
        case Action::focus: action = "focus"; break;
        case Action::blur: action = "blur"; break;
        case Action::expand: action = "expand"; break;
        case Action::collapse: action = "collapse"; break;
        case Action::increment: action = "increment"; break;
        case Action::decrement: action = "decrement"; break;
        case Action::scroll_into_view: action = "scroll_into_view"; break;
        case Action::show_context_menu: action = "open_menu"; break;
        case Action::set_text_selection:
            if (r.selection) {
                // Runs and characters to the field's graphemes.
                auto grapheme = [&](const TextPosition& p) -> std::optional<std::size_t> {
                    std::lock_guard hold(mutex_);
                    auto it = run_of_.find(p.run);
                    if (it == run_of_.end() || it->second.first != r.target) return std::nullopt;
                    return it->second.second + p.index;
                };
                const auto anchor = grapheme(r.selection->anchor), focus = grapheme(r.selection->focus);
                if (anchor && focus) {
                    action = "set_selection";
                    args = Value{}.set("anchor", static_cast<std::int64_t>(*anchor)).set("focus", static_cast<std::int64_t>(*focus));
                }
            }
            break;
        case Action::custom: {
            const Node d = describe(*node);
            if (r.custom >= 0 && static_cast<std::size_t>(r.custom) < d.custom.size()) action = d.custom[static_cast<std::size_t>(r.custom)];
            break;
        }
        case Action::set_value:
            if (node->action("set_value")) {
                action = "set_value";
                args = Value{}.set("value", r.number ? Value(*r.number) : Value(r.text.value_or(std::string{})));
            } else if (node->action("seek") && r.number) {
                action = "seek";
                args = Value{}.set("time", *r.number);
            }
            break;
        case Action::replace_selected_text:
            action = "replace_selection";
            args = Value{}.set("text", r.text.value_or(std::string{}));
            break;
        case Action::set_scroll_offset:
        case Action::scroll_to_point:
            if (r.point) {
                action = "scroll_to";
                args = Value{}.set("x", static_cast<double>(r.point->x)).set("y", static_cast<double>(r.point->y));
            }
            break;
        case Action::scroll_up:
        case Action::scroll_down:
        case Action::scroll_left:
        case Action::scroll_right: {
            action = "scroll_by";
            const bool back = r.action == Action::scroll_up || r.action == Action::scroll_left;
            const Value by = r.page ? Value(back ? "-page" : "page") : Value(back ? -line : line);
            const bool vertical = r.action == Action::scroll_up || r.action == Action::scroll_down;
            args = Value{}.set("dx", vertical ? Value(0.0) : by).set("dy", vertical ? by : Value(0.0));
            break;
        }
        default: break;
    }
    if (action.empty() || !node->action(action))
        return failed(spec::error_code::invalid_argument,
                      std::format("'{}' has no action for what assistive technology asked ({})", *id, static_cast<int>(r.action)));
    // The person's own input: what a click would answer, this answers.
    Invocation call;
    call.args = std::move(args);
    call.answer = node->action(action)->answer;
    log_.info("{} {} (assistive technology)", *id, action);
    return tree_.act(*id, action, call);
}

}  // namespace omui::accessibility
