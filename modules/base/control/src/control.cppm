// Native call authority shared by independently hosted engines and UI.
// Domain operations/payloads stay with their owner; no transport or UI types.
export module mlibs.control;

import std;
export import openas;

export namespace mlibs::control {

[[nodiscard]] std::optional<openas::Cap> parse_cap(std::string_view text) noexcept;
[[nodiscard]] std::string cap_name(const openas::Cap& cap);
enum class Mode : std::uint8_t { automatic, development, dual_user };
[[nodiscard]] std::optional<Mode> parse_mode(std::string_view text) noexcept;
[[nodiscard]] std::string_view mode_name(Mode mode) noexcept;
[[nodiscard]] Mode mode_for_profile(std::string_view profile) noexcept;

// Missing values inherit. An empty operations list denies everything;
// exact names, prefix.* and * are supported. All layers intersect.
struct Restrictions {
    std::optional<openas::Cap> cap;
    std::optional<bool> enabled, input;
    std::optional<Mode> mode;
    std::optional<std::vector<std::string>> operations;
};
struct Policy {
    Mode mode = Mode::dual_user;
    openas::Cap cap;
    bool enabled = true, input = true;
    std::string source = "explicit";
    Restrictions developer, user;
};
[[nodiscard]] Policy policy_for_profile(std::string_view profile, Mode requested = Mode::automatic);

// Borrowed operation filter for snapshots and serialization adapters. It
// performs no copies; effect ceilings and grants are evaluated by Access.
struct OperationPolicy {
    bool enabled = true, input = true, session_valid = true;
    std::span<const Restrictions> layers;
    [[nodiscard]] bool allows(std::string_view operation) const;
};
struct PolicyState {
    Mode mode = Mode::dual_user;
    openas::Cap cap;
    bool enabled = true, input = true;
    bool development_grant = false;
    bool session_valid = true;
    std::string source;
    std::array<Restrictions, 3> layers; // developer, user, session
    [[nodiscard]] bool allows(std::string_view operation) const;
};

class Session {
public:
    [[nodiscard]] openas::Actor attribute(const openas::Actor& requested) const;
    [[nodiscard]] std::string owner() const;
    // Metadata only: authority is rechecked against the issuer and epoch
    // by Access::decide. This flag cannot establish a live grant.
    [[nodiscard]] bool created_for_development() const noexcept { return development_; }
private:
    friend class Access;
    Session(std::weak_ptr<const void> issuer, std::uint64_t epoch, bool development,
            openas::Actor owner, Restrictions restrictions);
    std::weak_ptr<const void> issuer_;
    std::uint64_t epoch_ = 0;
    bool development_ = false;
    openas::Actor owner_;
    Restrictions restrictions_;
    mutable std::atomic<bool> active_{true};
};

// Cold control path; thread-safe policy/session operations. No global access
// object and no per-frame/pixel/sample work. The host binds an instance.
class Access {
public:
    explicit Access(Policy policy = {});
    void set_cap(openas::Cap cap);
    [[nodiscard]] openas::Cap cap() const;
    void set_policy(Policy policy);
    [[nodiscard]] Policy policy() const;
    [[nodiscard]] PolicyState effective(const Session* session = nullptr) const;
    // Only trusted native hosts create sessions, never request payloads.
    [[nodiscard]] std::shared_ptr<const Session> open_session(Restrictions restrictions = {});
    [[nodiscard]] bool owns(const Session& session) const;
    bool revoke_session(const Session& session);
    void give(openas::Grant grant);
    std::size_t revoke(const openas::Actor& actor);
    void revoke_all();
    [[nodiscard]] std::vector<openas::Grant> grants() const;
    // Evaluates a remote request; its actor is never observed as present.
    // Supply the transport/domain operation when different from name. An
    // empty operation falls back to name, never bypassing the restrictions.
    [[nodiscard]] openas::Decision decide(std::string_view name, openas::Effect effect, bool person,
        const openas::Actor& actor, const Session* session = nullptr, std::string_view operation = {}) const;
private:
    [[nodiscard]] PolicyState effective_locked(const Session* session) const;
    mutable std::mutex mutex_;
    Policy policy_;
    std::vector<openas::Grant> grants_;
    std::shared_ptr<const void> identity_;
    std::uint64_t grant_epoch_ = 0, session_next_ = 0;
};

// Authority metadata only. Payloads, revisions, results and undo belong to
// the domain. Remote status and sessions are set by the trusted host.
struct Invocation {
    std::string answer;
    openas::Actor actor{openas::Kind::person, "user", {}};
    bool remote = false;
    bool dry_run = false;
    std::shared_ptr<const Access> access;
    std::shared_ptr<const Session> session;
    std::string operation;
};

// Adapters can retain their native Invocation/Actor representation while
// sharing binding and inheritance. Call supplies the fields above; its access
// supplies effective(), and its session supplies attribute().
template<class Call>
[[nodiscard]] const Call& bind_invocation(const Call& given, Call& copy,
    const decltype(Call::access)& access, std::type_identity_t<const Call*> parent = nullptr, std::string_view operation = {}) {
    const bool inherit = parent && parent->remote && !given.remote;
    const bool bind = access && given.access != access;
    const auto& source = inherit ? *parent : given;
    const auto* policy = access ? access.get() : source.access.get();
    const bool delegate = source.remote && source.session && policy &&
        policy->effective(source.session.get()).development_grant;
    const bool identify = source.remote && source.operation.empty() && !operation.empty();
    constexpr bool has_preview = requires(Call value) { value.dry_run = true; };
    const bool simulate = [&] {
        if constexpr(has_preview) return parent && parent->dry_run && !given.dry_run;
        else return false;
    }();
    if (!inherit && !bind && !delegate && !identify && !simulate) return given;
    copy = given;
    if constexpr(has_preview) if (simulate) copy.dry_run = true;
    if (inherit) {
        copy.actor = parent->actor;
        copy.remote = true;
        copy.access = parent->access;
        copy.session = parent->session;
        copy.operation = parent->operation;
    }
    if (access) copy.access = access;
    if (identify) copy.operation = std::string(operation);
    if (delegate) copy.actor = copy.session->attribute(copy.actor);
    return copy;
}

template<class Call> class Scope;
// Instance-owned and single-threaded. Deferred/worker tasks copy the call
// by value, enter a scope on their destination, then recheck live authority.
template<class Call> class Context {
public:
    [[nodiscard]] const Call* current() const noexcept { return calls_.empty() ? nullptr : calls_.back(); }
private:
    friend class Scope<Call>;
    std::vector<const Call*> calls_;
};
template<class Call> class Scope {
public:
    Scope(Context<Call>& context, const Call& call) : context_(context) { context_.calls_.push_back(&call); }
    ~Scope() { context_.calls_.pop_back(); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    Context<Call>& context_;
};
using InvocationContext = Context<Invocation>;
using InvocationScope = Scope<Invocation>;

struct Operation {
    std::string_view name;
    openas::Effect effect = openas::Effect::read;
    bool person = false;
};
// An operation's catalog entry (convergence plan §10.3): a product's
// `<product>.ops.omui.toml`, generated into its code by `omui-gen ops`, from
// which its CLI, MCP and openas bindings and its reference are made.
struct Parameter {
    std::string_view name, type, doc;  // type: bool integer number seconds string json, or a named kind ("playback")
    bool optional = false;
};
enum class Cost : std::uint8_t { instant, frame, bounded, heavy };
struct CatalogEntry {
    Operation operation;
    std::string_view summary;
    std::span<const Parameter> parameters;
    std::string_view result;      // a type as parameters' (empty: nothing)
    Cost cost = Cost::instant;    // instant: no work; frame: a frame's; bounded: bounded by its inputs; heavy: may take long
    std::string_view undo;        // how its effect is undone (empty for a read)
    bool cancellable = false;     // it can be cancelled while it runs
    bool undoable = false;        // the host's history takes it back (undo, redo)
    bool delegated = false;       // its effect is what it runs (an action, a verb), decided there; `operation.effect` is the most
    // Requests of it as they are sent (a JSON object a line), the plainest first: what `describe` shows with
    // the operation, and what a refused or malformed request is answered with (D-186).
    std::span<const std::string_view> examples;
    // Words an intent may use for it that its summary does not ("fire" for a heat), a space between: `find` reads them.
    std::string_view keywords;
};
// A task to begin with: what it gets done, and the requests that do it, in order, each as it is sent.
struct Task {
    std::string_view name;     // "burn-a-plank"
    std::string_view summary;
    std::span<const std::string_view> steps;
};
// What a catalog tells an agent that knows nothing of its subject (D-186): what the subject is, in a line, and
// the tasks to begin with. A catalog's data, as its operations are (`<product>.ops.omui.ts`: doc, tasks).
struct Guide {
    std::string_view about;
    std::span<const Task> tasks;
};
// Cold synchronous admission. Bind authority before authorization callbacks;
// keep this scope alive through domain execution and dependent output.
class OperationScope {
public:
    OperationScope(InvocationContext&, std::shared_ptr<const Access>, const Invocation&, Operation);
    OperationScope(const OperationScope&) = delete;
    OperationScope& operator=(const OperationScope&) = delete;
    [[nodiscard]] openas::Decision decision() const noexcept { return decision_; }
    [[nodiscard]] bool dry_run() const noexcept { return call_.dry_run; }
    [[nodiscard]] bool execute() const noexcept { return decision_==openas::Decision::run && !dry_run(); }
    [[nodiscard]] const Invocation& invocation() const & noexcept { return call_; }
    const Invocation& invocation() const && = delete;
private:
    Invocation call_;
    InvocationScope scope_;
    openas::Decision decision_ = openas::Decision::refuse;
};

} // namespace mlibs::control
