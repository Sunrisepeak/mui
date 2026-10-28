// Declarative behaviours: compiling *.behavior.omui.toml into a statechart
// (every state, event, operation and name checked, with its line), and
// running it as a canvas behaviour.
module openmui.ui.canvas.kit;

import std;
import openmui.core;
import openmui.view;
import openmui.render.vector;
import openmui.expr;
import openmui.ui.canvas;
import :parts;
import :chart;

namespace omui::world {

std::string BehaviorProblem::text() const {
    return std::format("{}:{}:{}: {}{}", file, line, column, message, hint.empty() ? "" : std::format(" ({})", hint));
}

}  // namespace omui::world

namespace omui::kit {

namespace {

// ── what a behaviour can do and read ────────────────────────────────────

struct OpSpec {
    world::BehaviorOperation info;
    std::size_t              least, most;  // arguments
};

constexpr std::array<OpSpec, 17> op_specs{{
    {{"capture", "capture()", "In pointer_down: take the pointer until it is released (a press not captured goes on to the next behaviour, or pans)."}, 0, 0},
    {{"move", "move(target, delta)", "Move a node, or each of a list of keys, by a {x, y} of world units."}, 2, 2},
    {{"move_to", "move_to(target, x, y)", "Move a node's top-left corner to a world point."}, 3, 3},
    {{"resize", "resize(target, width, height)", "Set a node's size, world units."}, 3, 3},
    {{"raise", "raise(target)", "Put a node, or each of a list, above every other."}, 1, 1},
    {{"select", "select(target)", "Choose a node, or a list of them, alone."}, 1, 1},
    {{"add_to_selection", "add_to_selection(target)", "Add a node, or a list, to the selection."}, 1, 1},
    {{"toggle_selection", "toggle_selection(target)", "Take a chosen node out of the selection, or add one that is not."}, 1, 1},
    {{"clear_selection", "clear_selection()", "Choose nothing."}, 0, 0},
    {{"connect", "connect(from, to)", "Connect two nodes with an edge."}, 2, 2},
    {{"disconnect", "disconnect(from, to)", "Remove the edges between two nodes."}, 2, 2},
    {{"remove", "remove(target)", "Remove a node, or each of a list, with its edges."}, 1, 1},
    {{"commit", "commit(event, value?)", "Tell the canvas's listeners: the event, with the value (what scripts save on)."}, 1, 2},
    {{"set", "set(name, value)", "Remember a value: vars.<name>, until the canvas makes its behaviours again."}, 2, 2},
    {{"fit", "fit(target)", "Move the camera to show a node, a list of them, or `'all'` the scene, whole (on a spring)."}, 1, 1},
    {{"preset", "preset(name)", "Move the camera to one of the scene's named views (on a spring)."}, 1, 1},
    {{"zoom_to", "zoom_to(zoom)", "Zoom about the view's centre to screen pixels per world unit: 1 is 1:1 (on a spring)."}, 1, 1},
}};

constexpr auto operation_list = [] {
    std::array<world::BehaviorOperation, op_specs.size()> out{};
    for (std::size_t i = 0; i < op_specs.size(); ++i) out[i] = op_specs[i].info;
    return out;
}();

// The names each place's expressions see.
constexpr std::array<std::string_view, 11> pointer_names{"pointer", "press", "delta", "travel", "hit", "over", "mods", "selection", "zoom", "vars", "state"};
constexpr std::array<std::string_view, 8>  tap_names{"pointer", "hit", "over", "mods", "selection", "zoom", "vars", "state"};
// A key during a gesture also sees the gesture (Esc puts a drag back); outside one, they are null.
constexpr std::array<std::string_view, 10> key_names{"key", "mods", "hit", "press", "delta", "travel", "selection", "zoom", "vars", "state"};
constexpr std::array<std::string_view, 12> entry_names{"pointer", "press", "delta", "travel", "hit", "over", "mods", "key", "selection", "zoom", "vars", "state"};
constexpr std::array<std::string_view, 5>  action_when_names{"node", "selection", "zoom", "vars", "state"};
constexpr std::array<std::string_view, 6>  action_names{"node", "args", "selection", "zoom", "vars", "state"};

constexpr std::array<std::string_view, 6> behavior_keys{"on", "initial", "priority", "doc", "states", "actions"};
constexpr std::array<std::string_view, 3> state_keys{"entry", "exit", "on"};
constexpr std::array<std::string_view, 3> transition_keys{"when", "to", "do"};
constexpr std::array<std::string_view, 6> action_keys{"effect", "answer", "args", "when", "do", "doc"};
constexpr std::array<std::string_view, 12> arg_types{"f64", "f32", "i64", "i32", "bool", "string", "list<string>", "any", "point", "rect", "size", "color"};
constexpr std::array<std::string_view, 4> effect_names{"read", "reversible", "irreversible", "external"};

std::span<const std::string_view> names_for(Event e) {
    switch (e) {
        case Event::tap: return tap_names;
        case Event::key: return key_names;
        default: return pointer_names;
    }
}

// Edits from a to b, swaps of neighbours one edit (optimal string alignment).
std::size_t distance(std::string_view a, std::string_view b) {
    const std::size_t w = b.size() + 1;
    std::vector<std::size_t> d((a.size() + 1) * w);
    for (std::size_t i = 0; i <= a.size(); ++i) d[i * w] = i;
    for (std::size_t j = 0; j <= b.size(); ++j) d[j] = j;
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j) {
            std::size_t v = std::min({d[(i - 1) * w + j] + 1, d[i * w + j - 1] + 1, d[(i - 1) * w + j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1)});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) v = std::min(v, d[(i - 2) * w + j - 2] + 1);
            d[i * w + j] = v;
        }
    return d[a.size() * w + b.size()];
}

