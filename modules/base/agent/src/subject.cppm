// mlibs.agent:subject — what an agent finds, observes and operates in a
// process (D-173): an interface, a world, an animation stage, a renderer's
// frame. Each gives, from its own data model, what it can of the six things
// an agent needs — state, observation, control, time, causes, identity and
// authority — and says what it cannot; it never pretends.
//
// A session over a process's subjects speaks one small protocol, whatever the
// subjects are:
//
//   → {"op":"subjects"}                       who is here, what each can do, its state's hash
//   → {"op":"world.describe"}                 a subject's operations: name, effect, what it does
//   → {"op":"world.observe","re":7, …}        one of them — decided first (mlibs.control, openas)
//   ← {"kind":"data","re":7,"data":{…}}
//   ← {"kind":"result","re":7,"exitCode":0}
//
// An operation the policy asks a person about ends in `needs-answer` (exit
// code 2) until the request carries the answer; one it refuses is `refused`
// and does not run. Nothing reaches a subject without that decision.
export module mlibs.agent:subject;

import std;
import openas;
import mlibs.control;
import :endpoint;
import :envelope;

export namespace mlibs::agent {

// What a subject gives beyond its state (which it has by being one).
struct Capabilities {
    bool observation = false;  // a designed projection of its state, within a budget
    bool control = false;      // operations that change it
    bool time = false;         // steps, seeks, replays
    bool causes = false;       // why its state is what it is
    bool preview = false;      // a branch run ahead and compared, the state untouched
    bool history = false;      // commits, branches, what was undone
};

struct Operation {
    std::string_view name;    // within its subject: "observe"
    openas::Effect   effect = openas::Effect::read;
    std::string_view doc;
};

class Subject {
public:
    virtual ~Subject() = default;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;            // "world"
    [[nodiscard]] virtual std::string_view interface_name() const noexcept = 0;  // what its operations speak: "xengine.world@0.1"
    [[nodiscard]] virtual Capabilities capabilities() const noexcept = 0;
    [[nodiscard]] virtual std::span<const Operation> operations() const noexcept = 0;
    // A hash of its whole state: the same state, the same hash. 0: it has none to give.
    [[nodiscard]] virtual std::uint64_t state_hash() const { return 0; }
    // One of its operations, already decided: its data, then its result (a
    // result it does not give is `done`).
    virtual void operate(std::string_view operation, const Request& request, Answer& answer) = 0;
    // Once a pump, at the owner's safe point.
    virtual void poll() {}
};

struct SessionOptions {
    // Who decides. None: every declared operation runs — an owner's own tools and tests.
    std::shared_ptr<const mlibs::control::Access>  access;
    std::shared_ptr<const mlibs::control::Session> session;
    openas::Actor actor{openas::Kind::agent, {}, {}};
};

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
