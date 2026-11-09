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
// changes, find, act, invoke (an application verb), wait, capture, and —
// when the application allows it — input. The protocol is transport-free: the
// endpoint feeds it lines from local IPC; tests and the MCP bridge feed it
// directly. It runs on the thread that owns the runtime.
export module omui.agent:protocol;

import std;
import omui.core;
import omui.view;
import omui.semantic;
import xrender.vector;
import mlibs.image;

export namespace omui::agent {

inline constexpr std::string_view interface_name = "openmui.tree@0.1";

// Pixels of a region, as PNG bytes (hosts that render provide it).
using CaptureFn = std::function<std::expected<std::vector<std::uint8_t>, std::string>(Rect window)>;

// The same region as a picture of vectors: SVG with every node's drawing
// marked by its path (data-node), and where pixels stand in for what SVG
// cannot say (an effect, a conic paint), each island and why.
struct VectorIsland {
    std::string why;
    Rect        bounds;
};
struct VectorCapture {
    std::string               svg;
    std::vector<VectorIsland> islands;
};
using CaptureSvgFn = std::function<std::expected<VectorCapture, std::string>(Rect window)>;

// What every rendering host gives as its CaptureSvgFn: the runtime's current
// display list as SVG over `region` (logical pixels), each node named by its
// path, islands drawn at `scale` device pixels per logical one.
[[nodiscard]] std::expected<VectorCapture, std::string> vector_capture(const Runtime& runtime, const xrender::Resources& resources, Rect region,
                                                                       float scale, std::optional<Color> background);

struct ProtocolOptions {
    std::string app;              // the application id, for `describe`
    std::string default_actor = "agent:anonymous";  // when a request names none
    double      default_timeout = 5.0;               // seconds, for `wait`
    CaptureFn   capture;
    CaptureSvgFn capture_svg;  // `capture` with format "svg"; none: PNG only
    // The node whose drawing a pixel (logical coordinates) shows, from the
    // renderer's object ids — `drew` (Part 2 §4.1, B); none: the host cannot say.
    std::function<NodeId(Point)> pixel_owner;
    // Requests come over the bus (an endpoint, another process): a person's
    // own action is refused to them whatever actor they name (S-10). False
    // only for the program's own requests through this handler (the C ABI
    // its language bindings call).
    bool        remote = true;
    // Input as a person's devices give it — pointer, wheel, keys, text —
    // delivered by the host a frame at a time (`input`, `take_input`). What
    // it performs is the agent's and decided as its requests are
    // (Runtime::set_input_actor: the person's own actions refused, the cap,
    // the grants, the answers). The hosts offer it unless the application
    // or the person turns it off (AppSpec::agent_input, --no-agent-input);
    // the protocol alone, without a host to deliver it, does not.
    bool        input = false;
    // Only the trusted host supplies a session handle or its narrowing
    // limits. Request JSON (including dev/person fields) cannot create one.
    // With none, the first request opens a session for the Runtime binding.
    std::shared_ptr<const AgentSession> session;
    AgentRestrictions session_limits;
};

// A frame's worth of an agent's input, and who gives it (the actions it
// performs are theirs: Runtime::set_input_actor).
struct AgentInput {
    std::vector<InputEvent> events;
    Actor                   actor;
    std::shared_ptr<const AgentSession> session;
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
    [[nodiscard]] std::size_t pending() const noexcept { return waits_.size() + deliveries_.size() + reloads_.size(); }
    // The next frame's events from `input` requests, oldest first: the host
    // takes one batch a frame (an empty batch is a frame with none); none
    // when nothing waits.
    [[nodiscard]] std::optional<AgentInput> take_input();

private:
    struct Wait {
        semantic::Condition                   condition;
        Value                                 re;
        std::chrono::steady_clock::time_point deadline;
        Reply                                 reply;
        Invocation                            call;
    };
    // An `input` request being delivered: its frames' events, what its
    // presses hit, and where the journal stood when it came.
    struct Delivery {
        Value                               re;
        Reply                               reply;
        Actor                               actor;
        std::deque<std::vector<InputEvent>> frames;
        std::size_t                         delivered = 0;
        std::size_t                         journal = 0;
        Value::List                         pressed;
        Invocation                          call;
    };
    // A `reload` sampling the motion it started (W2): the time held, stepped a
    // sample a frame, each picture against the one before, until two in a row
    // are the same or the samples run out.
    struct Reload {
        Value               re;
        Reply               reply;
        Value               answer;   // the reloader's: what it reloaded, the semantic changes
        mlibs::image::Image before;   // the window before the reload
        mlibs::image::Image previous; // the last sample
        Value::List         samples;
        double              every = 1.0 / 30, at = 0;
        int                 left = 12, still = 0;
        bool                held = false;  // the protocol holds the time (the agent did not already)
    };
    void ensure_session();
    void poll_reloads();
    [[nodiscard]] std::optional<Envelope> denied(const Invocation& call, Effect effect) const;
    void send(const Reply& reply, const Envelope& envelope, const Value& re) const;
    Envelope handle(const Value& req, const Value& re, const Reply& reply, bool& deferred);

    Runtime&          runtime_;
    semantic::Tree    tree_;
    ProtocolOptions   options_;
    std::shared_ptr<const AgentSession> session_;
    std::shared_ptr<AgentAccess> session_binding_;
    const AgentAccess* session_access_ = nullptr;
    std::vector<Wait> waits_;
    std::deque<Delivery> deliveries_;
    std::vector<Reload> reloads_;
};

// The record lines of an envelope, each with "re" when it is not null.
[[nodiscard]] std::vector<std::string> lines(const Envelope& envelope, const Value& re = {});

}  // namespace omui::agent