template <class Range>
std::string did_you_mean(std::string_view name, const Range& options, std::string_view what) {
    std::string_view best;
    std::size_t d = std::min(std::max<std::size_t>(1, name.size() / 3) + 1, name.size());
    for (const auto& o : options)
        if (const std::size_t x = distance(name, o); x < d) {
            d = x;
            best = o;
        }
    if (!best.empty()) return std::format("did you mean `{}`?", best);
    std::string all;
    for (const auto& o : options) all += std::format("{}`{}`", all.empty() ? "" : ", ", std::string_view(o));
    return all.empty() ? std::string{} : std::format("the {} are {}", what, all);
}

// A behaviour's name: what `behaviors` lists (`name:args`), so no colon.
bool behavior_name(std::string_view s) {
    if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    return std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'; });
}

bool identifier(std::string_view s) {
    if (s.empty() || !(s[0] >= 'a' && s[0] <= 'z')) return false;
    return std::ranges::all_of(s, [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; });
}

std::string_view type_name(Toml::Type t) {
    switch (t) {
        case Toml::Type::table: return "a table";
        case Toml::Type::array: return "a list";
        case Toml::Type::text: return "a string";
        case Toml::Type::integer: return "an integer";
        case Toml::Type::real: return "a number";
        case Toml::Type::boolean: return "a boolean";
        default: return "a date";
    }
}

// ── compiling ────────────────────────────────────────────────────────────

struct Compiler {
    std::string              file;
    world::BehaviorProblems& out;

    void error(const Toml* n, std::string message, std::string hint = {}) {
        out.push_back({file, n ? n->line : 0, n ? n->column : 0, std::move(message), std::move(hint)});
    }

    template <std::size_t N>
    void only(const Toml& t, const std::array<std::string_view, N>& keys, std::string_view where) {
        for (std::size_t i = 0; i < t.keys.size(); ++i)
            if (std::ranges::find(keys, t.keys[i]) == keys.end())
                error(&t.items[i], std::format("{} has no key `{}`", where, t.keys[i]), did_you_mean(t.keys[i], keys, "keys"));
    }

    const Toml* table(const Toml* n, std::string_view what) {
        if (!n) return nullptr;
        if (n->type == Toml::Type::table) return n;
        error(n, std::format("{} is a table, not {}", what, type_name(n->type)));
        return nullptr;
    }

    std::optional<std::string> text(const Toml* n, std::string_view what) {
        if (!n) return std::nullopt;
        if (n->type == Toml::Type::text) return n->text;
        error(n, std::format("{} is a string, not {}", what, type_name(n->type)));
        return std::nullopt;
    }

    // Every call in it is one of the expression language's functions, and
    // every name it reads is one the place offers.
    bool check(const expr::Node& node, const Toml* at, std::span<const std::string_view> names, std::string_view where) {
        bool ok = true;
        if (node.op == expr::Op::call && std::ranges::find(expr::functions(), node.name) == expr::functions().end()) {
            error(at, std::format("{}: `{}` is not a function of expressions", where, node.name), did_you_mean(node.name, expr::functions(), "functions"));
            ok = false;
        }
        if (node.op == expr::Op::ref && std::ranges::find(names, node.name) == names.end()) {
            error(at, std::format("{}: there is no `{}` here", where, node.name), did_you_mean(node.name, names, "names here"));
            ok = false;
        }
        for (const auto& a : node.args) ok = check(*a, at, names, where) && ok;
        return ok;
    }

    std::optional<expr::Expr> parse(const Toml* n, std::string_view where) {
        auto e = expr::parse(n->text);
        if (!e) {
            // The column inside the string, past its opening quote.
            out.push_back({file, n->line, n->column + 1 + static_cast<int>(e.error().at), std::format("{}: {}", where, e.error().message), e.error().hint});
            return std::nullopt;
        }
        return std::move(*e);
    }

