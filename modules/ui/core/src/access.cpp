module openmui.core;

import std;
import openas;

namespace omui {

std::optional<openas::Cap> parse_cap(std::string_view text) noexcept {
    if (text == "none") return openas::Cap::none();
    if (auto e = openas::parse_effect(text)) return openas::Cap{*e};
    return std::nullopt;
}

std::string cap_name(const openas::Cap& cap) { return cap.effect ? std::string(openas::name(*cap.effect)) : std::string("none"); }

void AgentAccess::set_cap(openas::Cap cap) {
    std::scoped_lock lock(mutex_);
    cap_ = cap;
}

openas::Cap AgentAccess::cap() const {
    std::scoped_lock lock(mutex_);
    return cap_;
}

void AgentAccess::give(openas::Grant grant) {
    std::scoped_lock lock(mutex_);
    grants_.push_back(std::move(grant));
}

std::size_t AgentAccess::revoke(const openas::Actor& actor) {
    std::scoped_lock lock(mutex_);
    return std::erase_if(grants_, [&](const openas::Grant& g) { return g.actor == actor; });
}

void AgentAccess::revoke_all() {
    std::scoped_lock lock(mutex_);
    grants_.clear();
}

std::vector<openas::Grant> AgentAccess::grants() const {
    std::scoped_lock lock(mutex_);
    return grants_;
}

openas::Decision AgentAccess::decide(std::string_view name, Effect effect, bool person, const Actor& actor) const {
    const openas::Verb verb{.name = std::string(name), .effect = openas::read_effect(spec::name(effect)), .person = person};
    std::scoped_lock lock(mutex_);
    // Over the bus the person is never present (openas §2.7), whatever it says.
    return openas::decide(verb, to_openas(actor), false, cap_, grants_);
}

AgentAccess& agent_access() {
    static AgentAccess access;
    return access;
}

openas::Decision clearance(std::string_view name, Effect effect, bool person, const Invocation& call) {
    if (!call.remote) return openas::Decision::ask;
    return agent_access().decide(name, effect, person, call.actor);
}

Envelope above_cap(std::string_view what) {
    Envelope e;
    e.error(spec::error_code::refused,
            std::format("{} is beyond what this application lets agents do (it allows up to {})", what, cap_name(agent_access().cap())))
        .result(spec::exit_code::failure);
    return e;
}

openas::Actor to_openas(const Actor& actor) {
    return {openas::parse_kind(actor.kind).value_or(openas::Kind::program), actor.name, actor.via};
}

}  // namespace omui
