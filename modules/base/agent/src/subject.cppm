// mlibs.agent:subject — what an agent finds, observes and operates in a
// process (D-173): an interface, a world, an animation stage, a renderer's
// frame. Each gives, from its own data model, what it can of the six things
// an agent needs — state, observation, control, time, causes, identity and
// authority — and says what it cannot; it never pretends.
//
// A session over a process's subjects speaks one small protocol, whatever the
// subjects are:
//
//   → {"op":"subjects"}                               who is here, what each can do, its state's hash
//   → {"op":"describe","subject":"world"}             what it is, the tasks to begin with (each step a request to
//                                                     send as it is), and its operations briefly: name, effect, a line
//   → {"op":"describe","operation":"world.act"}       one of them whole: its parameters, cost, how it is undone, examples
//   → {"op":"describe","subject":"world","detail":"full"}   the whole catalog (its listing: <product>.ops.json)
//   → {"op":"find","q":"set something on fire"}       what answers an intent, across the subjects: operations, tasks,
//                                                     what a subject's content names — each with requests to send
//   → {"op":"log","since":12}                          what was asked here, as facts: every change, every failure (:facts)
//   → {"op":"world.observe","re":7, …}                one of them — decided first (mlibs.control, openas)
//
// The session's own four requests have no dot in their names; a subject's
// operations are addressed under it (`world.…`), so neither can take the
// other's name.
//
// What a subject can do is disclosed in tiers, so that finding out costs an
// agent as little as it needs: who is here; then names and a line each; then
// one operation, or all of them, whole. The operations are a catalog's
// (`<product>.ops.omui.ts`, omui-gen ops): mlibs.control's entries.
//   ← {"kind":"data","re":7,"data":{…}}
//   ← {"kind":"result","re":7,"exitCode":0}
//
// An operation the policy asks a person about ends in `needs-answer` (exit
// code 2) until the request carries the answer; one it refuses is `refused`
// and does not run. Nothing reaches a subject without that decision.
//
// An agent needs no more than this to begin (D-186): `subjects`, then
// `describe` — the guide is the catalog's own data (its doc, tasks and
// examples), and every error carries `next`, requests that lead out of it:
// the nearest names for one misspelt, the operation described and an example
// for one sent without what it needs, the same request with the answer for
// one a person is asked about.
export module mlibs.agent:subject;

import std;
import openas;
import mlibs.control;
import :endpoint;
import :envelope;
import :facts;

export namespace mlibs::agent {

// What a subject gives beyond its state (which it has by being one).
struct Capabilities {
    bool observation = false;  // a designed projection of its state
    bool budget = false;       // … answered within a budget of bytes the request gives
    bool control = false;      // operations that change it
    bool time = false;         // steps, seeks, replays
    bool causes = false;       // why its state is what it is, and who made each change
    bool provenance = false;   // from what it puts out (a pixel, a sound, a value) back to what made it
    bool preview = false;      // a branch run ahead and compared, the state untouched
    bool history = false;      // commits, branches, what was undone
};

// Something of a subject's content an agent may be looking for besides its operations and tasks — a
// material, a law, a channel, a node: its kind, its name, a line of what it is, and a request that shows or uses it.
struct Term {
    std::string kind, name, text, request;
};

class Subject {
public:
    virtual ~Subject() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;            // "world"
    [[nodiscard]] virtual std::string_view interface_name() const noexcept = 0;  // what its operations speak: "xengine.world@0.1"
    [[nodiscard]] virtual Capabilities capabilities() const noexcept = 0;
    // Its catalog (omui-gen ops's table): each operation by its whole name ("world.observe"), with its effect,
    // parameters, cost and how it is undone.
    [[nodiscard]] virtual std::span<const mlibs::control::CatalogEntry> operations() const noexcept = 0;
    // The catalog's listing (<product>.ops.json: every operation with its input schema, the tiers), when it has one.
    [[nodiscard]] virtual std::string_view listing() const noexcept { return {}; }
    // What it tells an agent that knows nothing of it: what it is, and the tasks to begin with (its catalog's `guide`).
    [[nodiscard]] virtual mlibs::control::Guide guide() const noexcept { return {}; }
    // What `find` looks through besides the catalog: what its content names. Asked when a `find` is, never a frame.
    [[nodiscard]] virtual std::vector<Term> terms() const { return {}; }
    // A hash of its whole state: the same state, the same hash. 0: it has none to give.
    [[nodiscard]] virtual std::uint64_t state_hash() const { return 0; }
    // One of its operations (its whole name), already decided: its data, then
    // its result (a result it does not give is `done`).
    virtual void operate(std::string_view operation, const Request& request, Answer& answer) = 0;
    // Once a pump, at the owner's safe point.
    virtual void poll() {}
};

struct SessionOptions {
    // Who decides. None: every declared operation runs — an owner's own tools and tests.
    std::shared_ptr<const mlibs::control::Access>  access;
    std::shared_ptr<const mlibs::control::Session> session;
    openas::Actor actor{openas::Kind::agent, {}, {}};
    // Where what is asked goes on record (norm 11). None: the session keeps a book of its own; a host that
    // gives every session the same one lets each read what the others did.
    std::shared_ptr<Facts> facts;
};

// What `find` found: an operation (its example, or its `describe`), a task (its steps), or a term of a subject's
// content (its request). `score`: whole numbers, the same on every machine.
struct Found {
    std::string              kind, subject, name, text;
    std::vector<std::string> requests;
    std::uint64_t            score = 0;
};
// What answers an intent — a few words, or a sentence — across the subjects, the best first; of equals, as the
// subjects list them. Words are matched as written (lowercase, without endings): no model, nothing learnt, the
// same answer everywhere. A word in a name weighs most, then in what it says, then in its parameters and
// keywords; a word few things have weighs more than one many have; what has more of the words wanted comes first.
[[nodiscard]] std::vector<Found> find(std::span<Subject* const> subjects, std::string_view query, std::size_t limit = 5);

// One connection's conversation with a process's subjects.
class SubjectSession final : public Session {
public:
    SubjectSession(std::vector<Subject*> subjects, SessionOptions options = {});
    void request(std::string_view line, const Reply& reply) override;
    void poll() override;

private:
    std::vector<Subject*> subjects_;
    SessionOptions        options_;
};

}  // namespace mlibs::agent