    std::optional<expr::Expr> condition(const Toml* n, std::span<const std::string_view> names, std::string_view where) {
        auto s = text(n, where);
        if (!s) return std::nullopt;
        auto e = parse(n, where);
        if (!e || !check(e->node(), n, names, where)) return std::nullopt;
        return e;
    }

    // `do`: an operation, or a list of them.
    std::vector<Step> steps(const Toml* n, std::span<const std::string_view> names, std::string_view where, bool may_capture) {
        std::vector<Step> out_steps;
        if (!n) return out_steps;
        std::vector<const Toml*> items;
        if (n->type == Toml::Type::text) items.push_back(n);
        else if (n->type == Toml::Type::array)
            for (const Toml& i : n->items) items.push_back(&i);
        else {
            error(n, std::format("{} is an operation or a list of them, not {}", where, type_name(n->type)), "do = [\"capture()\", \"select(hit.node)\"]");
            return out_steps;
        }
        for (const Toml* item : items) {
            if (item->type != Toml::Type::text) {
                error(item, std::format("{}: an operation is a string, not {}", where, type_name(item->type)));
                continue;
            }
            auto e = parse(item, where);
            if (!e) continue;
            const expr::Node& call = e->node();
            if (call.op != expr::Op::call) {
                error(item, std::format("{}: `{}` is not an operation", where, item->text), "an operation is a call: `move(hit.node, delta.world)`");
                continue;
            }
            const auto found = std::ranges::find(op_specs, std::string_view(call.name), [](const OpSpec& o) { return o.info.name; });
            if (found == op_specs.end()) {
                std::vector<std::string_view> all;
                for (const OpSpec& o : op_specs) all.push_back(o.info.name);
                error(item, std::format("{}: `{}` is not an operation of a canvas behaviour", where, call.name), did_you_mean(call.name, all, "operations"));
                continue;
            }
            if (call.args.size() < found->least || call.args.size() > found->most) {
                error(item, std::format("{}: `{}` takes {} argument{}, not {}", where, call.name,
                                        found->least == found->most ? std::format("{}", found->least) : std::format("{} or {}", found->least, found->most),
                                        found->most == 1 ? "" : "s", call.args.size()),
                      std::string(found->info.signature));
                continue;
            }
            const Op op = static_cast<Op>(found - op_specs.begin());
            if (op == Op::capture && !may_capture) {
                error(item, std::format("{}: `capture()` is for a pointer_down transition", where), "a press is taken when it begins");
                continue;
            }
            Step st{op, {}, item->text, item->line};
            bool ok = true;
            for (const auto& a : call.args) {
                ok = check(*a, item, names, where) && ok;
                st.args.emplace_back(a);
            }
            if (ok) out_steps.push_back(std::move(st));
        }
        return out_steps;
    }

