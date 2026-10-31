module openmui.core;

import std;
import openas;

namespace omui {

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
    return std::erase_if(verbs_, [&](const Slot& s) { return s.desc.name == name; }) > 0;
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

Envelope Verbs::invoke(std::string_view name, const Invocation& call) {
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
    if (clear == openas::Decision::refuse) return stop(above_cap(std::format("`{}` ({})", name, spec::name(effect))), effect);
    if (clear != openas::Decision::run && spec::requires_answer(effect) && call.answer != it->desc.answer)
        return stop(needs_answer(std::format("`{}` ({})", name, spec::name(effect)), it->desc.answer), effect);
    // A verb states no inverse: a reversible one is not in the history, so
    // it cannot be undone by the runtime (S-12).
    if (call.dry_run) return dry_run_answer("run", effect, Value{}.set("undoable", false));
    Envelope e;
    auto result = it->fn(call.args);
    if (result) {
        if (!result->is_null()) e.data(std::move(*result));
        e.result(spec::exit_code::success);
        record(spec::exit_code::success);
    } else {
        e.error(spec::error_code::internal, result.error()).result(spec::exit_code::failure);
        record(spec::exit_code::failure);
    }
    return e;
}

}  // namespace omui
