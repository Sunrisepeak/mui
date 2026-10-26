// openmui.core:verb — the application's named operations (spec §5).
//
// A verb is what a button, a shortcut, a script and an agent all invoke by
// name: `note.delete`. Its description states its effect; an irreversible or
// external verb states the answer that confirms it (S-1), and invoking it
// without that answer returns `needs-answer` at once (S-2) — nothing waits for
// a human inside the runtime.
export module openmui.core:verb;

import std;
import :base;

export namespace omui {

struct VerbDesc {
    std::string name;     // lower-case words joined by dots
    std::string summary;
    Effect      effect = Effect::reversible;
    std::string answer;   // required for irreversible and external verbs
    std::vector<std::pair<std::string, std::string>> args;  // name, type
};

// Who asks, with what, and the answer they give.
struct Invocation {
    Value       args;
    std::string answer;
    Actor       actor{"person", "user", {}};
};

// What a verb's implementation returns: a value for the `data` record, or a
// failure message.
using VerbResult = std::expected<Value, std::string>;
using VerbFn     = std::function<VerbResult(const Value& args)>;

[[nodiscard]] bool valid_verb_name(std::string_view name) noexcept;

class Verbs {
public:
    // Refused (with the reason) when the name is malformed, already taken, or
    // the verb needs an answer and states none (S-1).
    std::expected<void, std::string> add(VerbDesc desc, VerbFn fn);
    bool remove(std::string_view name);

    [[nodiscard]] const VerbDesc* find(std::string_view name) const noexcept;
    [[nodiscard]] std::vector<const VerbDesc*> list() const;

    // Runs a verb and answers with an envelope: `needs-answer` (exit 2) when
    // an answer is required and not given or wrong; `not-found` (exit 1) for
    // an unknown verb; otherwise the verb's data and exit 0, or its error and
    // exit 1. Every invocation is appended to the journal.
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
    struct Slot {
        VerbDesc desc;
        VerbFn   fn;
    };
    std::vector<Slot>  verbs_;
    std::vector<Entry> journal_;
};

// The needs-answer envelope of S-2, shared by verbs and node actions.
[[nodiscard]] Envelope needs_answer(std::string_view what, std::string_view answer);

}  // namespace omui
