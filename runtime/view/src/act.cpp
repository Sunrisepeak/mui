// Actions and semantics: what a node says about itself (contract ①) and
// performing its actions under the rules of spec §3.3 (S-1 effects and
// answers, S-4 removal, disabled nodes).
module openmui.view;

import std;
import openmui.core;
import :impl;

namespace omui {

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
    const bool placed = options.stages >= Stages::layout;
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
        for (const spec::ActionSpec& a : c->actions) add(a.name, a.effect, a.args);
    else if (has_handler(id, "press") || has_handler(id, "activate") || prop("verb"))
        add("activate", Effect::reversible);
    if (const Value* v = prop("verb"); v && !v->as_string().empty()) {
        spec::ActionDesc& a = add("activate", Effect::reversible);
        a.verb = std::string(v->as_string());
        if (const VerbDesc* d = verbs.find(a.verb)) {
            a.effect = std::max(a.effect, d->effect);
            a.answer = d->answer;
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
    // A generic node that can be activated is a button to every reader
    // (what ARIA asks of a clickable div), so it is found as one and must be named (S-6).
    if (s.role == Role::generic && activate() && !prop("role")) s.role = Role::button;
    if (focusable) {
        add("focus", Effect::read);
        add("blur", Effect::read);
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

Envelope Runtime::Impl::perform(NodeId id, std::string_view action, const Invocation& call, bool command) {
    Envelope env;
    if (!alive(id)) {
        env.error(spec::error_code::not_found, "no such node (it was removed)").result(spec::exit_code::failure);
        return env;
    }
    const std::string where = self.path(id);
    if (at(id).state.removing) {
        env.error(spec::error_code::refused, std::format("'{}' is being removed (spec S-4)", where)).result(spec::exit_code::failure);
        return env;
    }
    const spec::SemanticNode sem = semantics(id);
    const spec::ActionDesc* a = sem.action(action);
    if (!a && !command) {
        Value::List names;
        for (const spec::ActionDesc& x : sem.actions) names.emplace_back(x.name);
        env.error(spec::error_code::not_found, std::format("'{}' has no action '{}'", where, action),
                  Value{}.set("actions", Value(std::move(names))))
            .result(spec::exit_code::failure);
        return env;
    }
    const Effect effect = a ? a->effect : Effect::reversible;
    if (at(id).state.disabled && effect != Effect::read) {
        env.error(spec::error_code::refused, std::format("'{}' is disabled", where)).result(spec::exit_code::failure);
        return env;
    }
    if (a && spec::requires_answer(effect) && call.answer != a->answer)
        return needs_answer(std::format("{} on '{}' ({})", action, where, spec::name(effect)), a->answer);

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
    if (action == "activate") {
        call_handlers(id, "press", call.args);
        call_handlers(id, "activate", call.args);
        if (a && !a->verb.empty()) {
            Envelope v = verbs.invoke(a->verb, call);
            return v;
        }
    }
    if (!r.data.is_null()) env.data(std::move(r.data));
    env.result(spec::exit_code::success);
    touch(dirty_semantics | dirty_paint);
    return env;
}

Envelope Runtime::act(NodeId node, std::string_view action, const Invocation& call) {
    Impl& d = *impl_;
    std::string where = d.alive(node) ? path(node) : std::string{};
    Envelope e = batch([&] { return d.perform(node, action, call, false); });
    d.journal.push_back({std::move(where), std::string(action), call.actor.text(), e.exit_code().value_or(0)});
    return e;
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