    std::optional<Chart> behavior(std::string_view name, const Toml& t) {
        const std::size_t before = out.size();
        Chart c;
        c.name = std::string(name);
        c.file = file;
        const std::string where = std::format("behaviour `{}`", name);
        if (!behavior_name(name))
            error(&t, std::format("`{}` is not a behaviour name", name), "lower case, digits, `-`, `_` and `.`, a letter first: canvases name it in `behaviors`");
        only(t, behavior_keys, where);
        if (auto on = text(t.get("on"), std::format("{}'s `on`", where)); on && *on != "canvas")
            error(t.get("on"), std::format("{} is on `{}`: only canvases have behaviours", where, *on), "on = \"canvas\"");
        if (auto doc = text(t.get("doc"), "`doc`")) c.doc = *doc;
        if (const Toml* p = t.get("priority")) {
            if (p->type == Toml::Type::integer && std::abs(p->number) < 1e6) c.priority = static_cast<int>(p->number);
            else error(p, std::format("{}'s `priority` is a whole number", where), "presses are offered from the highest: canvas-kit's select is 30, drag 40, connect 50, resize 60, minimap 100");
        }
        // States: first their names (transitions name each other), then what they do.
        const Toml* states = table(t.get("states"), std::format("{}'s `states`", where));
        if (states)
            for (std::size_t i = 0; i < states->keys.size(); ++i) {
                if (!identifier(states->keys[i])) error(&states->items[i], std::format("`{}` is not a state name", states->keys[i]), "lower case, digits and _, a letter first");
                c.states.push_back({states->keys[i], {}, {}, {}});
            }
        std::vector<std::string_view> state_names;
        for (const State& s : c.states) state_names.push_back(s.name);
        if (states)
            for (std::size_t i = 0; i < states->keys.size(); ++i) {
                const Toml* st = table(&states->items[i], std::format("state `{}`", states->keys[i]));
                if (!st) continue;
                State& s = c.states[i];
                const std::string sw = std::format("state `{}`", s.name);
                only(*st, state_keys, sw);
                s.entry = steps(st->get("entry"), entry_names, sw + "'s entry", false);
                s.exit = steps(st->get("exit"), entry_names, sw + "'s exit", false);
                const Toml* on = table(st->get("on"), sw + "'s `on`");
                if (!on) continue;
                for (std::size_t k = 0; k < on->keys.size(); ++k) {
                    const auto ev = std::ranges::find(event_names, on->keys[k]);
                    if (ev == event_names.end()) {
                        error(&on->items[k], std::format("{} has no event `{}`", sw, on->keys[k]), did_you_mean(on->keys[k], event_names, "events"));
                        continue;
                    }
                    const auto e = static_cast<Event>(ev - event_names.begin());
                    const std::string tw = std::format("{} on {}", sw, *ev);
                    std::vector<const Toml*> list;
                    const Toml& v = on->items[k];
                    if (v.type == Toml::Type::table) list.push_back(&v);
                    else if (v.type == Toml::Type::array)
                        for (const Toml& x : v.items) list.push_back(&x);
                    for (const Toml* x : list) {
                        if (x->type != Toml::Type::table) {
                            error(x, std::format("{}: a transition is a table, not {}", tw, type_name(x->type)), "{ when = \"…\", to = \"…\", do = [\"…\"] }");
                            continue;
                        }
                        only(*x, transition_keys, tw);
                        Transition tr;
                        if (const Toml* w = x->get("when")) tr.when = condition(w, names_for(e), tw + "'s `when`");
                        if (auto to = text(x->get("to"), tw + "'s `to`")) {
                            const auto at = std::ranges::find(state_names, *to);
                            if (at == state_names.end())
                                error(x->get("to"), std::format("{} goes to `{}`, which is not one of its states", tw, *to), did_you_mean(*to, state_names, "states"));
                            else tr.to = static_cast<int>(at - state_names.begin());
                        }
                        tr.steps = steps(x->get("do"), names_for(e), tw, e == Event::pointer_down);
                        s.on[static_cast<std::size_t>(e)].push_back(std::move(tr));
                    }
                    if (list.empty()) error(&v, std::format("{} is a transition or a list of them, not {}", tw, type_name(v.type)));
                }
            }
        if (const Toml* init = t.get("initial")) {
            if (auto s = text(init, std::format("{}'s `initial`", where))) {
                const auto at = std::ranges::find(state_names, *s);
                if (at == state_names.end()) error(init, std::format("{} begins in `{}`, which is not one of its states", where, *s), did_you_mean(*s, state_names, "states"));
                else c.initial = static_cast<int>(at - state_names.begin());
            }
        } else if (!c.states.empty()) {
            error(&t, std::format("{} has states but no `initial`", where), std::format("initial = \"{}\"", c.states.front().name));
        }
        if (const Toml* actions = table(t.get("actions"), std::format("{}'s `actions`", where)))
            for (std::size_t i = 0; i < actions->keys.size(); ++i) {
                const std::string aw = std::format("action `{}`", actions->keys[i]);
                const Toml* a = table(&actions->items[i], aw);
                if (!a) continue;
                only(*a, action_keys, aw);
                ActionDef d;
                d.name = actions->keys[i];
                if (!identifier(d.name)) error(&actions->items[i], std::format("`{}` is not an action name", d.name), "lower case, digits and _, a letter first");
                if (d.name == "focus") error(&actions->items[i], "`focus` is the canvas's own action on every node", "name it after what it does");
                if (auto doc = text(a->get("doc"), aw + "'s `doc`")) d.doc = *doc;
                if (auto eff = text(a->get("effect"), aw + "'s `effect`")) {
                    if (auto parsed = spec::parse_effect(*eff)) d.effect = *parsed;
                    else error(a->get("effect"), std::format("{}: `{}` is not an effect", aw, *eff), did_you_mean(*eff, effect_names, "effects"));
                }
                if (auto ans = text(a->get("answer"), aw + "'s `answer`")) d.answer = *ans;
                if (spec::requires_answer(d.effect) && d.answer.empty())
                    error(&actions->items[i], std::format("{} is {}, so it states the answer that confirms it (S-1)", aw, spec::name(d.effect)), "answer = \"yes\"");
                if (const Toml* args = table(a->get("args"), aw + "'s `args`"))
                    for (std::size_t k = 0; k < args->keys.size(); ++k) {
                        auto type = text(&args->items[k], std::format("{}'s argument `{}`", aw, args->keys[k]));
                        if (!type) continue;
                        if (std::ranges::find(arg_types, *type) == arg_types.end())
                            error(&args->items[k], std::format("{}: `{}` is not a type", aw, *type), did_you_mean(*type, arg_types, "types"));
                        d.args.emplace_back(args->keys[k], *type);
                    }
                std::ranges::sort(d.args);  // by name, as the catalogue lists them
                if (const Toml* w = a->get("when")) d.when = condition(w, action_when_names, aw + "'s `when`");
                d.steps = steps(a->get("do"), action_names, aw, false);
                if (!a->get("do")) error(a, std::format("{} does nothing", aw), "do = [\"move(node.key, {'x': args.dx, 'y': args.dy})\"]");
                c.actions.push_back(std::move(d));
            }
        if (c.states.empty() && c.actions.empty()) error(&t, std::format("{} has neither states nor actions", where));
        if (out.size() != before) return std::nullopt;
        return c;
    }
};

