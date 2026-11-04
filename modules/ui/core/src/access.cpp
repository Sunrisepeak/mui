module openmui.core;

import std;
import openas;

namespace omui {
namespace {
bool covers(std::string_view pattern, std::string_view name) {
    return pattern == "*" || pattern == name || (pattern.ends_with(".*") && name.starts_with(pattern.substr(0, pattern.size() - 1)));
}
openas::Cap intersect(openas::Cap a, openas::Cap b) {
    return a.effect && b.effect ? openas::Cap{std::min(*a.effect, *b.effect)} : openas::Cap::none();
}
void restrict(AgentPolicyState& state, const AgentRestrictions& layer) {
    if (layer.cap) state.cap = intersect(state.cap, *layer.cap);
    if (layer.enabled) state.enabled &= *layer.enabled;
    if (layer.input) state.input &= *layer.input;
    if (layer.mode == AgentMode::dual_user) state.mode = AgentMode::dual_user;
}
Value restrictions_value(const AgentRestrictions& layer) {
    Value out;
    if (layer.cap) out.set("cap", cap_name(*layer.cap));
    if (layer.enabled) out.set("enabled", *layer.enabled);
    if (layer.input) out.set("input", *layer.input);
    if (layer.mode) out.set("mode", agent_mode_name(*layer.mode));
    if (layer.operations) {
        Value::List names;
        for (const auto& name : *layer.operations)
            names.emplace_back(name);
        out.set("operations", Value(std::move(names)));
    }
    return out;
}
} // namespace
std::optional<openas::Cap> parse_cap(std::string_view text) noexcept {
    if (text == "none") return openas::Cap::none();
    if (auto e = openas::parse_effect(text)) return openas::Cap{*e};
    return std::nullopt;
}
std::string cap_name(const openas::Cap& cap) { return cap.effect ? std::string(openas::name(*cap.effect)) : std::string("none"); }
std::optional<AgentMode> parse_agent_mode(std::string_view text) noexcept {
    if (text == "automatic" || text == "auto") return AgentMode::automatic;
    if (text == "development" || text == "dev") return AgentMode::development;
    if (text == "dual-user" || text == "dual_user") return AgentMode::dual_user;
    return std::nullopt;
}
std::string_view agent_mode_name(AgentMode mode) noexcept {
    switch (mode) {
    case AgentMode::automatic:
        return "automatic";
    case AgentMode::development:
        return "development";
    case AgentMode::dual_user:
        return "dual-user";
    }
    return "dual-user";
}
AgentMode agent_mode_for_profile(std::string_view profile) noexcept {
    return profile == "dev" ? AgentMode::development : AgentMode::dual_user;
}
bool AgentPolicyState::allows(std::string_view operation) const {
    if (!enabled || !session_valid || (operation == "input" && !input)) return false;
    for (const auto& layer : layers)
        if (layer.operations &&
            !std::ranges::any_of(*layer.operations, [&](const std::string& pattern) { return covers(pattern, operation); }))
            return false;
    return true;
}
Value AgentPolicyState::to_value() const {
    Value out;
    out.set("mode", agent_mode_name(mode))
        .set("source", source)
        .set("cap", cap_name(cap))
        .set("enabled", enabled)
        .set("input", input)
        .set("session_valid", session_valid)
        .set("development_grant", development_grant);
    Value hierarchy;
    hierarchy.set("developer", restrictions_value(layers[0]))
        .set("user", restrictions_value(layers[1]))
        .set("session", restrictions_value(layers[2]));
    return out.set("layers", std::move(hierarchy));
}
AgentSession::AgentSession(std::weak_ptr<const void> issuer, std::uint64_t epoch, bool development, openas::Actor owner,
                           AgentRestrictions restrictions)
    : issuer_(std::move(issuer)), epoch_(epoch), development_(development), owner_(std::move(owner)),
      restrictions_(std::move(restrictions)) {}
Actor AgentSession::attribute(const Actor& requested) const {
    if (!development_) return requested;
    // Client provenance is attribution, not proof of delegation. Only the
    // host-created owner is attached; a reported person never becomes present.
    const auto kind = openas::parse_kind(requested.kind).value_or(openas::Kind::agent);
    const auto delegated = openas::delegate(owner_, kind == openas::Kind::person ? openas::Kind::agent : kind, requested.name);
    return {std::string(openas::name(delegated.kind)), delegated.name, delegated.via};
}
std::string AgentSession::owner() const { return openas::format(owner_); }
AgentAccess::AgentAccess() : identity_(std::make_shared<const std::byte>(std::byte{})) { policy_.source = "legacy-global-or-unbound"; }
void AgentAccess::set_cap(openas::Cap cap) {
    std::scoped_lock lock(mutex_);
    policy_.cap = cap;
}
openas::Cap AgentAccess::cap() const { return effective().cap; }
void AgentAccess::set_policy(AgentPolicy policy) {
    std::scoped_lock lock(mutex_);
    if (policy.mode == AgentMode::automatic) policy.mode = AgentMode::dual_user;
    policy_ = std::move(policy);
}
AgentPolicy AgentAccess::policy() const {
    std::scoped_lock lock(mutex_);
    return policy_;
}
bool AgentAccess::owns(const AgentSession& session) const { return session.issuer_.lock().get() == identity_.get(); }
AgentPolicyState AgentAccess::effective_locked(const AgentSession* session) const {
    AgentPolicyState out;
    out.mode = policy_.mode;
    out.cap = policy_.cap;
    out.enabled = policy_.enabled;
    out.input = policy_.input;
    out.source = policy_.source;
    out.layers[0] = policy_.developer;
    out.layers[1] = policy_.user;
    if (session) {
        out.session_valid = owns(*session) && session->active_.load();
        if (out.session_valid) out.layers[2] = session->restrictions_;
    }
    for (const auto& layer : out.layers)
        restrict(out, layer);
    out.enabled &= out.session_valid;
    out.input &= out.enabled;
    if (!out.enabled) out.cap = openas::Cap::none();
    out.development_grant =
        session && out.session_valid && out.mode == AgentMode::development && session->development_ && session->epoch_ == grant_epoch_;
    return out;
}
AgentPolicyState AgentAccess::effective(const AgentSession* session) const {
    std::scoped_lock lock(mutex_);
    return effective_locked(session);
}
std::shared_ptr<const AgentSession> AgentAccess::open_session(AgentRestrictions restrictions) {
    std::scoped_lock lock(mutex_);
    auto state = effective_locked(nullptr);
    restrict(state, restrictions);
    const bool development = state.enabled && state.mode == AgentMode::development;
    const auto name = std::format("{}-{}", development ? "development-session" : "agent-session", ++session_next_);
    return std::shared_ptr<const AgentSession>(
        new AgentSession(identity_, grant_epoch_, development, {openas::Kind::service, name, {}}, std::move(restrictions)));
}
bool AgentAccess::revoke_session(const AgentSession& session) {
    if (!owns(session)) return false;
    return session.active_.exchange(false);
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
    ++grant_epoch_;
}
std::vector<openas::Grant> AgentAccess::grants() const {
    std::scoped_lock lock(mutex_);
    return grants_;
}
openas::Decision AgentAccess::decide(std::string_view name, Effect effect, bool person, const Actor& actor, const AgentSession* session,
                                     std::string_view operation) const {
    const openas::Verb verb{.name = std::string(name), .effect = openas::read_effect(spec::name(effect)), .person = person};
    std::scoped_lock lock(mutex_);
    const auto policy = effective_locked(session);
    const auto cap = operation.empty() || policy.allows(operation) ? policy.cap : openas::Cap::none();
    auto grants = grants_;
    if (policy.development_grant) grants.push_back({session->owner_, openas::Effect::external, {"*"}});
    // No remote actor is observed present. A session is an authenticated
    // delegation context, never an exemption from openas or S-10.
    const Actor bound = policy.development_grant ? session->attribute(actor) : actor;
    return openas::decide(verb, to_openas(bound), false, cap, grants);
}
AgentAccess& agent_access() {
    static AgentAccess access;
    return access;
}
openas::Decision clearance(std::string_view name, Effect effect, bool person, const Invocation& call) {
    if (!call.remote) return openas::Decision::ask;
    return (call.access ? *call.access : agent_access()).decide(name, effect, person, call.actor, call.session.get(), call.operation);
}
Envelope above_cap(std::string_view what, const Invocation& call) {
    const auto policy = (call.access ? *call.access : agent_access()).effective(call.session.get());
    Envelope e;
    const auto reason = !policy.session_valid ? std::format("{} uses a session that is not valid for this application", what)
                        : !call.operation.empty() && !policy.allows(call.operation)
                            ? std::format("{} is disabled by the effective Agent policy for `{}`", what, call.operation)
                            : std::format("{} is beyond what this application lets agents do (it "
                                          "allows up to {})",
                                          what, cap_name(policy.cap));
    e.error(spec::error_code::refused, reason, Value{}.set("policy", policy.to_value())).result(spec::exit_code::failure);
    return e;
}
openas::Actor to_openas(const Actor& actor) {
    return {openas::parse_kind(actor.kind).value_or(openas::Kind::program), actor.name, actor.via};
}
} // namespace omui
