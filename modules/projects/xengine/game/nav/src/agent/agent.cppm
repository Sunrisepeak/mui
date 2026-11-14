// xengine.nav.agent — a navigation world as a subject of the agent kernel (D-173, D-174): its state is the world
// (the mesh, its obstacles and links, the crowd); an agent builds it from triangles, asks paths, rays and the
// nearest place, adds walkers and sends them, places obstacles and links, steps it, returns to a frame and asks
// what would be — the same requests, the same bits.
//
//     xengine::nav::agent::Scene scene(std::move(*world));
//     mlibs::agent::SubjectSession session({&scene});
//     // → {"op":"nav.add","at":[-5,0,-6]}   → {"op":"nav.target","walker":1,"to":[5,0,-5]}   → {"op":"nav.step","steps":60}
//
// Time: the world's history is a checkpoint (its image) every `every` steps and the inputs given since (a build,
// walkers added, removed and sent, obstacles and links placed and taken); a frame is reached again by restoring
// the checkpoint before it and giving the inputs again — the same bits, as the world contract says.
//
// What it cannot do yet it says: why a walker went where it went as a record of causes — who sent it, which
// walker made it step aside, why its path was planned again — comes with the kernels' record of causes (the
// plan's T1.11).
export module xengine.nav.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
import xengine.nav;
export import :catalog;

export namespace xengine::nav::agent {

struct Options {
    std::int64_t keep = 3600;        // steps of history kept
    std::int64_t every = 60;         // a checkpoint every this many steps
    std::int64_t most_steps = 3600;  // a request's most
};

class Scene final : public mlibs::agent::Subject {
public:
    // The world as it is: its step count is the first frame, the history's first checkpoint.
    explicit Scene(World world, Options options = {});

    // One step (the owner's own way in: the same step a request makes).
    void step();
    [[nodiscard]] const World& world() const noexcept { return world_; }
    [[nodiscard]] std::int64_t frame() const noexcept { return static_cast<std::int64_t>(world_.steps()); }
    [[nodiscard]] std::int64_t earliest() const noexcept { return checkpoints_.empty() ? frame() : checkpoints_.begin()->first; }

    // ── the world as a subject ────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "nav"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.nav@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .budget = true, .control = true, .time = true, .causes = false, .provenance = false, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::nav::agent::listing; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return xengine::nav::agent::guide; }
    [[nodiscard]] std::uint64_t state_hash() const override { return world_.state_hash(); }
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

    // What an agent gives the world at a frame, before the step from it: given at once, and again when the frame
    // is played again.
    struct Input {
        enum class Kind : std::uint8_t { build, add, remove, target, stop, place, take, join, unjoin } kind = Kind::add;
        std::int64_t frame = 0;
        std::shared_ptr<const Ground> ground;  // build
        Walker size;                           // build
        WalkerDef walker;                      // add
        std::uint32_t id = 0;                  // remove, target, stop, take, unjoin
        Vec to;                                // target
        Obstacle obstacle;                     // place
        Link link;                             // join
    };

private:
    static std::expected<std::uint32_t, std::string> give(World& world, const Input& input);
    [[nodiscard]] std::string observed(const World& world) const;
    [[nodiscard]] std::string status() const;
    [[nodiscard]] bool seek(std::int64_t frame);
    // Given now and kept for the frame's replay; the answer's text, or why it was refused.
    std::expected<std::uint32_t, std::string> apply(Input input);

    Options options_;
    World world_;
    std::map<std::int64_t, std::vector<std::uint8_t>> checkpoints_;  // frame → the world's image
    std::vector<Input> given_;                                       // the inputs since the earliest checkpoint, in order
};

}  // namespace xengine::nav::agent
