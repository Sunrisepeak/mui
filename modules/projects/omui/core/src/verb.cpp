module omui.core;

import std;
import openas;
import mlibs.control;

namespace omui {

const Invocation& bind_invocation(const Invocation& given, Invocation& copy,
    const std::shared_ptr<const AgentAccess>& access, const Invocation* parent, std::string_view operation) {
    return mlibs::control::bind_invocation(given, copy, access, parent, operation);
}

VerbDesc verb_of(const mlibs::control::CatalogEntry& entry, std::string answer) {
    VerbDesc desc{.name = std::string(entry.operation.name), .summary = std::string(entry.summary), .answer = std::move(answer),
                  .person = entry.operation.person, .undoable = entry.undoable};
    switch (entry.operation.effect) {
    case openas::Effect::read: desc.effect = Effect::read; break;
    case openas::Effect::reversible: desc.effect = Effect::reversible; break;
    case openas::Effect::irreversible: desc.effect = Effect::irreversible; break;
    default: desc.effect = Effect::external; break;
    }
    for (const auto& parameter : entry.parameters) {
        std::string type(parameter.type == "bool" ? "boolean" : parameter.type == "json" ? "object" : parameter.type);
        if (parameter.optional) type += '?';
        desc.args.emplace_back(std::string(parameter.name), std::move(type));
    }
    return desc;
}

bool valid_verb_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128) return false;
    bool word_start = true;
    for (char c : name) {
        if (c == '.') {
            if (word_start) return false;
            word_start = true;
            continue;
        }
        bool lower = c >= 'a' && c <= 'z';
        bool digit = c >= '0' && c <= '9';
        if (word_start && !lower) return false;
        if (!lower && !digit && c != '_') return false;
        word_start = false;
    }
    return !word_start;
}

std::expected<void, std::string> Verbs::add(VerbDesc desc, VerbFn fn) {
    if (!valid_verb_name(desc.name))
        return std::unexpected(std::format("`{}` is not a verb name: lower-case words joined by dots", desc.name));
    if (find(desc.name)) return std::unexpected(std::format("verb `{}` is already registered", desc.name));
    if (spec::requires_answer(desc.effect) && desc.answer.empty())
        return std::unexpected(std::format("verb `{}` is {} and states no answer (S-1)", desc.name, spec::name(desc.effect)));
    if (!fn) return std::unexpected(std::format("verb `{}` has no implementation", desc.name));
    verbs_.push_back({std::move(desc), std::move(fn)});
    return {};
}

bool Verbs::remove(std::string_view name) {
    if (std::erase_if(verbs_, [&](const Slot& slot) { return slot.desc.name == name; }) == 0) return false;
    // A task whose step is no longer there is not a task.
    std::erase_if(tasks_, [&](const TaskDesc& task) { return std::ranges::any_of(task.steps, [&](const TaskStep& step) { return step.verb == name; }); });
    return true;
}

namespace {

// Lower-case words joined by hyphens, as a catalog's tasks are named.
bool valid_task_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128) return false;
    bool word_start = true;
    for (const char letter : name) {
        if (letter == '-') {
            if (word_start) return false;
            word_start = true;
            continue;
        }
        if (!(letter >= 'a' && letter <= 'z') && !(letter >= '0' && letter <= '9')) return false;
        word_start = false;
    }
    return !word_start;
}

}  // namespace

std::expected<void, std::string> Verbs::add_task(TaskDesc task) {
    if (!valid_task_name(task.name))
        return std::unexpected(std::format("`{}` is not a task name: lower-case words joined by hyphens", task.name));
    if (std::ranges::contains(tasks_, task.name, &TaskDesc::name)) return std::unexpected(std::format("task `{}` is already registered", task.name));
    if (task.summary.empty()) return std::unexpected(std::format("task `{}` does not say what it gets done (summary)", task.name));
    if (task.steps.empty()) return std::unexpected(std::format("task `{}` has no step", task.name));
    for (std::size_t index = 0; index < task.steps.size(); ++index) {
        const TaskStep& step = task.steps[index];
        const bool of_node = !step.node.empty() || !step.action.empty();
        if (step.verb.empty() == !of_node)
            return std::unexpected(std::format("task `{}`, step {}: a step is a verb, or a node and one of its actions", task.name, index + 1));
        if (of_node) {
            if (step.node.empty() || step.action.empty())
                return std::unexpected(std::format("task `{}`, step {}: a node's action names both the node and the action", task.name, index + 1));
            continue;
        }
        const VerbDesc* verb = find(step.verb);
        if (!verb) return std::unexpected(std::format("task `{}`, step {}: verb `{}` is not registered (add the verb first)", task.name, index + 1, step.verb));
        if (verb->person) return std::unexpected(std::format("task `{}`, step {}: verb `{}` is the person's alone, never a request's", task.name, index + 1, step.verb));
        if (spec::requires_answer(verb->effect) && !step.dry_run)
            return std::unexpected(std::format("task `{}`, step {}: verb `{}` is {} and asks for an answer; a task to begin with runs as written — make the step a dry run",
                                               task.name, index + 1, step.verb, spec::name(verb->effect)));
    }
    tasks_.push_back(std::move(task));
    return {};
}

