// omui.agent:subject — the interface as a subject of the agent kernel
// (D-173): `ui`, beside whatever else the process holds — a world, a frame.
//
// It adds no operation and changes none: `ui.snapshot` is the protocol's
// `snapshot` (openmui.tree@0.1), decided by the runtime's own access as ever,
// answered in the same records. What is new is where it is found: in a
// kernel session (`subjects`, `describe`, then `ui.…`), under the contract
// every subject is held to (mlibs.agent.cts).
//
//     agent::Protocol protocol(runtime, options);
//     agent::InterfaceSubject interface(runtime, protocol);
//     mlibs::agent::SubjectSession session({&interface, &world});
//     // → {"op":"subjects"}  → {"op":"ui.snapshot","interactive":true}  → {"op":"world.observe"}
export module omui.agent:subject;

import std;
import mlibs.agent;
import mlibs.control;
import omui.core;
import omui.semantic;
import :catalog;
import :protocol;

export namespace omui::agent {

struct InterfaceOptions {
    // The host draws and can say which node drew a pixel (ProtocolOptions::pixel_owner): `ui.drew` traces.
    bool traces_pixels = false;
};

class InterfaceSubject : public mlibs::agent::Subject {
public:
    // Both outlive the subject. The protocol is the one the application's endpoint serves, or one of the
    // subject's own session.
    InterfaceSubject(Runtime& runtime, Protocol& protocol, InterfaceOptions options = {});
    ~InterfaceSubject() override;

    [[nodiscard]] std::string_view name() const noexcept override { return "ui"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "openmui.tree@0.1"; }
    // Observation, control (undo, redo, transactions), previews, the record of what input and actions did;
    // the source of a pixel where the host draws. Not here: an answer within a budget of bytes (a snapshot
    // is paged by nodes: `limit`, `depth`), and time as replayed steps (the clock is held and stepped by
    // `ui.clock`; a recorded session is replayed by `ui.record`'s scenario, outside a session).
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .control = true, .causes = true, .provenance = options_.traces_pixels, .preview = true, .history = true};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return omui::agent::listing; }
    // What the interface is, and the tasks to begin with: the application's own first (Verbs::add_task — its
    // verbs and its nodes' actions, written here as the requests that do them), then the interface's (the
    // catalog's). What is returned stays valid until the application's tasks change.
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override;
    // What `ui.state` answers, hashed: what was set in the interface — text, switches, values, scroll, focus.
    [[nodiscard]] std::uint64_t state_hash() const override;
    // What the application names, for `find`: its verbs (each with the request that tries it, a dry run), and
    // every node a person or an agent can act on — its role, its name and its actions — each with the request
    // that reads it (its path is what `ui.act` takes as `node`).
    [[nodiscard]] std::vector<mlibs::agent::Term> terms() const override;
    [[nodiscard]] std::string recorded_at() const override;  // the action journal's last entry (`ui.timeline`'s `actions`)
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    Runtime&               runtime_;
    Protocol&              protocol_;
    InterfaceOptions       options_;
    mutable semantic::Tree tree_;
    // The guide as written: each of the application's tasks as its name, its summary and its requests, and what views them.
    mutable std::vector<std::string>                   step_texts_;
    mutable std::vector<std::string_view>              steps_;
    mutable std::vector<mlibs::control::Task>          tasks_;
};

// A connection's conversation with an application: openmui.tree@0.1 as ever ({"op":"snapshot"}), and — on the
// same lines — the agent kernel's requests for everything the process holds: `subjects`, `describe` a subject
// or an operation, and any operation by its whole name (`ui.snapshot`, `world.observe`, `frame.at`). The two
// cannot be taken for each other: the protocol's requests have no dot in their names; its own `describe` names
// neither a `subject` nor an `operation`; its `find` takes a query that is an object (the kernel's `find` takes
// what is wanted, in words); and it has no `log` or `help` (its own are `logs`, `describe`).
//
// The interface is the first subject; the rest are the runtime's (Runtime::subjects: a game shown in a
// viewport, the host's frame). Each is decided by the application's own access, in this connection's session.
class ProcessSession final : public mlibs::agent::Session {
public:
    ProcessSession(Runtime& runtime, ProtocolOptions options);
    ~ProcessSession() override;
    void request(std::string_view line, const mlibs::agent::Reply& reply) override;
    void poll() override { protocol_.poll(); }
    [[nodiscard]] Protocol& protocol() noexcept { return protocol_; }
    // Where what is asked goes on record (`log`): the connection's own book, or the one its host gives every
    // connection, so that a request a connection (`omui agent call`) reads what the ones before it did.
    void keep_facts_in(std::shared_ptr<mlibs::agent::Facts> facts) { facts_ = std::move(facts); }

private:
    Runtime&         runtime_;
    std::string      default_actor_;
    std::shared_ptr<mlibs::agent::Facts> facts_ = std::make_shared<mlibs::agent::Facts>();
    Protocol         protocol_;
    InterfaceSubject interface_;
};

}  // namespace omui::agent
