// openmui.agent:protocol — openmui.tree@0.1 as JSON lines (spec §9, plan §11).
//
// One request per line, answered by envelope records, one per line, the last
// a `result` (openas §5):
//
//   → {"op":"find","q":{"role":"button","name":"Delete*"}}
//   ← {"kind":"data","data":{"nodes":[...]}}
//   ← {"kind":"result","exitCode":0}
//
// A request may carry "re": any JSON value, repeated in each of its records,
// so a client can have several in flight. Operations: describe, snapshot,
// changes, find, act, invoke (an application verb), wait, capture. The protocol is transport-free: the
// endpoint feeds it lines from local IPC; tests and the MCP bridge feed it
// directly. It runs on the thread that owns the runtime.
export module openmui.agent:protocol;

import std;
import openmui.core;
import openmui.view;
import openmui.semantic;

export namespace omui::agent {

inline constexpr std::string_view interface_name = "openmui.tree@0.1";

// Pixels of a region, as PNG bytes (hosts that render provide it).
using CaptureFn = std::function<std::expected<std::vector<std::uint8_t>, std::string>(Rect window)>;

struct ProtocolOptions {
    std::string app;              // the application id, for `describe`
    std::string default_actor = "agent:anonymous";  // when a request names none
    double      default_timeout = 5.0;               // seconds, for `wait`
    CaptureFn   capture;
    // Requests come over the bus (an endpoint, another process): a person's
    // own action is refused to them whatever actor they name (S-10). False
    // only for the program's own requests through this handler (the C ABI
    // its language bindings call).
    bool        remote = true;
};

class Protocol {
public:
    using Reply = std::function<void(std::string_view line)>;

    Protocol(Runtime& runtime, ProtocolOptions options = {});

    // Handles one request line; replies now, or (for `wait`) when the
    // condition holds or times out — see `poll`.
    void request(std::string_view line, const Reply& reply);
    // Checks pending waits; call after each frame.
    void poll();
    [[nodiscard]] std::size_t pending() const noexcept { return waits_.size(); }

private:
    struct Wait {
        semantic::Condition                   condition;
        Value                                 re;
        std::chrono::steady_clock::time_point deadline;
        Reply                                 reply;
    };
    void send(const Reply& reply, const Envelope& envelope, const Value& re) const;
    Envelope handle(const Value& req, const Value& re, const Reply& reply, bool& deferred);

    Runtime&          runtime_;
    semantic::Tree    tree_;
    ProtocolOptions   options_;
    std::vector<Wait> waits_;
};

// The record lines of an envelope, each with "re" when it is not null.
[[nodiscard]] std::vector<std::string> lines(const Envelope& envelope, const Value& re = {});

}  // namespace omui::agent