// ── running ──────────────────────────────────────────────────────────────

Value point(double x, double y) { return Value{}.set("x", Value(x)).set("y", Value(y)); }

Value pointer_value(const PointerInput& in) {
    std::string_view button = "none";
    switch (in.button) {
        case MouseButton::left: button = "left"; break;
        case MouseButton::right: button = "right"; break;
        case MouseButton::middle: button = "middle"; break;
        default: break;
    }
    return Value{}
        .set("x", Value(in.screen.x))
        .set("y", Value(in.screen.y))
        .set("world", point(in.world[0], in.world[1]))
        .set("button", Value(button))
        .set("clicks", Value(static_cast<int>(in.clicks)));
}

// What a pointer is over; with `inside`, the keys of the nodes lying wholly
// on it too, bottom to top (a frame dragged with what is on it).
Value hit_value(const Scene& s, const std::optional<world::Hit>& hit, bool inside = false) {
    if (!hit || !s.valid(hit->node)) return {};
    Value v = Value{}.set("node", Value(s.key(hit->node))).set("kind", Value(s.kind_name(hit->node))).set("title", Value(hit->title));
    if (inside) {
        const WorldRect r = s.rect(hit->node);
        std::vector<NodeRef> under;
        s.query(r, under);
        Value::List keys;
        for (NodeRef n : under) {
            if (n == hit->node) continue;
            const WorldRect q = s.rect(n);
            if (q.x >= r.x && q.y >= r.y && q.right() <= r.right() && q.bottom() <= r.bottom()) keys.emplace_back(s.key(n));
        }
        v.set("inside", Value(std::move(keys)));
    }
    return v;
}

Value mods_value(Modifiers m) {
    return Value{}
        .set("shift", Value(m.has(Modifier::shift)))
        .set("ctrl", Value(m.has(Modifier::ctrl)))
        .set("alt", Value(m.has(Modifier::alt)))
        .set("meta", Value(m.has(Modifier::meta)))
        .set("mod", Value(m.has(primary_modifier())));
}

Value node_value(const Scene& s, NodeRef n) {
    const WorldRect r = s.rect(n);
    return Value{}
        .set("key", Value(s.key(n)))
        .set("kind", Value(s.kind_name(n)))
        .set("x", Value(r.x))
        .set("y", Value(r.y))
        .set("width", Value(r.width))
        .set("height", Value(r.height));
}

class Declarative final : public Behavior {
public:
    Declarative(std::shared_ptr<const Chart> chart, std::string_view args) : chart_(std::move(chart)), state_(chart_->initial) {
        vars_.set("args", Value(args));
    }

    std::string_view name() const override { return chart_->name; }
    int priority() const override { return chart_->priority; }

    Claim pointer(Host& h, const PointerInput& in) override {
        Env env{h};
        env.in = &in;
        switch (in.kind) {
            case PointerKind::down:
                press_ = pointer_value(in);
                press_hit_ = hit_value(h.scene(), in.hit, true);
                start_ = last_ = {in.screen, in.world};
                captured_ = false;
                env.hit = &press_hit_;
                dispatch(env, Event::pointer_down);
                if (!captured_) {
                    press_ = press_hit_ = Value{};
                    return Claim::pass;
                }
                return Claim::take;
            case PointerKind::move:
                env.hit = &press_hit_;
                dispatch(env, Event::pointer_move);
                last_ = {in.screen, in.world};
                return Claim::take;
            default:
                env.hit = &press_hit_;
                dispatch(env, in.kind == PointerKind::up ? Event::pointer_up : Event::pointer_cancel);
                captured_ = false;
                press_ = press_hit_ = Value{};
                return Claim::take;
        }
    }

