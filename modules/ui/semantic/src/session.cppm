// openmui.semantic:session — recording a session and replaying it exactly.
//
// What is recorded is what drives the interface: the frames with their time
// and input events, and the semantic actions with their actor and answer
// (plan §11: "semantic actions + clock"). Checkpoints store a digest of the
// semantic tree. Replaying on a fresh runtime with the same view, in
// headless mode and on the virtual clock, gives the same tree at every
// checkpoint, on every platform — a bug report a test can run.
export module openmui.semantic:session;

import std;
import openmui.core;
import openmui.view;
import openmui.session;
import :tree;

export namespace omui::semantic {

class Recorder {
public:
    // Fills the session's header from the runtime (size, scale, clock).
    Recorder(Tree& tree, session::Session& session);

    // Runs a frame and records it (its time, and its events if any).
    FrameOutput frame(const FrameInput& input = {});
    // Performs an action by id and records it, with its outcome.
    Envelope act(std::string_view id, std::string_view action, const Invocation& call = {});
    // Stores a digest of the tree now: replay checks it.
    void checkpoint(std::string_view label = {});
    void note(std::string_view text);

private:
    Tree&            tree_;
    session::Session& session_;
};

struct ReplayReport {
    std::size_t              frames = 0;
    std::size_t              actions = 0;
    std::size_t              checkpoints = 0;
    std::vector<std::string> mismatches;  // empty: the replay agrees with the recording
    std::uint64_t            digest = 0;  // of the tree at the end

    [[nodiscard]] bool ok() const noexcept { return mismatches.empty(); }
};

// Plays a session forward on `tree`'s runtime (already showing the recorded
// view): `advance_to` runs the entries up to a time not yet run, so a session
// can be stepped through (a debugger's time travel replays from the start).
class Player {
public:
    Player(Tree& tree, const session::Session& session);

    // Runs the entries with time <= `time`; answers what happened so far.
    const ReplayReport& advance_to(double time);
    const ReplayReport& finish() { return advance_to(std::numeric_limits<double>::infinity()); }
    [[nodiscard]] bool done() const noexcept;
    [[nodiscard]] double position() const noexcept { return position_; }
    [[nodiscard]] const ReplayReport& report() const noexcept { return report_; }

private:
    void run(const session::Entry& entry);

    Tree&                  tree_;
    const session::Session& session_;
    std::size_t            next_ = 0;
    double                 position_ = 0;
    ReplayReport           report_;
};

// Replays a whole session (or up to `until` seconds of it).
[[nodiscard]] ReplayReport replay(Tree& tree, const session::Session& session,
                                  double until = std::numeric_limits<double>::infinity());

}  // namespace omui::semantic