const VerbDesc* Verbs::find(std::string_view name) const noexcept {
    for (const Slot& s : verbs_)
        if (s.desc.name == name) return &s.desc;
    return nullptr;
}

std::vector<const VerbDesc*> Verbs::list() const {
    std::vector<const VerbDesc*> out;
    for (const Slot& s : verbs_) out.push_back(&s.desc);
    return out;
}

Envelope needs_answer(std::string_view what, std::string_view answer) {
    Envelope e;
    Value options;
    options.set("answer", Value::List{Value(answer)});
    options.set("hint", std::format("call again with \"answer\": \"{}\"", answer));
    e.error(spec::error_code::needs_answer, std::format("{} needs confirmation", what), std::move(options));
    e.result(spec::exit_code::needs_answer);
    return e;
}

Envelope person_only(std::string_view what) {
    Envelope e;
    e.error(spec::error_code::refused, std::format("{} is the person's own: only they do it, at the machine (S-10)", what));
    e.result(spec::exit_code::failure);
    return e;
}

Envelope dry_run_answer(std::string_view outcome, Effect effect, Value more) {
    Value d;
    d.set("dry_run", true).set("outcome", outcome).set("effect", spec::name(effect));
    if (const Value::Map* m = more.map())
        for (const auto& [k, v] : *m) d.set(k, v);
    Envelope e;
    e.data(std::move(d)).result(spec::exit_code::success);
    return e;
}

Envelope dry_run_of(const Envelope& stopped, Effect effect) {
    const int code = stopped.exit_code().value_or(spec::exit_code::failure);
    Value more;
    for (const spec::Record& r : stopped.records()) {
        if (r.kind != spec::RecordKind::error) continue;
        Value err;
        for (std::string_view k : {"code", "text"})
            if (const Value* v = r.body.get(k)) err.set(k, *v);
        more.set("error", std::move(err));
        if (const Value* a = r.body.get("answer"); a && a->list() && !a->list()->empty()) more.set("answer", a->list()->front());
        break;
    }
    // What it would do, where known before it is done: an irreversible or external action is not taken back.
    if (spec::requires_answer(effect)) more.set("undoable", false);
    const Value* code_text = more.get("error") ? more.get("error")->get("code") : nullptr;
    const std::string_view outcome = code == spec::exit_code::needs_answer                                   ? "needs-answer"
                                     : code_text && code_text->as_string() == spec::error_code::refused ? "refused"
                                                                                                         : "fails";
    return dry_run_answer(outcome, effect, std::move(more));
}

Envelope Verbs::invoke(std::string_view name, const Invocation& given) {
    Invocation copy;
    InvocationContext& context = context_ ? *context_ : own_context_;
    const Invocation& call = bind_invocation(given, copy, access_, context.current(), "invoke");
    auto record = [&](int code) {
        if (!call.dry_run) journal_.push_back({std::string(name), call.actor.text(), call.args, code});
    };
    auto it = std::ranges::find_if(verbs_, [&](const Slot& s) { return s.desc.name == name; });
    auto stop = [&](Envelope e, Effect effect) {
        record(e.exit_code().value_or(spec::exit_code::failure));
        return call.dry_run ? dry_run_of(e, effect) : e;
    };
    if (it == verbs_.end()) {
        Envelope e;
        e.error(spec::error_code::not_found, std::format("no verb `{}`", name)).result(spec::exit_code::failure);
        return stop(std::move(e), Effect::external);
    }
    const Effect effect = it->desc.effect;
    if (it->desc.person && call.remote) return stop(person_only(std::format("`{}`", name)), effect);
    const openas::Decision clear = clearance(name, effect, it->desc.person, call);
    if (clear == openas::Decision::refuse) return stop(above_cap(std::format("`{}` ({})", name, spec::name(effect)), call), effect);
    if (clear != openas::Decision::run && spec::requires_answer(effect) && call.answer != it->desc.answer)
        return stop(needs_answer(std::format("`{}` ({})", name, spec::name(effect)), it->desc.answer), effect);
    // A verb states no inverse: a reversible one is not in the history, so it
    // cannot be undone by the runtime (S-12) — unless its function records one.
    if (call.dry_run) return dry_run_answer("run", effect, Value{}.set("undoable", it->desc.undoable && effect == Effect::reversible));
    Envelope e;
    InvocationScope active(context, call);
    auto result = it->fn(call.args);
    if (result) {
        if (!result->is_null()) e.data(std::move(*result));
        e.result(spec::exit_code::success);
        record(spec::exit_code::success);
    } else {
        e.error(spec::error_code::failed, result.error()).result(spec::exit_code::failure);  // the work ran and did not succeed
        record(spec::exit_code::failure);
    }
    return e;
}

}  // namespace omui
