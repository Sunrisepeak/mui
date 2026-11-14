// xengine.particles.agent — a cloud of particles as a subject of the agent kernel (D-173, D-174): an agent reads
// what is in flight (where, of what, how hot) and what struck the world, finds those near a place, emits more,
// steps the cloud, returns to a frame and asks what would be — the same requests, the same bits.
//
//     particles::agent::Scene scene(std::move(*cloud), world);      // `world`: the owner's surfaces, kept alive by it
//     mlibs::agent::SubjectSession session({&scene});
//     // → {"op":"particles.emit","at":[0,1,0],"velocity":[0,3,0],"material":"iron","kelvin":1500,"count":20,"spread":1}
//     // → {"op":"particles.step","steps":60}   → {"op":"particles.observe"}
//
// Time: a cloud is a value, so its history is a copy every `every` steps and the emissions given since; a frame
// is reached again by the copy before it and the emissions played again — the same bits (the cloud's own
// determinism). An emission's spread is a number of (frame, its place among the frame's emissions, the
// particle's), so a branch and a replay spread it alike.
//
// What it cannot do yet it says: why a particle is as it is as a record of causes (which law cooled it, which
// strike turned it) comes with the kernels' record of causes (the plan's T1.11); `events` counts what the laws did.
export module xengine.particles.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
export import xengine.particles;
export import :catalog;

export namespace xengine::particles::agent {

struct Options {
    std::int64_t keep = 3600;        // steps of history kept
    std::int64_t every = 60;         // a copy of the cloud every this many steps
    std::int64_t most_steps = 3600;  // a request's most
};

class Scene final : public mlibs::agent::Subject {
public:
    // The cloud as it is (its steps are the first frame) among the owner's surfaces, which outlive the scene.
    Scene(Cloud cloud, std::span<const Surface> world, Options options = {});

    // One step with the emissions given for it (the owner's own way in: the same step a request makes).
    [[nodiscard]] std::expected<void, std::string> step();
    [[nodiscard]] const Cloud& cloud() const noexcept { return cloud_; }
    [[nodiscard]] std::int64_t frame() const noexcept { return static_cast<std::int64_t>(cloud_.steps()); }
    [[nodiscard]] std::int64_t earliest() const noexcept { return copies_.empty() ? frame() : copies_.begin()->first; }

    // ── the cloud as a subject ────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "particles"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.particles@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .budget = true, .control = true, .time = true, .causes = false, .provenance = false, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::particles::agent::listing; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return xengine::particles::agent::guide; }
    [[nodiscard]] std::uint64_t state_hash() const override { return cloud_.fingerprint(); }
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    // An emission an agent gave, before the step from `frame`; `order` its place among that frame's.
    struct Emission {
        std::int64_t  frame = 0;
        std::uint32_t order = 0, count = 1;
        Spawn         spawn;
        float         spread = 0;
    };
    [[nodiscard]] std::expected<void, std::string> advance(Cloud& cloud, std::span<const Emission> emissions) const;
    [[nodiscard]] std::string observed(const Cloud& cloud, std::optional<std::int64_t> budget) const;
    [[nodiscard]] std::string particle_json(const Cloud& cloud, std::size_t particle) const;
    [[nodiscard]] std::string status() const;
    [[nodiscard]] bool seek(std::int64_t frame);

    Options                                options_;
    Cloud                                  cloud_;
    std::span<const Surface>               world_;
    std::optional<std::uint32_t>           temperature_;
    std::map<std::int64_t, Cloud>          copies_;   // frame → the cloud then
    std::vector<Emission>                  given_;    // since the earliest copy, in order
    std::vector<Emission>                  pending_;  // for the next step
    mutable std::vector<Spawn>             spawns_;   // room a step reuses
};

}  // namespace xengine::particles::agent
