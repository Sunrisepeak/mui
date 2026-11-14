// xengine.dynamics.rigid3.agent — a 3D rigid world as a subject of the agent kernel (D-173, D-174): its state is
// the world the rigid3 family (Jolt under this package's contract) steps; an agent reads its bodies and what
// touched what, casts a ray or a shape into it, pushes a body, drives a joint's motor, walks and jumps a character,
// steps it, returns to a frame, and asks what would be — the same requests, the same bits.
//
//     dynamics::rigid3::agent::Scene scene(std::move(*world));
//     mlibs::agent::SubjectSession session({&scene});
//     // → {"op":"rigid3.push","body":1,"impulse":[0,400,0]}   → {"op":"rigid3.step","steps":30}   → {"op":"rigid3.observe"}
//
// Time: the world is not a value to copy every step, so its history is a checkpoint (the world's image) every
// `every` steps and the inputs given since (pushes, motors driven, characters walked and jumped); a frame is reached
// again by restoring the checkpoint before it and playing the inputs again — the same bits, as the family's
// contract says.
//
// What it cannot do yet it says: why a body is where it is as a record of causes — who pushed it, which contact
// turned it — and from a body's place back to what moved it come with the kernels' record of causes (the
// plan's T1.11); the contacts a step reports are what touched what, which is not that.
export module xengine.dynamics.rigid3.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
import xengine.dynamics.rigid3;
export import :catalog;

export namespace xengine::dynamics::rigid3::agent {

struct Options {
    std::int64_t keep = 3600;        // steps of history kept
    std::int64_t every = 60;         // a checkpoint every this many steps
    std::int64_t most_steps = 3600;  // a request's most
};

class Scene final : public mlibs::agent::Subject {
public:
    // The world as it is: its step count is the first frame, the history's first checkpoint.
    explicit Scene(World world, Options options = {});

    // One step with the inputs given for it (the owner's own way in: the same step a request makes).
    void step();
    [[nodiscard]] const World& world() const noexcept { return world_; }
    [[nodiscard]] std::int64_t frame() const noexcept { return static_cast<std::int64_t>(world_.steps()); }
    [[nodiscard]] std::int64_t earliest() const noexcept { return checkpoints_.empty() ? frame() : checkpoints_.begin()->first; }

    // ── the world as a subject ────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "rigid3"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.dynamics.rigid3@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .budget = true, .control = true, .time = true, .causes = false, .provenance = false, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::dynamics::rigid3::agent::listing; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return xengine::dynamics::rigid3::agent::guide; }
    [[nodiscard]] std::uint64_t state_hash() const override { return world_.state_hash(); }
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    // What an agent gives the world, before the step from `frame`: an impulse on a body, a joint's motor driven,
    // a character's walk or its jump.
    struct Input {
        enum class Kind : std::uint8_t { push, drive, walk, jump } kind = Kind::push;
        std::int64_t frame = 0;
        BodyId body;
        Vec impulse;
        std::optional<Vec> at;
        JointId joint;
        Motor::Mode mode = Motor::Mode::off;
        float target = 0;
        CharacterId character;
        Vec velocity;
        float speed = 0;
    };
    static void give(World& world, const Input& input);
    [[nodiscard]] std::string observed(const World& world, std::optional<std::int64_t> budget) const;
    [[nodiscard]] std::string status() const;
    [[nodiscard]] bool seek(std::int64_t frame);

    Options options_;
    World world_;
    std::map<std::int64_t, std::vector<std::uint8_t>> checkpoints_;  // frame → the world's image
    std::vector<Input> given_;                                       // the inputs since the earliest checkpoint, in order
    std::vector<Input> pending_;                                     // for the next step
};

}  // namespace xengine::dynamics::rigid3::agent
