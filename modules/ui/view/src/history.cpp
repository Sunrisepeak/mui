// The history of what was done, and transactions (spec S-12, S-13).
//
// A reversible action whose implementation gives its inverse joins its
// scope's history; undo performs the inverses (as the one undoing would),
// and what they give back is what redo performs. An irreversible or external
// action is a barrier. A transaction gathers the actions performed until it
// ends into one step: committed, or taken back, last first.
module openmui.view;

import std;
import openmui.core;
import :impl;

namespace omui {

namespace {

// The code and message of an envelope's first error.
std::pair<std::string, std::string> first_error(const Envelope& e) {
    for (const spec::Record& r : e.records())
        if (r.kind == spec::RecordKind::error)
            return {std::string(r.body.get("code") ? r.body.get("code")->as_string() : spec::error_code::failed),
                    std::string(r.body.get("text") ? r.body.get("text")->as_string() : std::string_view{})};
    return {std::string(spec::error_code::failed), {}};
}

Envelope refused(std::string message, Value extra = {}) {
    Envelope e;
    e.error(spec::error_code::refused, std::move(message), std::move(extra)).result(spec::exit_code::failure);
    return e;
}

}  // namespace

Envelope not_in_transaction(std::string_view what, Effect effect) {
    return refused(std::format("{} is {}: a transaction holds only actions that can be taken back (S-13)", what, spec::name(effect)));
}

bool Runtime::Impl::is_scope(NodeId id) const {
    if (id == root) return true;
    if (!alive(id)) return false;
    const auto it = at(id).props.find("undo_scope");
    return it != at(id).props.end() && it->second.as_bool();
}

NodeId Runtime::Impl::scope_of(NodeId id) const {
    for (NodeId c = id; alive(c); c = at(c).parent)
        if (is_scope(c)) return c;
    return root;
}

const Runtime::Impl::ScopeHistory* Runtime::Impl::find_history(NodeId scope) const {
    for (const ScopeHistory& h : histories)
        if (h.scope == scope) return &h;
    return nullptr;
}

Runtime::Impl::ScopeHistory& Runtime::Impl::history_of(NodeId scope) {
    // A scope that is gone takes its history with it.
    std::erase_if(histories, [&](const ScopeHistory& h) { return !alive(h.scope); });
    for (ScopeHistory& h : histories)
        if (h.scope == scope) return h;
    histories.push_back({scope, {}, {}});
    return histories.back();
}

void Runtime::Impl::record(NodeId scope, UndoStep step, Effect effect) {
    if (replaying > 0 || effect == Effect::read) return;
    if (transaction) {
        Transaction& t = *transaction;
        if (t.steps.empty()) t.scope = scope;
        else if (t.scope != scope) t.mixed = true;
        if (step.inverse.empty() && t.unrevertible.empty()) t.unrevertible = step.label;
        t.steps.push_back(std::move(step));
        return;
    }
    ScopeHistory& h = history_of(scope);
    h.redo.clear();  // a new change: what was undone stays undone
    if (spec::requires_answer(effect)) {
        h.undo.push_back({std::format("{} ({})", step.label, spec::name(effect)), {}, true, step.path});
    } else if (!step.inverse.empty()) {
        std::string label = step.label;
        h.undo.push_back({std::move(label), {}, false});
        h.undo.back().steps.push_back(std::move(step));
    }
    if (h.undo.size() > history_limit) h.undo.erase(h.undo.begin());
    touch(dirty_semantics);
}

void Runtime::Impl::forget_history(std::string_view slot) {
    auto inside = [&](std::string_view path) { return path == slot || (path.starts_with(slot) && path.substr(slot.size()).starts_with('/')); };
    auto gone = [&](const UndoEntry& e) {
        return (e.barrier && inside(e.path)) || std::ranges::any_of(e.steps, [&](const UndoStep& s) { return inside(s.path); });
    };
    bool changed = false;
    for (ScopeHistory& h : histories) {
        changed |= std::erase_if(h.undo, gone) > 0;
        changed |= std::erase_if(h.redo, gone) > 0;
    }
    if (changed) touch(dirty_semantics);
}

std::expected<std::vector<Runtime::Impl::UndoStep>, Envelope> Runtime::Impl::take_back(const std::vector<UndoStep>& steps, const Invocation& call) {
    ++replaying;
    std::vector<UndoStep> back;  // in the order taken back
    std::optional<Envelope> failed;
    for (auto it = steps.rbegin(); it != steps.rend() && !failed; ++it) {
        const UndoStep& s = *it;
        const NodeId n = alive(s.node) ? s.node : self.find(s.path);
        // Each inverse's own inverse, performed in the reverse order, puts the step forward again.
        std::vector<std::vector<Inverse>> again;
        bool all = true;
        for (const Inverse& inv : s.inverse) {
            if (inv.run) {  // the program's own: run, and its own inverse is the step again
                inv.run();
                again.push_back({Inverse{{}, {}, inv.again, inv.run}});
                if (!inv.again) all = false;
                continue;
            }
            Invocation c = call;
            c.args = inv.args;
            c.dry_run = false;
            Outcome o;
            Envelope e = s.key.empty() ? perform(n, inv.action, c, false, &o) : perform_item(n, s.key, inv.action, c, &o);
            if (e.exit_code() != spec::exit_code::success) {
                auto [code, message] = first_error(e);
                Envelope f;
                f.error(code, std::format("taking back {} failed: {}", s.label, message), Value{}.set("step", s.label))
                    .result(e.exit_code().value_or(spec::exit_code::failure));
                failed = std::move(f);
                break;
            }
            // A read inverse (a selection put back) needs none of its own.
            all = all && (!o.undo.empty() || o.effect == Effect::read);
            again.push_back(std::move(o.undo));
        }
        if (failed) break;
        UndoStep forward{n, s.path, s.key, {}, {}, {}, s.label};
        if (s.inverse.size() == 1) {
            forward.action = s.inverse.front().action;
            forward.args = s.inverse.front().args;
        }
        if (all) {
            for (auto r = again.rbegin(); r != again.rend(); ++r)
                for (Inverse& x : *r) forward.inverse.push_back(std::move(x));
        } else if (!s.action.empty()) {
            forward.inverse.push_back({s.action, s.args});  // the step itself, done again
        }
        back.push_back(std::move(forward));
    }
    --replaying;
    if (failed) {
        // What was taken back is put forward again: the history stays as it was.
        if (!back.empty()) (void)take_back(back, call);
        return std::unexpected(std::move(*failed));
    }
    return back;
}

Envelope Runtime::Impl::history_action(NodeId scope, bool redo, const Invocation& call) {
    const std::string_view name = redo ? "redo" : "undo";
    const std::string where = scope == root ? std::string("app") : self.path(scope);
    auto stop = [&](Envelope e) { return call.dry_run ? dry_run_of(e, Effect::reversible) : e; };
    if (clearance(name, Effect::reversible, false, call) == openas::Decision::refuse)
        return stop(above_cap(std::format("{} on '{}' (reversible)", name, where)));
    if (transaction && replaying == 0) return stop(refused(std::format("{} inside a transaction: commit or roll it back first (S-13)", name)));
    const ScopeHistory* h = find_history(scope);
    const std::vector<UndoEntry>* from = h ? (redo ? &h->redo : &h->undo) : nullptr;
    if (!from || from->empty()) return stop(refused(std::format("nothing to {} in '{}'", name, where)));
    if (from->back().barrier)
        return stop(refused(std::format("{} cannot be taken back: nothing done before it can be undone (S-12)", from->back().label),
                            Value{}.set("barrier", from->back().label)));
    if (call.dry_run) return dry_run_answer("run", Effect::reversible, Value{}.set(name, from->back().label).set("undoable", true));

    ScopeHistory& hist = history_of(scope);
    std::vector<UndoEntry>& source = redo ? hist.redo : hist.undo;
    UndoEntry entry = std::move(source.back());
    source.pop_back();
    auto back = batch([&] { return take_back(entry.steps, call); });
    ScopeHistory& after = history_of(scope);  // taking back may have added a scope
    if (!back) {
        (redo ? after.redo : after.undo).push_back(std::move(entry));
        touch(dirty_semantics);
        return back.error();
    }
    const std::size_t steps = back->size();
    (redo ? after.undo : after.redo).push_back({entry.label, std::move(*back), false});
    touch(dirty_semantics | dirty_paint);
    Envelope e;
    e.data(Value{}.set(redo ? "redone" : "undone", entry.label).set("steps", static_cast<std::int64_t>(steps)))
        .result(spec::exit_code::success);
    return e;
}

// ── the runtime's own ──

NodeId Runtime::undo_scope(NodeId node) const { return impl_->scope_of(node); }

History Runtime::history(NodeId scope) const {
    const Impl& d = *impl_;
    if (!d.alive(scope)) scope = d.root;
    History out;
    out.scope = scope == d.root ? std::string("app") : path(scope);
    if (const Impl::ScopeHistory* h = d.find_history(scope)) {
        for (const Impl::UndoEntry& e : h->undo) out.undo.push_back({e.label, e.steps.empty() ? 1 : e.steps.size(), e.barrier});
        for (const Impl::UndoEntry& e : h->redo) out.redo.push_back({e.label, e.steps.size(), false});
    }
    return out;
}

void Runtime::clear_history(NodeId scope) {
    Impl& d = *impl_;
    if (!d.alive(scope)) scope = d.root;
    std::erase_if(d.histories, [&](const Impl::ScopeHistory& h) { return h.scope == scope; });
    d.touch(dirty_semantics);
}

Envelope Runtime::undo(NodeId scope, const Invocation& call) { return act(valid(scope) ? scope : root(), "undo", call); }
Envelope Runtime::redo(NodeId scope, const Invocation& call) { return act(valid(scope) ? scope : root(), "redo", call); }

Envelope Runtime::invoke(std::string_view verb, const Invocation& given) {
    Impl& d = *impl_;
    Invocation copy;
    const Invocation& call = d.attributed(given, copy);
    if (call.dry_run) return d.verbs.invoke(verb, call);
    const VerbDesc* v = d.verbs.find(verb);
    if (v && d.transaction && d.replaying == 0 && spec::requires_answer(v->effect))
        return not_in_transaction(std::format("`{}`", verb), v->effect);
    const Effect effect = v ? v->effect : Effect::read;
    std::vector<Inverse> outer = std::exchange(d.program_inverse, {});
    ++d.acting;
    d.actors.push_back(call.actor);
    Envelope e = batch([&] { return d.verbs.invoke(verb, call); });
    d.actors.pop_back();
    --d.acting;
    std::vector<Inverse> inverse = effect == Effect::reversible ? std::move(d.program_inverse) : std::vector<Inverse>{};
    d.program_inverse = std::move(outer);
    // A verb gives no inverse of its own, unless its function said how
    // (taken_back_by): a reversible one without is not recorded (but clears
    // what could be redone); an irreversible or external one is a barrier.
    if (v && e.exit_code() == spec::exit_code::success)
        d.record(d.root, {d.root, "app", {}, std::string(verb), call.args, std::move(inverse), std::format("`{}`", verb)}, effect);
    return e;
}

std::optional<Actor> Runtime::acting() const {
    const Impl& d = *impl_;
    if (d.actors.empty()) return std::nullopt;
    return d.actors.back();
}

void Runtime::taken_back_by(std::function<void()> undo, std::function<void()> redo) {
    Impl& d = *impl_;
    if (d.acting == 0 || d.replaying > 0 || !undo) return;
    d.program_inverse.push_back({{}, {}, std::move(undo), std::move(redo)});
}

void Runtime::begin_transaction(std::string label) {
    Impl& d = *impl_;
    if (d.transaction) {
        ++d.transaction->depth;
        return;
    }
    d.transaction.emplace();
    d.transaction->label = std::move(label);
}

bool Runtime::in_transaction() const noexcept { return impl_->transaction.has_value(); }

Envelope Runtime::commit_transaction() {
    Impl& d = *impl_;
    if (!d.transaction) return refused("no transaction is open");
    if (--d.transaction->depth > 0) {
        Envelope e;
        e.data(Value{}.set("nested", true)).result(spec::exit_code::success);
        return e;
    }
    Impl::Transaction t = std::move(*d.transaction);
    d.transaction.reset();
    const std::size_t steps = t.steps.size();
    const bool undoable = t.unrevertible.empty();
    if (t.label.empty()) t.label = steps == 1 ? t.steps.front().label : std::format("{} actions", steps);
    if (!t.steps.empty()) {
        Impl::ScopeHistory& h = d.history_of(t.mixed ? d.root : t.scope);
        h.redo.clear();
        // One step of history; one that gave no inverse leaves the whole out of it.
        if (undoable) h.undo.push_back({t.label, std::move(t.steps), false});
        if (h.undo.size() > history_limit) h.undo.erase(h.undo.begin());
        d.touch(dirty_semantics);
    }
    Value data;
    data.set("committed", t.label).set("steps", static_cast<std::int64_t>(steps)).set("undoable", undoable);
    if (!undoable) data.set("not_undoable", t.unrevertible);
    Envelope e;
    e.data(std::move(data)).result(spec::exit_code::success);
    return e;
}

Envelope Runtime::rollback_transaction(const Invocation& call) {
    Impl& d = *impl_;
    if (!d.transaction) return refused("no transaction is open");
    // A rollback anywhere ends the outermost transaction: all or nothing.
    Impl::Transaction t = std::move(*d.transaction);
    d.transaction.reset();
    std::vector<Impl::UndoStep> revertible;
    Value::List kept;
    for (Impl::UndoStep& s : t.steps) {
        if (s.inverse.empty()) kept.emplace_back(s.label);
        else revertible.push_back(std::move(s));
    }
    auto back = batch([&] { return d.take_back(revertible, call); });
    if (!t.steps.empty())  // a rollback with nothing done is no action
        d.journal.push_back({t.scope == d.root || !d.alive(t.scope) ? std::string("app") : path(t.scope), "rollback", call.actor.text(),
                         back ? spec::exit_code::success : back.error().exit_code().value_or(spec::exit_code::failure)});
    d.touch(dirty_semantics | dirty_paint);
    if (!back) return back.error();
    Value data;
    data.set("rolled_back", static_cast<std::int64_t>(revertible.size()));
    // A step that gave no inverse stays done; the caller learns which.
    if (!kept.empty()) data.set("kept", Value(std::move(kept)));
    Envelope e;
    e.data(std::move(data)).result(spec::exit_code::success);
    return e;
}

}  // namespace omui
