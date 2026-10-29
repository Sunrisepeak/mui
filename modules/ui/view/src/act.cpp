// Actions and semantics: what a node says about itself (contract ①) and
// performing its actions under the rules of spec §3.3 (S-1 effects and
// answers, S-4 removal, disabled nodes).
module openmui.view;

import std;
import openmui.core;
import :impl;

namespace omui {

namespace {

// Whether a value is of a declared argument type: a number with no fraction
// is an integer, whichever way the caller's language wrote it.
bool of_one_type(const Value& v, std::string_view type) {
    if (type == "string") return v.type() == Value::Type::text;
    if (type == "bool") return v.type() == Value::Type::boolean;
    if (type == "f64" || type == "f32") return v.is_number();
    if (type == "i32" || type == "i64") {
        if (v.type() == Value::Type::integer) return type == "i64" || (v.as_int() >= std::numeric_limits<std::int32_t>::min() &&
                                                                      v.as_int() <= std::numeric_limits<std::int32_t>::max());
        const double d = v.as_double();
        return v.type() == Value::Type::real && std::isfinite(d) && d == std::trunc(d) &&
               std::abs(d) <= (type == "i32" ? 2147483647.0 : 9007199254740992.0);
    }
    if (type == "list<string>") {
        const Value::List* l = v.list();
        return l && std::ranges::all_of(*l, [](const Value& x) { return x.type() == Value::Type::text; });
    }
    if (type == "any" || type == "point" || type == "rect" || type == "size" || type == "color") return true;
    return v.type() == Value::Type::text && v.as_string() == type;  // a symbol: `end`, `-page`
}

// A type, or alternatives `f64 | min | max` (spec §3.3).
bool of_type(const Value& v, std::string_view type) {
    for (auto part : type | std::views::split('|')) {
        std::string_view t(part.begin(), part.end());
        while (!t.empty() && t.front() == ' ') t.remove_prefix(1);
        while (!t.empty() && t.back() == ' ') t.remove_suffix(1);
        if (of_one_type(v, t)) return true;
    }
    return false;
}

// What is wrong with the arguments of a call to an action that declares
// them (spec §3.3): empty when nothing is.
std::string invalid_arguments(const spec::ActionDesc& a, const Value& args) {
    const Value::Map empty;
    const Value::Map* given = args.map();
    std::vector<std::string> problems;
    if (!given && !args.is_null()) problems.emplace_back("the arguments are a map of names to values");
    if (!given) given = &empty;
    for (const auto& [name, type] : a.args) {
        auto it = std::ranges::find(*given, name, &std::pair<std::string, Value>::first);
        if (it == given->end()) problems.push_back(std::format("`{}` ({}) is missing", name, type));
        else if (!of_type(it->second, type)) problems.push_back(std::format("`{}` is a {}", name, type));
    }
    for (const auto& [name, v] : *given)
        if (std::ranges::find(a.args, name, &std::pair<std::string, std::string>::first) == a.args.end())
            problems.push_back(std::format("`{}` is not an argument of it", name));
    std::string out;
    for (const std::string& p : problems) out += (out.empty() ? "" : "; ") + p;
    if (out.empty()) return out;
    std::string takes;
    for (const auto& [name, type] : a.args) takes += std::format("{}{}: {}", takes.empty() ? "" : ", ", name, type);
    return std::format("{} (it takes {})", out, takes);
}

}  // namespace

void Runtime::Impl::call_handlers(NodeId id, std::string_view event, const Value& args) {
    if (!alive(id)) return;
    std::vector<Handler> calls;
    for (const auto& [name, h] : at(id).handlers)
        if (name == event && h) calls.push_back(h);
    for (const RuntimeHandler& h : runtime_handlers)
        if (h.node == id && h.event == event && h.fn) calls.push_back(h.fn);
    // Copies: a handler may change the tree, even remove this node.
    for (Handler& h : calls) batch([&] { h(args); });
}

bool Runtime::Impl::has_handler(NodeId id, std::string_view event) const {
    const Node& n = at(id);
    for (const auto& [name, h] : n.handlers)
        if (name == event) return true;
    for (const RuntimeHandler& h : runtime_handlers)
        if (h.node == id && h.event == event) return true;
    return false;
}

spec::SemanticNode Runtime::Impl::semantics(NodeId id) const {
    const Node& n = at(id);
    spec::SemanticNode s;
    s.id = id == root ? std::string("app") : self.path(id);
    s.role = n.type->contract ? n.type->contract->role : n.type->role;
    auto prop = [&](std::string_view k) -> const Value* {
        auto it = n.props.find(k);
        return it == n.props.end() ? nullptr : &it->second;
    };
    if (const Value* v = prop("role"))
        if (auto r = spec::parse_role(v->as_string())) s.role = *r;
    if (const Value* v = prop("label")) s.name = std::string(v->as_string());
    if (const Value* v = prop("description")) s.description = std::string(v->as_string());
    if (const Value* v = prop("origin"))
        if (auto o = spec::parse_origin(v->as_string())) s.origin = *o;

    const bool focusable = is_focusable(id);
    s.states.set(State::disabled, n.state.disabled)
        .set(State::focusable, focusable)
        .set(State::focused, n.state.focused)
        .set(State::hovered, n.state.hovered)
        .set(State::pressed, n.state.pressed)
        .set(State::removing, n.state.removing)
        .set(State::animating, n.state.animating)
        .set(State::hidden, !displayed(id));
    // Without layout (H0) a node has no place: neither bounds nor offscreen.
    const bool placed = this->placed;  // the pipeline computes bounds
    if (placed && id != root && !n.bounds.intersects(Rect{0, 0, size.width, size.height})) s.states.set(State::offscreen);

    // Actions: the contract's, then the verb's classification of activate.
    auto add = [&](std::string_view name, Effect effect, std::span<const spec::ArgInfo> args = {}) -> spec::ActionDesc& {
        for (spec::ActionDesc& a : s.actions)
            if (a.name == name) return a;
        spec::ActionDesc a;
        a.name = std::string(name);
        a.effect = effect;
        for (const spec::ArgInfo& x : args) a.args.emplace_back(std::string(x.name), std::string(x.type));
        s.actions.push_back(std::move(a));
        return s.actions.back();
    };
    if (const spec::ContractInfo* c = n.type->contract)
        for (const spec::ActionSpec& a : c->actions) add(a.name, a.effect, a.args).answer = std::string(a.answer);
    else if (has_handler(id, "press") || has_handler(id, "activate") || prop("verb"))
        add("activate", Effect::reversible);
    if (const Value* v = prop("verb"); v && !v->as_string().empty()) {
        spec::ActionDesc& a = add("activate", Effect::reversible);
        a.verb = std::string(v->as_string());
        if (const VerbDesc* d = verbs.find(a.verb)) {
            a.effect = std::max(a.effect, d->effect);
            a.answer = d->answer;
            a.person = a.person || d->person;
        }
    }
    auto activate = [&]() -> spec::ActionDesc* {
        auto it = std::ranges::find(s.actions, std::string_view("activate"), &spec::ActionDesc::name);
        return it == s.actions.end() ? nullptr : &*it;
    };
    // A stated effect may only strengthen the classification (S-1).
    if (const Value* v = prop("effect"))
        if (auto e = spec::parse_effect(v->as_string()))
            if (spec::ActionDesc* a = activate()) a->effect = std::max(a->effect, *e);
    if (const Value* v = prop("answer"))
        if (spec::ActionDesc* a = activate()) a->answer = std::string(v->as_string());
    // The person's own (S-10): a node may only add the mark, never lift its verb's.
    if (const Value* v = prop("person"); v && v->as_bool())
        if (spec::ActionDesc* a = activate()) a->person = true;
    // A generic node that can be activated is a button to every reader
    // (what ARIA asks of a clickable div), so it is found as one and must be named (S-6).
    if (s.role == Role::generic && activate() && !prop("role")) s.role = Role::button;
    if (focusable) {
        add("focus", Effect::read);
        add("blur", Effect::read);
    }
    // A history's scope offers undo and redo while there is something to take (S-12).
    if (is_scope(id))
        if (const ScopeHistory* h = find_history(id)) {
            if (!h->undo.empty() && !h->undo.back().barrier) add("undo", Effect::reversible);
            if (!h->redo.empty()) add("redo", Effect::reversible);
        }
    for (NodeId c = n.parent; alive(c); c = at(c).parent)
        if (at(c).type->scroll_offset) {
            add("scroll_into_view", Effect::read);
            break;
        }

    if (placed) s.bounds = to_spec(n.bounds);
    for (NodeId c : n.children) s.children.push_back(self.path(c));
    if (n.type->semantics) {
        SemanticsContext ctx{self, id, n.state};
        n.type->semantics(n.instance, ctx, s);
    }
    return s;
}

Envelope Runtime::Impl::perform(NodeId id, std::string_view action, const Invocation& call, bool command, Outcome* out) {
    Envelope env;
    // A dry run answers where the checks stopped as its outcome (S-13).
    Effect effect = Effect::external;  // until known: the safe reading
    auto stop = [&](Envelope e) { return call.dry_run ? dry_run_of(e, effect) : e; };
    if (!alive(id)) {
        env.error(spec::error_code::not_found, "no such node (it was removed)").result(spec::exit_code::failure);
        return stop(std::move(env));
    }
    const std::string where = self.path(id);
    if (at(id).state.removing) {
        env.error(spec::error_code::refused, std::format("'{}' is being removed (spec S-4)", where)).result(spec::exit_code::failure);
        return stop(std::move(env));
    }
    // A history's own actions, unless the component's contract has its own.
    if ((action == "undo" || action == "redo") && is_scope(id) && !(at(id).type->contract && at(id).type->contract->action(action)))
        return history_action(id, action == "redo", call);
    const spec::SemanticNode sem = semantics(id);
    const spec::ActionDesc* a = sem.action(action);
    if (!a && !command) {
        Value::List names;
        for (const spec::ActionDesc& x : sem.actions) names.emplace_back(x.name);
        env.error(spec::error_code::not_found, std::format("'{}' has no action '{}'", where, action),
                  Value{}.set("actions", Value(std::move(names))))
            .result(spec::exit_code::failure);
        return stop(std::move(env));
    }
    effect = a ? a->effect : Effect::reversible;
    if (out) out->effect = effect;
    if (at(id).state.disabled && effect != Effect::read) {
        env.error(spec::error_code::refused, std::format("'{}' is disabled", where)).result(spec::exit_code::failure);
        return stop(std::move(env));
    }
    if (a && a->person && call.remote) return stop(person_only(std::format("{} on '{}'", action, where)));
    // Over the bus, an action is decided by the verb it invokes, or by its own name (S-11).
    const openas::Decision clear = clearance(a && !a->verb.empty() ? std::string_view(a->verb) : action, effect, a && a->person, call);
    if (clear == openas::Decision::refuse) return stop(above_cap(std::format("{} on '{}' ({})", action, where, spec::name(effect))));
    // Refused before it asks: no answer would let a transaction take it back.
    if (transaction && !replaying && spec::requires_answer(effect))
        return stop(not_in_transaction(std::format("{} on '{}'", action, where), effect));
    if (a && clear != openas::Decision::run && spec::requires_answer(effect) && call.answer != a->answer)
        return stop(needs_answer(std::format("{} on '{}' ({})", action, where, spec::name(effect)), a->answer));
    if (a && !a->args.empty())
        if (auto wrong = invalid_arguments(*a, call.args); !wrong.empty()) {
            Value::Map takes;
            for (const auto& [name, type] : a->args) takes.emplace_back(name, Value(type));
            env.error(spec::error_code::invalid_argument, std::format("{} on '{}': {}", action, where, wrong),
                      Value{}.set("args", Value(std::move(takes))))
                .result(spec::exit_code::failure);
            return stop(std::move(env));
        }
    const bool invokes_verb = action == "activate" && a && !a->verb.empty();
    if (call.dry_run && invokes_verb && !verbs.find(a->verb)) {
        env.error(spec::error_code::not_found, std::format("no verb `{}`", a->verb)).result(spec::exit_code::failure);
        return stop(std::move(env));
    }

    if (call.dry_run) {
        // What it would do: the component's preview where it has one (the
        // value after, the inverse it would give); a verb gives no inverse.
        Value more;
        if (spec::requires_answer(effect) || invokes_verb) more.set("undoable", false);
        if (effect != Effect::read && !invokes_verb && at(id).type->preview) {
            ActContext ctx{self, id, call.args};
            ActResult r = at(id).type->preview(at(id).instance, ctx, action);
            if (!r.ok) {
                Envelope failed;
                failed.error(spec::error_code::refused, r.error.empty() ? std::format("'{}' would refuse '{}'", where, action) : r.error)
                    .result(spec::exit_code::failure);
                return stop(std::move(failed));
            }
            if (effect == Effect::reversible) more.set("undoable", !r.undo.empty());
            if (!r.data.is_null()) more.set("value", Value{}.set("before", sem.value).set("after", std::move(r.data)));
        }
        return dry_run_answer("run", effect, std::move(more));
    }

    if (action == "focus") {
        set_focus(id, true);
        env.result(spec::exit_code::success);
        return env;
    }
    if (action == "blur") {
        if (focus == id) set_focus({}, false);
        env.result(spec::exit_code::success);
        return env;
    }
    if (action == "scroll_into_view") {
        // Each scroll ancestor, innermost first, moves the least that shows the node.
        Rect target = at(id).bounds;
        for (NodeId c = at(id).parent; alive(c); c = at(c).parent) {
            Node& sc = at(c);
            if (!sc.type->scroll_offset || !sc.type->act) continue;
            const Point off = sc.type->scroll_offset(sc.instance);
            const Rect view = sc.bounds;
            float x = off.x, y = off.y;
            if (target.bottom() > view.bottom()) y += target.bottom() - view.bottom();
            if (target.y < view.y + (y - off.y)) y -= view.y + (y - off.y) - target.y;
            if (target.right() > view.right()) x += target.right() - view.right();
            if (target.x < view.x + (x - off.x)) x -= view.x + (x - off.x) - target.x;
            if (x != off.x || y != off.y) {
                Value args;
                args.set("x", static_cast<double>(x)).set("y", static_cast<double>(y));
                ActContext ctx{self, c, args};
                (void)sc.type->act(sc.instance, ctx, "scroll_to");
                touch(dirty_paint);
            }
            target = target.offset({-(x - off.x), -(y - off.y)});
        }
        env.result(spec::exit_code::success);
        return env;
    }

    ActResult r = ActResult::done();
    if (at(id).type->act) {
        ActContext ctx{self, id, call.args};
        r = at(id).type->act(at(id).instance, ctx, action);
    }
    if (!r.ok) {
        env.error(spec::error_code::refused, r.error.empty() ? std::format("'{}' refused '{}'", where, action) : r.error)
            .result(spec::exit_code::failure);
        return env;
    }
    if (out) {
        out->ran = true;
        out->undo = std::move(r.undo);
    }
    if (action == "activate") {
        call_handlers(id, "press", call.args);
        call_handlers(id, "activate", call.args);
        if (a && !a->verb.empty()) {
            // A verb gives no inverse: the action as a whole cannot be taken back.
            if (out) out->undo.clear();
            // The node's own arguments (a row's delete button names its row)
            // win over the caller's: an action cannot be pointed elsewhere.
            Invocation with = call;
            if (auto own = at(id).props.find("verb_args"); own != at(id).props.end() && own->second.map()) {
                if (!with.args.map()) with.args = Value(Value::Map{});
                for (const auto& [k, v] : *own->second.map()) with.args.set(k, v);
            }
            Envelope e = verbs.invoke(a->verb, with);
            if (out) out->ran = e.exit_code() == spec::exit_code::success;
            return e;
        }
    }
    if (!r.data.is_null()) env.data(std::move(r.data));
    env.result(spec::exit_code::success);
    touch(dirty_semantics | dirty_paint);
    return env;
}

Envelope Runtime::act(NodeId node, std::string_view action, const Invocation& call) {
    Impl& d = *impl_;
    if (call.dry_run) return d.perform(node, action, call, false);  // no trace: no journal, no history
    std::string where = d.alive(node) ? path(node) : std::string{};
    const NodeId scope = d.scope_of(node);
    Impl::Outcome out;
    Envelope e = batch([&] { return d.perform(node, action, call, false, &out); });
    if (out.ran) d.record(scope, {node, where, {}, std::string(action), call.args, std::move(out.undo), std::format("{} on '{}'", action, where)}, out.effect);
    d.journal.push_back({std::move(where), std::string(action), call.actor.text(), e.exit_code().value_or(0)});
    return e;
}

Envelope Runtime::Impl::perform_item(NodeId holder, std::string_view key, std::string_view action, const Invocation& call, Outcome* out) {
    Envelope env;
    const std::string where = alive(holder) ? std::format("{}/{}", self.path(holder), key) : std::string(key);
    Effect effect = Effect::external;
    auto stop = [&](Envelope e) { return call.dry_run ? dry_run_of(e, effect) : e; };
    if (!alive(holder) || !at(holder).type->act_item || !at(holder).type->virtual_item) {
        env.error(spec::error_code::not_found, std::format("no node '{}'", where)).result(spec::exit_code::failure);
        return stop(std::move(env));
    }
    const Node& h = at(holder);
    const auto item = h.type->virtual_item(h.instance, self.path(holder), key);
    if (!item) {
        env.error(spec::error_code::not_found, std::format("no node '{}'", where)).result(spec::exit_code::failure);
        return stop(std::move(env));
    }
    const spec::ActionDesc* a = item->action(action);
    if (!a) {
        Value::List names;
        for (const spec::ActionDesc& x : item->actions) names.emplace_back(x.name);
        env.error(spec::error_code::not_found, std::format("'{}' has no action '{}'", where, action), Value{}.set("actions", Value(std::move(names))))
            .result(spec::exit_code::failure);
        return stop(std::move(env));
    }
    effect = a->effect;
    if (out) out->effect = effect;
    if (a->person && call.remote) return stop(person_only(std::format("{} on '{}'", action, where)));
    const openas::Decision clear = clearance(!a->verb.empty() ? std::string_view(a->verb) : action, a->effect, a->person, call);
    if (clear == openas::Decision::refuse) return stop(above_cap(std::format("{} on '{}' ({})", action, where, spec::name(a->effect))));
    if (transaction && !replaying && spec::requires_answer(effect))
        return stop(not_in_transaction(std::format("{} on '{}'", action, where), effect));
    if (clear != openas::Decision::run && spec::requires_answer(a->effect) && call.answer != a->answer)
        return stop(needs_answer(std::format("{} on '{}' ({})", action, where, spec::name(a->effect)), a->answer));
    if (!a->args.empty())
        if (auto wrong = invalid_arguments(*a, call.args); !wrong.empty()) {
            Value::Map takes;
            for (const auto& [name, type] : a->args) takes.emplace_back(name, Value(type));
            env.error(spec::error_code::invalid_argument, std::format("{} on '{}': {}", action, where, wrong), Value{}.set("args", Value(std::move(takes))))
                .result(spec::exit_code::failure);
            return stop(std::move(env));
        }
    if (call.dry_run) {
        Value more;
        if (spec::requires_answer(effect)) more.set("undoable", false);
        return dry_run_answer("run", effect, std::move(more));
    }
    ActResult r = batch([&] {
        ActContext ctx{self, holder, call.args};
        return at(holder).type->act_item(at(holder).instance, ctx, key, action);
    });
    if (!r.ok) {
        env.error(spec::error_code::refused, r.error.empty() ? std::format("'{}' refused '{}'", where, action) : r.error).result(spec::exit_code::failure);
        return env;
    }
    if (out) {
        out->ran = true;
        out->undo = std::move(r.undo);
    }
    if (!r.data.is_null()) env.data(std::move(r.data));
    env.result(spec::exit_code::success);
    touch(dirty_semantics | dirty_paint);
    return env;
}

Envelope Runtime::act_item(NodeId holder, std::string_view key, std::string_view action, const Invocation& call) {
    Impl& d = *impl_;
    if (call.dry_run) return d.perform_item(holder, key, action, call);
    const std::string path_of = d.alive(holder) ? path(holder) : std::string{};
    const std::string where = d.alive(holder) ? std::format("{}/{}", path_of, key) : std::string(key);
    const NodeId scope = d.scope_of(holder);
    Impl::Outcome out;
    Envelope e = d.perform_item(holder, key, action, call, &out);
    if (out.ran)
        d.record(scope, {holder, path_of, std::string(key), std::string(action), call.args, std::move(out.undo), std::format("{} on '{}'", action, where)},
                 out.effect);
    d.journal.push_back({where, std::string(action), call.actor.text(), e.exit_code().value_or(0)});
    return e;
}

std::optional<spec::SemanticNode> Runtime::virtual_item(NodeId holder, std::string_view key) const {
    const Impl& d = *impl_;
    if (!d.alive(holder) || !d.at(holder).type->virtual_item) return std::nullopt;
    return d.at(holder).type->virtual_item(d.at(holder).instance, path(holder), key);
}

bool Runtime::acts_on_items(NodeId holder) const {
    const Impl& d = *impl_;
    return d.alive(holder) && d.at(holder).type->act_item && d.at(holder).type->virtual_item;
}

std::span<const ActionEntry> Runtime::action_journal() const noexcept { return impl_->journal; }

std::vector<spec::ActionDesc> Runtime::actions(NodeId node) const {
    if (!valid(node)) return {};
    return impl_->semantics(node).actions;
}

spec::SemanticNode Runtime::semantic_node(NodeId node) const {
    if (!valid(node)) return {};
    impl_->rebuild_paths();
    return impl_->semantics(node);
}

std::uint64_t Runtime::semantic_revision() const noexcept { return impl_->semantic_revision; }

}  // namespace omui