    bool tap(Host& h, const PointerInput& in) override {
        Env env{h};
        env.in = &in;
        const Value hit = hit_value(h.scene(), in.hit, true);
        env.hit = &hit;
        start_ = last_ = {in.screen, in.world};
        return dispatch(env, Event::tap);
    }

    bool key(Host& h, const KeyEvent& e) override {
        Env env{h};
        env.key = &e;
        if (captured_) env.hit = &press_hit_;
        return dispatch(env, Event::key);
    }

    void describe(Host& h, NodeRef n, spec::SemanticNode& node) override {
        for (const ActionDef& a : chart_->actions) {
            if (a.when) {
                Env env{h};
                env.node = n;
                const expr::Result r = evaluate(*a.when, env);
                if (!r.ok() || !r.value.as_bool(false)) continue;
            }
            node.actions.push_back({a.name, a.effect, {}, a.answer, a.args});
        }
    }

    std::optional<ActResult> act(Host& h, NodeRef n, std::string_view action, const Value& args) override {
        const auto a = std::ranges::find(chart_->actions, action, &ActionDef::name);
        if (a == chart_->actions.end()) return std::nullopt;
        Env env{h};
        env.node = n;
        env.args = &args;
        for (const Step& s : a->steps)
            if (auto problem = run(env, s)) return ActResult::fail(std::format("{} ({}:{}): `{}`: {}", chart_->name, chart_->file, s.line, s.text, *problem));
        return ActResult::done();
    }

private:
    struct Env {
        Host&               h;
        const PointerInput* in = nullptr;
        const KeyEvent*     key = nullptr;
        const Value*        hit = nullptr;
        std::optional<NodeRef> node;
        const Value*        args = nullptr;
    };
    struct Where {
        Point                 screen;
        std::array<double, 2> world;
    };

    Value resolve(const Env& env, std::string_view name) const {
        Scene& s = env.h.scene();
        if (name == "pointer") return env.in ? pointer_value(*env.in) : Value{};
        if (name == "press") return press_.is_null() && env.in ? pointer_value(*env.in) : press_;  // null outside a gesture, for a key
        if (name == "delta" || name == "travel") {
            // Where the pointer is: this event's, or (a key during a gesture) its last.
            if (!env.in && !captured_) return {};
            const Where now = env.in ? Where{env.in->screen, env.in->world} : last_;
            const Where& from = name == "delta" ? last_ : start_;
            return point(now.screen.x - from.screen.x, now.screen.y - from.screen.y).set("world", point(now.world[0] - from.world[0], now.world[1] - from.world[1]));
        }
        if (name == "hit") return env.hit ? *env.hit : Value{};
        if (name == "over") return env.in ? hit_value(s, env.in->hit) : Value{};
        if (name == "mods") return env.in ? mods_value(env.in->mods) : env.key ? mods_value(env.key->mods) : Value{};
        if (name == "key") return env.key ? Value(omui::name(env.key->key)) : Value{};
        if (name == "selection") return keys_value(env.h.selection());
        if (name == "zoom") return Value(env.h.camera().zoom);
        if (name == "vars") return vars_;
        if (name == "state") return chart_->states.empty() ? Value{} : Value(chart_->states[static_cast<std::size_t>(state_)].name);
        if (name == "node") return env.node && s.valid(*env.node) ? node_value(s, *env.node) : Value{};
        if (name == "args") return env.args ? *env.args : Value{};
        return {};
    }

    expr::Result evaluate(const expr::Expr& e, const Env& env) const {
        return expr::evaluate(e, [&](std::string_view name) -> std::optional<Value> { return resolve(env, name); });
    }

    // Logged once a step: a behaviour that fails every move says so once.
    void complain(const void* what, int line, std::string_view text, std::string_view problem) {
        if (std::ranges::find(complained_, what) != complained_.end()) return;
        complained_.push_back(what);
        warn("canvas behaviour {} ({}:{}): `{}`: {}", chart_->name, chart_->file, line, text, problem);
    }

    // The first transition of the event whose `when` holds; true if one did.
    bool dispatch(Env& env, Event e) {
        if (chart_->states.empty()) return false;
        const State& now = chart_->states[static_cast<std::size_t>(state_)];
        for (const Transition& t : now.on[static_cast<std::size_t>(e)]) {
            if (t.when) {
                const expr::Result r = evaluate(*t.when, env);
                if (!r.ok()) {
                    complain(&t, 0, t.when->text(), r.errors.front().message);
                    continue;
                }
                if (!r.value.as_bool(false)) continue;
            }
            // Leaving (a transition to a state, itself included), its own operations, entering.
            if (t.to >= 0) run_all(env, now.exit);
            run_all(env, t.steps);
            if (t.to >= 0) {
                state_ = t.to;
                run_all(env, chart_->states[static_cast<std::size_t>(state_)].entry);
            }
            return true;
        }
        return false;
    }

