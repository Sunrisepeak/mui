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
import mlibs.error;

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

// Why work an application or a plugin provides did not succeed — a verb's, a task's, a language's (the player's
// plugins): what kind of failure (the error contract's, mlibs.error — the code an `error` record carries) and a
// message. A message alone is a `failed` one (`return std::unexpected("…")`); `{Kind::not_found, "no note 7"}` says
// more to an agent; a typed failure (mlibs.error) keeps its kind and its text.
struct Failure {
    mlibs::error::Kind kind = mlibs::error::Kind::failed;
    std::string        message;
    std::string        way;  // what to do about it (a domain error's way out, RC2.19); the error record's `way`
    Failure() = default;
    Failure(std::string text) : message(std::move(text)) {}
    Failure(const char* text) : message(text) {}
    Failure(mlibs::error::Kind of, std::string text, std::string out = {}) : kind(of), message(std::move(text)), way(std::move(out)) {}
    template <mlibs::error::Code C, class Where>
    Failure(const mlibs::error::Error<C, Where>& error) : kind(error.kind()), message(error.text()), way(error.way()) {}
    [[nodiscard]] std::string text() const { return std::format("{}: {}", mlibs::error::name(kind), message); }
};

// A failure ends the answer: an `error` record of its kind's code, its message and its way out (`way`), then the
// failing result.
Envelope& failed(Envelope& answer, const Failure& failure);

// What a verb's implementation returns: a value for the `data` record, or why it failed.
using VerbResult = std::expected<Value, Failure>;
using VerbFn     = std::function<VerbResult(const Value& args)>;

[[nodiscard]] bool valid_verb_name(std::string_view name) noexcept;

// Why a verb or a task cannot be registered (mlibs.error).
enum class VerbError : std::uint8_t { bad_verb_name, verb_taken, no_answer, no_implementation, bad_task_name, task_taken, no_summary, no_step, bad_step, unknown_verb, person_only_step, step_asks_answer };
[[nodiscard]] constexpr mlibs::error::Description describe(VerbError error) noexcept {
    using Kind = mlibs::error::Kind;
    switch (error) {
        case VerbError::bad_verb_name: return {"omui.verb.bad-name", Kind::invalid_argument, "lower-case words joined by dots", "name the verb in lower-case words joined by dots: `document.save`"};
        case VerbError::verb_taken: return {"omui.verb.taken", Kind::refused, "a verb of that name is registered", "give the verb another name, or replace the registered one where it is registered"};
        case VerbError::no_answer: return {"omui.verb.no-answer", Kind::invalid_argument, "an irreversible or external verb states its answer (S-1)", "state the verb's answer (what a dry run returns) when it is irreversible or reaches outside"};
        case VerbError::no_implementation: return {"omui.verb.no-implementation", Kind::invalid_argument, "a verb has a function", "give the verb its function"};
        case VerbError::bad_task_name: return {"omui.task.bad-name", Kind::invalid_argument, "lower-case words joined by hyphens", "name the task in lower-case words joined by hyphens: `save-and-close`"};
        case VerbError::task_taken: return {"omui.task.taken", Kind::refused, "a task of that name is registered", "give the task another name, or replace the registered one where it is registered"};
        case VerbError::no_summary: return {"omui.task.no-summary", Kind::invalid_argument, "a task says what it gets done", "give the task a summary: one sentence of what it gets done"};
        case VerbError::no_step: return {"omui.task.no-step", Kind::invalid_argument, "a task has steps", "give the task at least one step"};
        case VerbError::bad_step: return {"omui.task.bad-step", Kind::invalid_argument, "a step is a verb, or a node and one of its actions", "write each step as a verb, or as a node and one of its actions"};
        case VerbError::unknown_verb: return {"omui.task.unknown-verb", Kind::not_found, "a step's verb is registered first", "register the verb before the task that uses it, or name a registered one"};
        case VerbError::person_only_step: return {"omui.task.person-only-step", Kind::refused, "a person's verb is never a step", "leave the person's verb out of the task: the task stops for the person to do it"};
        case VerbError::step_asks_answer: return {"omui.task.step-asks-answer", Kind::invalid_argument, "a step that asks for an answer is a dry run", "make the step a dry run, or take the answer out of the task"};
    }
    return {"omui.verb.?", Kind::internal};
}
using VerbRegistrationFailure = mlibs::error::Error<VerbError>;
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
    std::expected<void, VerbRegistrationFailure> add(VerbDesc desc, VerbFn fn);
    bool remove(std::string_view name);

    [[nodiscard]] const VerbDesc* find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const VerbDesc*> list() const;

    // A task to begin with, made of the verbs registered and of nodes' actions. Refused (with the reason) when
    // its name is malformed or taken, it says nothing of what it gets done, it has no step, a step is neither a
    // verb nor a node's action (or both), a step's verb is not registered or is the person's alone, or a step
    // would ask for an answer and is not a dry run: a task to begin with runs as written, and nothing in it
    // waits for a person. Removing a verb removes the tasks that invoke it.
    std::expected<void, VerbRegistrationFailure> add_task(TaskDesc task);
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

// A failure written as its text (`std::format("{}", failure)`).
template <>
struct std::formatter<omui::Failure> : std::formatter<std::string_view> {
    auto format(const omui::Failure& failure, std::format_context& context) const {
        return std::formatter<std::string_view>::format(failure.text(), context);
    }
};
