// The application's Agent policy (S-11): presets are configuration, not
// effects. Every remote action is still decided by openas::decide.
export module openmui.core:access;

import std;
export import openas;
import :base;
import :verb;

export namespace omui {

[[nodiscard]] std::optional<openas::Cap> parse_cap(std::string_view text) noexcept;
[[nodiscard]] std::string cap_name(const openas::Cap& cap);

enum class AgentMode : std::uint8_t { automatic, development, dual_user };
[[nodiscard]] std::optional<AgentMode> parse_agent_mode(std::string_view text) noexcept;
[[nodiscard]] std::string_view agent_mode_name(AgentMode mode) noexcept;
// Shared by SDK startup and independent hosts/providers. No UI/SDK dependency.
[[nodiscard]] AgentMode agent_mode_for_profile(std::string_view profile) noexcept;

// Missing values inherit. A present empty operations list denies operations;
// lists use exact names, prefix.* or *. Layers intersect, never widen.
struct AgentRestrictions {
    std::optional<openas::Cap> cap;
    std::optional<bool> enabled, input;
    std::optional<AgentMode> mode;
    std::optional<std::vector<std::string>> operations;
};
struct AgentPolicy {
    AgentMode mode = AgentMode::dual_user;
    openas::Cap cap;
    bool enabled = true, input = true;
    std::string source = "explicit";
    AgentRestrictions developer, user;
};
// A reusable startup preset for SDKs, embedded hosts and independent
// control providers. Only the exact dev profile selects development.
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

  private:
    friend class AgentAccess;
    AgentSession(std::weak_ptr<const void> issuer, std::uint64_t epoch, bool development, openas::Actor owner,
                 AgentRestrictions restrictions);
    std::weak_ptr<const void> issuer_;
    std::uint64_t epoch_ = 0;
    bool development_ = false;
    openas::Actor owner_;
    AgentRestrictions restrictions_;
    mutable std::atomic<bool> active_{true};
};

class AgentAccess {
  public:
    AgentAccess();
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
    [[nodiscard]] AgentPolicyState effective_locked(const AgentSession* session) const;
    mutable std::mutex mutex_;
    AgentPolicy policy_;
    std::vector<openas::Grant> grants_;
    std::shared_ptr<const void> identity_;
    std::uint64_t grant_epoch_ = 0, session_next_ = 0;
};

// Compatibility fallback for unbound low-level runtimes and standalone Verbs.
// Applications/independent hosts should bind an instance for isolation.
[[nodiscard]] AgentAccess& agent_access();
[[nodiscard]] openas::Decision clearance(std::string_view name, Effect effect, bool person, const Invocation& call);
[[nodiscard]] Envelope above_cap(std::string_view what, const Invocation& call = {});
[[nodiscard]] openas::Actor to_openas(const Actor& actor);

} // namespace omui