    void run_all(Env& env, const std::vector<Step>& steps) {
        for (const Step& s : steps)
            if (auto problem = run(env, s)) complain(&s, s.line, s.text, *problem);
    }

    std::vector<NodeRef> targets(Scene& s, const Value& v, std::string& problem) const {
        std::vector<NodeRef> out;
        auto one = [&](const Value& k) {
            if (k.type() != Value::Type::text) {
                problem = std::format("a target is a node's key or a list of them, not {}", expr::text_of(k));
                return;
            }
            if (auto n = s.find(k.as_string())) out.push_back(*n);
            else problem = std::format("no node has the key `{}`", k.as_string());
        };
        if (v.is_null()) return out;  // nothing (no node was hit)
        if (const Value::List* l = v.list())
            for (const Value& k : *l) one(k);
        else one(v);
        return out;
    }

    // One operation; what went wrong, if something did.
    std::optional<std::string> run(Env& env, const Step& st) {
        std::vector<Value> a;
        a.reserve(st.args.size());
        for (const expr::Expr& e : st.args) {
            expr::Result r = evaluate(e, env);
            if (!r.ok()) return r.errors.front().message;
            a.push_back(std::move(r.value));
        }
        Host& h = env.h;
        Scene& s = h.scene();
        std::string problem;
        auto number = [&](const Value& v, std::string_view what) -> std::optional<double> {
            if (v.is_number() && std::isfinite(v.as_double())) return v.as_double();
            problem = std::format("{} is a number, not {}", what, expr::text_of(v));
            return std::nullopt;
        };
        auto keys_of = [&](const std::vector<NodeRef>& nodes) {
            std::vector<std::string> keys;
            for (NodeRef n : nodes) keys.emplace_back(s.key(n));
            return keys;
        };
        switch (st.op) {
            case Op::capture: captured_ = true; break;
            case Op::move: {
                const auto nodes = targets(s, a[0], problem);
                const Value* x = a[1].get("x");
                const Value* y = a[1].get("y");
                if (!x || !y) return std::format("the delta is a {{x, y}}, not {}", expr::text_of(a[1]));
                const auto dx = number(*x, "delta.x"), dy = number(*y, "delta.y");
                if (!dx || !dy) return problem;
                for (NodeRef n : nodes) {
                    const WorldRect r = s.rect(n);
                    s.move(n, r.x + *dx, r.y + *dy);
                }
                break;
            }
            case Op::move_to: {
                const auto nodes = targets(s, a[0], problem);
                const auto x = number(a[1], "x"), y = number(a[2], "y");
                if (!x || !y) return problem;
                for (NodeRef n : nodes) s.move(n, *x, *y);
                break;
            }
            case Op::resize: {
                const auto nodes = targets(s, a[0], problem);
                const auto w = number(a[1], "width"), hg = number(a[2], "height");
                if (!w || !hg) return problem;
                if (!(*w > 0) || !(*hg > 0)) return std::format("a size is above 0, not {} × {}", *w, *hg);
                for (NodeRef n : nodes) s.resize(n, static_cast<float>(*w), static_cast<float>(*hg));
                break;
            }
            case Op::raise:
                for (NodeRef n : targets(s, a[0], problem)) s.raise(n);
                break;
            case Op::select: h.select(keys_of(targets(s, a[0], problem))); break;
            case Op::add_to_selection:
            case Op::toggle_selection: {
                std::vector<std::string> keys = h.selection();
                for (const std::string& k : keys_of(targets(s, a[0], problem))) {
                    const auto at = std::ranges::find(keys, k);
                    if (at == keys.end()) keys.push_back(k);
                    else if (st.op == Op::toggle_selection) keys.erase(at);
                }
                h.select(std::move(keys));
                break;
            }
            case Op::clear_selection: h.select({}); break;
            case Op::connect:
            case Op::disconnect: {
                const auto from = targets(s, a[0], problem), to = targets(s, a[1], problem);
                if (!problem.empty()) return problem;
                if (from.size() != 1 || to.size() != 1) return std::string("connects one node to one node");
                const std::string f(s.key(from[0])), t(s.key(to[0]));
                if (f == t) return std::string("a node does not connect to itself");
                if (st.op == Op::connect) (void)s.connect({.from = f, .to = t});
                else {
                    std::vector<std::string> gone;
                    for (const world::Edge* e : s.edges_of(f))
                        if ((e->from == f && e->to == t) || (e->from == t && e->to == f)) gone.push_back(e->key);
                    for (const std::string& k : gone) s.disconnect(k);
                }
                break;
            }
            case Op::remove: {
                const auto nodes = targets(s, a[0], problem);
                const std::vector<std::string> keys = keys_of(nodes);
                for (NodeRef n : nodes) s.remove(n);
                std::vector<std::string> left = h.selection();
                std::erase_if(left, [&](const std::string& k) { return std::ranges::find(keys, k) != keys.end(); });
                h.select(std::move(left));
                break;
            }
            case Op::commit: {
                if (a[0].type() != Value::Type::text || a[0].as_string().empty()) return std::format("an event is a name, not {}", expr::text_of(a[0]));
                h.committed(a[0].as_string(), a.size() > 1 ? a[1] : Value{});
                break;
            }
            case Op::set: {
                if (a[0].type() != Value::Type::text || !identifier(a[0].as_string())) return std::format("a variable is a name, not {}", expr::text_of(a[0]));
                if (a[0].as_string() == "args") return std::string("`args` holds what follows the behaviour's name in `behaviors`");
                vars_.set(a[0].as_string(), std::move(a[1]));
                break;
            }
            case Op::fit: {
                if (a[0].type() == Value::Type::text && a[0].as_string() == "all") {
                    if (s.size()) h.animate_to(world::Camera::fit(s.bounds(), h.viewport(), 24, 1e-9, 1e9));
                    break;
                }
                const auto nodes = targets(s, a[0], problem);
                if (nodes.empty()) break;
                WorldRect r = s.rect(nodes.front());
                for (NodeRef n : nodes) r = r.united(s.rect(n));
                h.animate_to(world::Camera::fit(r, h.viewport(), 24, 1e-9, 1e9));
                break;
            }
            case Op::preset: {
                if (a[0].type() != Value::Type::text) return std::format("a view is a name, not {}", expr::text_of(a[0]));
                const auto p = s.preset(a[0].as_string());
                if (!p) return std::format("the scene has no view named `{}`", a[0].as_string());
                h.animate_to({p->x, p->y, p->zoom});
                break;
            }
            case Op::zoom_to: {
                const auto z = number(a[0], "zoom");
                if (!z) return problem;
                if (!(*z > 0)) return std::format("a zoom is above 0, not {}", *z);
                world::Camera c = h.camera();
                c.zoom = *z;
                h.animate_to(c);
                break;
            }
        }
        if (!problem.empty()) return problem;
        return std::nullopt;
    }

