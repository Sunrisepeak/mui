// openmui.core:verb — the application's named operations (spec §5).
//
// A verb is what a button, a shortcut, a script and an agent all invoke by
// name: `note.delete`. Its description states its effect; an irreversible or
// external verb states the answer that confirms it (S-1), and invoking it
// without that answer returns `needs-answer` at once (S-2) — nothing waits for
// a human inside the runtime.
export module omui.core:verb;

import std;
import :base;
import mlibs.control;

export namespace omui {

struct VerbDesc {
    std::string name;     // lower-case words joined by dots
    std::string summary;
    Effect      effect = Effect::reversible;
    std::string answer;   // required for irreversible and external verbs
    std::vector<std::pair<std::string, std::string>> args;  // name, type
    // Only the person at the machine invokes it — the answer to a
    // confirmation, a permission — never a request over the bus (S-10).
    bool        person = false;
    // The verb's function puts its own way back in the runtime's history
    // (Services::performed, Runtime::taken_back_by): undo takes it back, and
    // a preview says so. Else a reversible verb is not in the history (S-12).
    bool        undoable = false;
};

// One request of a task: a verb invoked, or one of a node's actions. The application says it in its own
// words (verbs, nodes); the request an agent sends is written from it by the interface's subject.
struct TaskStep {
    std::string verb;            // a registered verb …
    std::string node;            // … or a node's path ("main/dark")
    std::string action;          //   and one of its actions ("toggle")
    Value       args;            // the verb's or the action's arguments
    bool        dry_run = false; // only what it would do
};
// A task to begin with (D-186): something an agent that has never seen the application can get done here,
// and the steps that do it, in order. It is listed by `describe` beside the interface's own tasks, found by
// intent, and run as written by the contract suite — a task that does not run is a failure of the application.
struct TaskDesc {
    std::string           name;     // lower-case words joined by hyphens: "export-the-settings"
    std::string           summary;  // what it gets done
    std::vector<TaskStep> steps;
};

class AgentAccess;
class AgentSession;

// Who asks, with what, and the answer they give.
struct Invocation {
    Value       args;
    std::string answer;
    Actor       actor{"person", "user", {}};
    // The request came over the bus interface (the agent protocol, another
    // process): set by the runtime's protocol, never by what the request
    // says, so an actor that claims to be the person is still remote (S-10).
    bool        remote = false;
    // Only what would happen (a dry run, spec S-13): checked as it would be
    // performed, answered with the outcome as data, and nothing changes —
    // no state, no history, no journal entry, no event.
    bool        dry_run = false;
    // Trusted host context, never read from request JSON. Runtime/Verbs bind
    // the authoritative instance; session restrictions can only narrow it.
    std::shared_ptr<const AgentAccess> access;
    std::shared_ptr<const AgentSession> session;
    std::string operation;
};

[[nodiscard]] const Invocation& bind_invocation(const Invocation& given, Invocation& copy,
    const std::shared_ptr<const AgentAccess>& access, const Invocation* parent = nullptr, std::string_view operation = {});

// An instance-owned dynamic call context. A Runtime and its Verbs share
// this stack, so callbacks cannot shed remote authority by changing entry
// points. It is used only on their owning thread, never process-global.
using InvocationContext = mlibs::control::Context<Invocation>;
using InvocationScope = mlibs::control::Scope<Invocation>;

// The data of a dry run's answer (S-13): what performing it would do.
//   outcome  run | needs-answer | refused | fails
//   effect   its effect class; answer, the answer it asks for (needs-answer)
//   error    {code, text} when refused or failing (as an error record has them)
// `undoable` and the value before and after are added where known.
[[nodiscard]] Envelope dry_run_answer(std::string_view outcome, Effect effect, Value more = {});
// The outcome of a dry run that stopped at a gate: the gate's envelope as data.
[[nodiscard]] Envelope dry_run_of(const Envelope& stopped, Effect effect);

// The refusal of a person's own action asked for over the bus (S-10).
[[nodiscard]] Envelope person_only(std::string_view what);

// What a verb's implementation returns: a value for the `data` record, or a
// failure message.
using VerbResult = std::expected<Value, std::string>;
using VerbFn     = std::function<VerbResult(const Value& args)>;

[[nodiscard]] bool valid_verb_name(std::string_view name) noexcept;
// A verb declared by an operation catalog's entry (`<product>.ops.omui.toml`,
// convergence plan §10.3): its name, summary, effect, arguments (an optional
// one's type ends in `?`; a json one is an object) and whether it is undoable.
// An irreversible or external one names the answer that confirms it.
[[nodiscard]] VerbDesc verb_of(const mlibs::control::CatalogEntry& entry, std::string answer = {});

class Verbs {
public:
    void set_agent_access(std::shared_ptr<const AgentAccess> access) { access_ = std::move(access); }
    // The host owns the shared context longer than this registry. Null
    // restores the registry's own context (standalone Verbs).
    void set_invocation_context(InvocationContext* context) noexcept { context_ = context; }
    // Refused (with the reason) when the name is malformed, already taken, or
    // the verb needs an answer and states none (S-1).
    std::expected<void, std::string> add(VerbDesc desc, VerbFn fn);
    bool remove(std::string_view name);

    [[nodiscard]] const VerbDesc* find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const VerbDesc*> list() const;

    // A task to begin with, made of the verbs registered and of nodes' actions. Refused (with the reason) when
    // its name is malformed or taken, it says nothing of what it gets done, it has no step, a step is neither a
    // verb nor a node's action (or both), a step's verb is not registered or is the person's alone, or a step
    // would ask for an answer and is not a dry run: a task to begin with runs as written, and nothing in it
    // waits for a person. Removing a verb removes the tasks that invoke it.
    std::expected<void, std::string> add_task(TaskDesc task);
    [[nodiscard]] std::span<const TaskDesc> tasks() const noexcept { return tasks_; }

    // Runs a verb and answers with an envelope: `needs-answer` (exit 2) when
    // an answer is required and not given or wrong; `not-found` (exit 1) for
    // an unknown verb; otherwise the verb's data and exit 0, or its error and
    // exit 1. Every invocation is appended to the journal. A dry run
    // (`call.dry_run`) is checked the same way and answers its outcome
    // (dry_run_answer) without running the verb or journaling it.
    [[nodiscard]] Envelope invoke(std::string_view name, const Invocation& call);

    // The journal of invocations: who invoked what, and how it ended.
    struct Entry {
        std::string verb;
        std::string actor;
        Value       args;
        int         exit_code = 0;
    };
    [[nodiscard]] std::span<const Entry> journal() const noexcept { return journal_; }
    void clear_journal() { journal_.clear(); }

private:
    std::shared_ptr<const AgentAccess> access_;
    InvocationContext own_context_;
    InvocationContext* context_ = nullptr;
    struct Slot {
        VerbDesc desc;
        VerbFn   fn;
    };
    std::vector<Slot>  verbs_;
    std::vector<TaskDesc> tasks_;
    std::vector<Entry> journal_;
};

// The needs-answer envelope of S-2, shared by verbs and node actions.
[[nodiscard]] Envelope needs_answer(std::string_view what, std::string_view answer);

}  // namespace omui
