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
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return omui::agent::guide; }
    // What `ui.state` answers, hashed: what was set in the interface — text, switches, values, scroll, focus.
    [[nodiscard]] std::uint64_t state_hash() const override;
    [[nodiscard]] std::string recorded() const override;  // the action journal's last entry (`ui.timeline`'s `actions`)
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    Runtime&               runtime_;
    Protocol&              protocol_;
    InterfaceOptions       options_;
    mutable semantic::Tree tree_;
};

// A connection's conversation with an application: openmui.tree@0.1 as ever ({"op":"snapshot"}), and — on the
// same lines — the agent kernel's requests for everything the process holds: `subjects`, `describe` a subject
// or an operation, and any operation by its whole name (`ui.snapshot`, `world.observe`, `frame.at`). The two
// cannot be taken for each other: the protocol's requests have no dot in their names, and its own `describe`
// names neither a `subject` nor an `operation`.
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

private:
    Runtime&         runtime_;
    std::string      default_actor_;
    Protocol         protocol_;
    InterfaceSubject interface_;
};

}  // namespace omui::agent