    std::shared_ptr<const Chart> chart_;
    int                          state_ = 0;
    Value                        vars_;
    Value                        press_, press_hit_;
    Where                        start_{}, last_{};
    bool                         captured_ = false;
    std::vector<const void*>     complained_;
};

}  // namespace

}  // namespace omui::kit

namespace omui::world {

std::expected<std::vector<std::string>, BehaviorProblems> load_behaviors(std::string_view text, std::string_view file) {
    BehaviorProblems problems;
    auto root = kit::parse_toml(text, file);
    if (!root) {
        problems.push_back(std::move(root.error()));
        return std::unexpected(std::move(problems));
    }
    kit::Compiler c{std::string(file), problems};
    constexpr std::array<std::string_view, 1> top{"behavior"};
    c.only(*root, top, "a behaviour file");
    const kit::Toml* behaviors = c.table(root->get("behavior"), "`behavior`");
    if (!behaviors && !root->get("behavior")) c.error(&*root, "the file declares no behaviour", "[behavior.<name>] on = \"canvas\" …");
    std::vector<kit::Chart> charts;
    if (behaviors)
        for (std::size_t i = 0; i < behaviors->keys.size(); ++i) {
            const kit::Toml* t = c.table(&behaviors->items[i], std::format("behaviour `{}`", behaviors->keys[i]));
            if (!t) continue;
            if (auto chart = c.behavior(behaviors->keys[i], *t)) charts.push_back(std::move(*chart));
        }
    if (!problems.empty()) {
        // In the file's order (toml++ hands tables over sorted by key).
        std::ranges::stable_sort(problems, {}, [](const BehaviorProblem& p) { return std::pair{p.line, p.column}; });
        return std::unexpected(std::move(problems));
    }
    // All or nothing: a file with a problem registers none of its behaviours.
    std::vector<std::string> names;
    for (kit::Chart& chart : charts) {
        names.push_back(chart.name);
        auto shared = std::make_shared<const kit::Chart>(std::move(chart));
        register_behavior(names.back(), [shared](std::string_view args) -> std::unique_ptr<Behavior> { return std::make_unique<kit::Declarative>(shared, args); });
    }
    return names;
}

std::expected<std::vector<std::string>, BehaviorProblems> load_behavior_file(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(BehaviorProblems{{utf8(file), 0, 0, "cannot read the file", ""}});
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return load_behaviors(text, utf8(file));
}

std::span<const BehaviorOperation> behavior_operations() noexcept { return kit::operation_list; }

}  // namespace omui::world
