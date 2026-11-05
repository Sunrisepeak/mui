module mlibs.control;

import std;
import openas;
import openmui.log;
import openmui.trace;

namespace mlibs::control {
namespace {
constexpr omui::Log policy_log{"agent"};
bool covers(std::string_view pattern, std::string_view name) {
    return pattern == "*" || pattern == name || (pattern.ends_with(".*") && name.starts_with(pattern.substr(0, pattern.size() - 1)));
}
openas::Cap intersect(openas::Cap a, openas::Cap b) {
    return a.effect && b.effect ? openas::Cap{std::min(*a.effect, *b.effect)} : openas::Cap::none();
}
void restrict(PolicyState& state, const Restrictions& layer) {
    if (layer.cap) state.cap = intersect(state.cap, *layer.cap);
    if (layer.enabled) state.enabled &= *layer.enabled;
    if (layer.input) state.input &= *layer.input;
    if (layer.mode == Mode::dual_user) state.mode = Mode::dual_user;
}
} // namespace
std::optional<openas::Cap> parse_cap(std::string_view text) noexcept {
    if (text == "none") return openas::Cap::none();
    if (auto e = openas::parse_effect(text)) return openas::Cap{*e};
    return std::nullopt;
}
std::string cap_name(const openas::Cap& cap) { return cap.effect ? std::string(openas::name(*cap.effect)) : std::string("none"); }
std::optional<Mode> parse_mode(std::string_view text) noexcept {
    if (text == "automatic" || text == "auto") return Mode::automatic;
    if (text == "development" || text == "dev") return Mode::development;
    if (text == "dual-user" || text == "dual_user") return Mode::dual_user;
    return std::nullopt;
}
std::string_view mode_name(Mode mode) noexcept {
    switch (mode) {
    case Mode::automatic:
        return "automatic";
    case Mode::development:
        return "development";
    case Mode::dual_user:
        return "dual-user";
    }
    return "dual-user";
}
Mode mode_for_profile(std::string_view profile) noexcept {
    return profile == "dev" ? Mode::development : Mode::dual_user;
}
Policy policy_for_profile(std::string_view profile, Mode requested) {
    Policy policy;
    policy.mode = requested == Mode::automatic ? mode_for_profile(profile) : requested;
    policy.source = requested == Mode::automatic ? std::format("profile:{}", profile) :
        std::format("explicit-mode:{} (profile:{})", mode_name(requested), profile);
    return policy;
}
bool OperationPolicy::allows(std::string_view operation) const {
    if (!enabled || !session_valid || (operation == "input" && !input)) return false;
    for (const auto& layer : layers)
        if (layer.operations &&
            !std::ranges::any_of(*layer.operations, [&](const std::string& pattern) { return covers(pattern, operation); }))
            return false;
    return true;
}
bool PolicyState::allows(std::string_view operation) const {
    return OperationPolicy{enabled, input, session_valid, layers}.allows(operation);
}
Session::Session(std::weak_ptr<const void> issuer, std::uint64_t epoch, bool development, openas::Actor owner,
                           Restrictions restrictions)
    : issuer_(std::move(issuer)), epoch_(epoch), development_(development), owner_(std::move(owner)),
      restrictions_(std::move(restrictions)) {}
openas::Actor Session::attribute(const openas::Actor& requested) const {
    if (!development_) return requested;
    // Claimed provenance cannot mint a host delegation or a present person.
    return openas::delegate(owner_, requested.kind == openas::Kind::person ? openas::Kind::agent : requested.kind, requested.name);
}
std::string Session::owner() const { return openas::format(owner_); }
Access::Access(Policy policy) : policy_(std::move(policy)), identity_(std::make_shared<const std::byte>(std::byte{})) {
    if (policy_.mode == Mode::automatic) policy_.mode = Mode::dual_user;
}
void Access::set_cap(openas::Cap cap) {
    {
        std::scoped_lock lock(mutex_);
        policy_.cap = cap;
    }
    policy_log.debug("Agent cap changed to {}", cap_name(cap));
}
openas::Cap Access::cap() const { return effective().cap; }
void Access::set_policy(Policy policy) {
    omui::trace::Span span("control.policy", "control");
    if (policy.mode == Mode::automatic) policy.mode = Mode::dual_user;
    const auto mode = policy.mode;
    const auto cap = cap_name(policy.cap);
    const auto source = policy.source;
    {
        std::scoped_lock lock(mutex_);
        policy_ = std::move(policy);
    }
    // Logging can call an application sink which reads this policy again.
    policy_log.info("Agent policy: mode={}, cap={}, source={}", mode_name(mode), cap, source);
}
Policy Access::policy() const {
    std::scoped_lock lock(mutex_);
    return policy_;
}
bool Access::owns(const Session& session) const { return session.issuer_.lock().get() == identity_.get(); }
PolicyState Access::effective_locked(const Session* session) const {
    PolicyState out;
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
        session && out.session_valid && out.mode == Mode::development && session->development_ && session->epoch_ == grant_epoch_;
    return out;
}
PolicyState Access::effective(const Session* session) const {
    std::scoped_lock lock(mutex_);
    return effective_locked(session);
}
std::shared_ptr<const Session> Access::open_session(Restrictions restrictions) {
    omui::trace::Span span("control.open_session", "control");
    std::shared_ptr<const Session> session;
    bool development = false;
    std::string name;
    {
        std::scoped_lock lock(mutex_);
        auto state = effective_locked(nullptr);
        restrict(state, restrictions);
        development = state.enabled && state.mode == Mode::development;
        name = std::format("{}-{}", development ? "development-session" : "agent-session", ++session_next_);
        session = std::shared_ptr<const Session>(
            new Session(identity_, grant_epoch_, development, {openas::Kind::service, name, {}}, std::move(restrictions)));
    }
    policy_log.debug("Agent session opened: owner={}, development={}", name, development);
    return session;
}
bool Access::revoke_session(const Session& session) {
    omui::trace::Span span("control.revoke_session", "control");
    if (!owns(session)) return false;
    const bool revoked = session.active_.exchange(false);
    if (revoked) policy_log.debug("Agent session revoked: {}", session.owner());
    return revoked;
}
void Access::give(openas::Grant grant) {
    const auto actor = openas::format(grant.actor);
    {
        std::scoped_lock lock(mutex_);
        grants_.push_back(std::move(grant));
    }
    policy_log.debug("Agent grant added for {}", actor);
}
std::size_t Access::revoke(const openas::Actor& actor) {
    std::size_t removed;
    {
        std::scoped_lock lock(mutex_);
        removed = std::erase_if(grants_, [&](const openas::Grant& g) { return g.actor == actor; });
    }
    if (removed) policy_log.debug("Agent grants revoked for {}: {}", openas::format(actor), removed);
    return removed;
}
void Access::revoke_all() {
    std::uint64_t epoch;
    {
        std::scoped_lock lock(mutex_);
        grants_.clear();
        epoch = ++grant_epoch_;
    }
    policy_log.debug("Agent grants revoked, development epoch={}", epoch);
}
std::vector<openas::Grant> Access::grants() const {
    std::scoped_lock lock(mutex_);
    return grants_;
}
openas::Decision Access::decide(std::string_view name, openas::Effect effect, bool person, const openas::Actor& actor, const Session* session,
                                     std::string_view operation) const {
    omui::trace::Span span("control.decide", "control");
    const openas::Verb verb{.name = std::string(name), .effect = effect, .person = person};
    std::scoped_lock lock(mutex_);
    const auto policy = effective_locked(session);
    const auto cap = policy.allows(operation.empty() ? name : operation) ? policy.cap : openas::Cap::none();
    auto grants = grants_;
    if (policy.development_grant) grants.push_back({session->owner_, openas::Effect::external, {"*"}});
    // No remote actor is observed present. A session is an authenticated
    // delegation context, never an exemption from openas or S-10.
    const openas::Actor bound = policy.development_grant ? session->attribute(actor) : actor;
    return openas::decide(verb, bound, false, cap, grants);
}
} // namespace mlibs::control
