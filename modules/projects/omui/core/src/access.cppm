// The application's Agent policy (S-11): presets are configuration, not
// effects. Every remote action is still decided by openas::decide.
export module openmui.core:access;

import std;
export import mlibs.control;
import :base;
import :verb;

export namespace omui {

[[nodiscard]] std::optional<openas::Cap> parse_cap(std::string_view text) noexcept;
[[nodiscard]] std::string cap_name(const openas::Cap& cap);

using AgentMode = mlibs::control::Mode;
using AgentRestrictions = mlibs::control::Restrictions;
using AgentPolicy = mlibs::control::Policy;
[[nodiscard]] std::optional<AgentMode> parse_agent_mode(std::string_view text) noexcept;
[[nodiscard]] std::string_view agent_mode_name(AgentMode mode) noexcept;
[[nodiscard]] AgentMode agent_mode_for_profile(std::string_view profile) noexcept;
[[nodiscard]] AgentPolicy agent_policy_for_profile(std::string_view profile, AgentMode requested = AgentMode::automatic);
struct AgentPolicyState {
    AgentMode mode = AgentMode::dual_user;
    openas::Cap cap;
    bool enabled = true, input = true;
    bool development_grant = false;
    bool session_valid = true;
    std::string source;
    std::array<AgentRestrictions, 3> layers; // developer, user, session
    [[nodiscard]] bool allows(std::string_view operation) const;
    [[nodiscard]] Value to_value() const;
};

class AgentSession {
  public:
    [[nodiscard]] Actor attribute(const Actor& requested) const;
    [[nodiscard]] std::string owner() const;
    // Trusted native integration; this object is never deserialized from JSON.
    [[nodiscard]] const std::shared_ptr<const mlibs::control::Session>& control_session() const noexcept { return session_; }

  private:
    friend class AgentAccess;
    explicit AgentSession(std::shared_ptr<const mlibs::control::Session> session);
    std::shared_ptr<const mlibs::control::Session> session_;
};

class AgentAccess {
  public:
    AgentAccess();
    explicit AgentAccess(std::shared_ptr<mlibs::control::Access> access);
    AgentAccess(const AgentAccess&) = delete;
    AgentAccess& operator=(const AgentAccess&) = delete;
    [[nodiscard]] const std::shared_ptr<mlibs::control::Access>& control_binding() const noexcept { return access_; }
    void set_cap(openas::Cap cap);
    [[nodiscard]] openas::Cap cap() const;
    void set_policy(AgentPolicy policy);
    [[nodiscard]] AgentPolicy policy() const;
    [[nodiscard]] AgentPolicyState effective(const AgentSession* session = nullptr) const;
    // Trusted C++ hosts create sessions. Request JSON cannot construct one.
    // Development's owner grant exists only in this bound session context.
    [[nodiscard]] std::shared_ptr<const AgentSession> open_session(AgentRestrictions restrictions = {});
    [[nodiscard]] bool owns(const AgentSession& session) const;
    bool revoke_session(const AgentSession& session);
    void give(openas::Grant grant);
    std::size_t revoke(const openas::Actor& actor);
    void revoke_all();
    [[nodiscard]] std::vector<openas::Grant> grants() const;
    [[nodiscard]] openas::Decision decide(std::string_view name, Effect effect, bool person, const Actor& actor,
                                          const AgentSession* session = nullptr, std::string_view operation = {}) const;

  private:
    std::shared_ptr<mlibs::control::Access> access_;
};

// Compatibility fallback for unbound low-level runtimes and standalone Verbs.
// Applications/independent hosts should bind an instance for isolation.
[[nodiscard]] AgentAccess& agent_access();
[[nodiscard]] openas::Decision clearance(std::string_view name, Effect effect, bool person, const Invocation& call);
[[nodiscard]] Envelope above_cap(std::string_view what, const Invocation& call = {});
[[nodiscard]] openas::Actor to_openas(const Actor& actor);

enum class ControlInvocationError : std::uint8_t { unbound_access, missing_session };
// Convert an already host-bound UI call, not request JSON. No implicit global
// policy, new session, permission mapping or admission is introduced.
[[nodiscard]] std::expected<mlibs::control::Invocation,ControlInvocationError>
    to_control_invocation(const Invocation&);

} // namespace omui
