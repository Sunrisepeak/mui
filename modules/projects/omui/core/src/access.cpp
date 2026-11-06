module openmui.core;

import std;
import openas;
import mlibs.control;
import mlibs.trace;

namespace omui {
namespace {
constexpr Log policy_log{"agent"};
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

std::optional<openas::Cap> parse_cap(std::string_view text) noexcept { return mlibs::control::parse_cap(text); }
std::string cap_name(const openas::Cap& cap) { return mlibs::control::cap_name(cap); }
std::optional<AgentMode> parse_agent_mode(std::string_view text) noexcept { return mlibs::control::parse_mode(text); }
std::string_view agent_mode_name(AgentMode mode) noexcept { return mlibs::control::mode_name(mode); }
AgentMode agent_mode_for_profile(std::string_view profile) noexcept { return mlibs::control::mode_for_profile(profile); }
AgentPolicy agent_policy_for_profile(std::string_view profile, AgentMode requested) {
    return mlibs::control::policy_for_profile(profile, requested);
}
bool AgentPolicyState::allows(std::string_view operation) const {
    return mlibs::control::OperationPolicy{enabled, input, session_valid, layers}.allows(operation);
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

AgentSession::AgentSession(std::shared_ptr<const mlibs::control::Session> session) : session_(std::move(session)) {}
Actor AgentSession::attribute(const Actor& requested) const {
    if (!session_->created_for_development()) return requested;
    const auto kind = openas::parse_kind(requested.kind).value_or(openas::Kind::agent);
    const auto delegated = session_->attribute({kind, requested.name, requested.via});
    return {std::string(openas::name(delegated.kind)), delegated.name, delegated.via};
}
std::string AgentSession::owner() const { return session_->owner(); }
AgentAccess::AgentAccess() : AgentAccess(std::make_shared<mlibs::control::Access>(
    mlibs::control::Policy{.source = "legacy-global-or-unbound"})) {}
AgentAccess::AgentAccess(std::shared_ptr<mlibs::control::Access> access) : access_(std::move(access)) {
    if (!access_) access_ = std::make_shared<mlibs::control::Access>(mlibs::control::Policy{.source = "legacy-global-or-unbound"});
}
void AgentAccess::set_cap(openas::Cap cap) { access_->set_cap(cap); }
openas::Cap AgentAccess::cap() const { return access_->cap(); }
void AgentAccess::set_policy(AgentPolicy policy) { access_->set_policy(std::move(policy)); }
AgentPolicy AgentAccess::policy() const { return access_->policy(); }
AgentPolicyState AgentAccess::effective(const AgentSession* session) const {
    auto state = access_->effective(session ? session->control_session().get() : nullptr);
    return {.mode = state.mode, .cap = state.cap, .enabled = state.enabled, .input = state.input,
        .development_grant = state.development_grant, .session_valid = state.session_valid,
        .source = std::move(state.source), .layers = std::move(state.layers)};
}
std::shared_ptr<const AgentSession> AgentAccess::open_session(AgentRestrictions restrictions) {
    return std::shared_ptr<const AgentSession>(new AgentSession(access_->open_session(std::move(restrictions))));
}
bool AgentAccess::owns(const AgentSession& session) const { return access_->owns(*session.control_session()); }
bool AgentAccess::revoke_session(const AgentSession& session) { return access_->revoke_session(*session.control_session()); }
void AgentAccess::give(openas::Grant grant) { access_->give(std::move(grant)); }
std::size_t AgentAccess::revoke(const openas::Actor& actor) { return access_->revoke(actor); }
void AgentAccess::revoke_all() { access_->revoke_all(); }
std::vector<openas::Grant> AgentAccess::grants() const { return access_->grants(); }
openas::Decision AgentAccess::decide(std::string_view name, Effect effect, bool person, const Actor& actor,
    const AgentSession* session, std::string_view operation) const {
    // Conversion is separate from authority: the shared implementation binds
    // development delegation and decides against one locked policy snapshot.
    return access_->decide(name, openas::read_effect(spec::name(effect)), person, to_openas(actor),
        session ? session->control_session().get() : nullptr, operation);
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
    policy_log.debug("Agent request refused: {}", reason);
    e.error(spec::error_code::refused, reason, Value{}.set("policy", policy.to_value())).result(spec::exit_code::failure);
    return e;
}
openas::Actor to_openas(const Actor& actor) {
    return {openas::parse_kind(actor.kind).value_or(openas::Kind::program), actor.name, actor.via};
}
std::expected<mlibs::control::Invocation,ControlInvocationError> to_control_invocation(const Invocation& call) {
    mlibs::trace::Span span("control.convert","agent");
    if(call.remote && !call.access)return std::unexpected(ControlInvocationError::unbound_access);
    if(call.remote && !call.session)return std::unexpected(ControlInvocationError::missing_session);
    return mlibs::control::Invocation{.answer=call.answer,.actor=to_openas(call.actor),
        .remote=call.remote,.dry_run=call.dry_run,
        .access=call.access?call.access->control_binding():nullptr,
        .session=call.session?call.session->control_session():nullptr,.operation=call.operation};
}
} // namespace omui
