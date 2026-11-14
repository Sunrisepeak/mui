// xengine.dynamics.agent — a soft body as a subject of the agent kernel
// (D-173, D-174): its state is the body the XPBD family steps; an agent reads
// where it is, how squashed, how fast and what it touches, pushes it, steps
// it, returns to a frame, and asks what would be — the same requests, the
// same bits.
//
//     dynamics::agent::Body body(*dynamics::SoftBody::ring(points, params, planes));
//     mlibs::agent::SubjectSession session({&body});
//     // → {"op":"body.push","kick":[0,-400]}   → {"op":"body.step","steps":30}   → {"op":"body.observe"}
//
// What it cannot do yet it says: why the body is where it is as a record of
// causes — who pushed it, which contact turned it — and from a particle back
// to what moved it come with the kernels' record of causes (the plan's
// T1.11); `body.contacts` says what the last step touched, which is not that.
export module xengine.dynamics.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.simulation;
import mlibs.json;
import xengine.dynamics.xpbd;
export import :catalog;

export namespace xengine::dynamics::agent {

struct Options {
    // What the owner holds on every step: the ledges it stands among, a pin. A request's push is added to it.
    BodyInput    input;
    std::int64_t keep = 3600;        // steps of history kept
    std::int64_t most_steps = 3600;  // a request's most
};

class Body final : public mlibs::agent::Subject {
public:
    explicit Body(const SoftBody& initial, Options options = {});

    // One step under what is held (the owner's own way in: the same step a request makes).
    void step();
    [[nodiscard]] const SoftBody& state() const noexcept { return timeline_.state(); }
    [[nodiscard]] std::int64_t frame() const noexcept { return timeline_.frame(); }

    // ── the body as a subject ─────────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "body"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.dynamics.body@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .budget = true, .control = true, .time = true, .causes = false, .provenance = false, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::dynamics::agent::listing; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return xengine::dynamics::agent::guide; }
    [[nodiscard]] std::uint64_t state_hash() const override { return hash_of(timeline_.state()); }
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

    [[nodiscard]] static std::uint64_t hash_of(const SoftBody& body) noexcept;

private:
    // What a request pushes with: a force and a walk held, a kick once.
    struct Push {
        double force_x = 0, force_y = 0, walk = 0, kick_x = 0, kick_y = 0;
    };
    [[nodiscard]] static std::expected<Push, std::string> push_of(const mlibs::agent::Request& request, Push held);
    [[nodiscard]] BodyInput input_of(const Push& push) const noexcept;
    [[nodiscard]] std::string observed(const SoftBody& body, std::int64_t frame, std::optional<std::int64_t> budget) const;
    [[nodiscard]] std::string status() const;

    Options                                    options_;
    mlibs::simulation::Timeline<SoftBodyModel> timeline_;
    Push                                       held_;
};

}  // namespace xengine::dynamics::agent
