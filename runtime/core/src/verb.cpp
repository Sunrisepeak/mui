module openmui.core;

import std;

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

Envelope Verbs::invoke(std::string_view name, const Invocation& call) {
    auto record = [&](int code) {
        journal_.push_back({std::string(name), call.actor.text(), call.args, code});
    };
    auto it = std::ranges::find_if(verbs_, [&](const Slot& s) { return s.desc.name == name; });
    if (it == verbs_.end()) {
        Envelope e;
        e.error(spec::error_code::not_found, std::format("no verb `{}`", name)).result(spec::exit_code::failure);
        record(spec::exit_code::failure);
        return e;
    }
    if (it->desc.person && call.remote) {
        record(spec::exit_code::failure);
        return person_only(std::format("`{}`", name));
    }
    if (spec::requires_answer(it->desc.effect) && call.answer != it->desc.answer) {
        record(spec::exit_code::needs_answer);
        return needs_answer(std::format("`{}` ({})", name, spec::name(it->desc.effect)), it->desc.answer);
    }
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
