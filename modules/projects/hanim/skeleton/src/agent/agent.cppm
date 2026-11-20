// hanim.skeleton.agent — a crowd of skeletal characters as a subject of the agent kernel (D-173, D-174): its state
// is every character's (its machine, its layers, where it stands); an agent reads the rig and the characters,
// sends one to a state or sets its parameters, layers a clip on part of it, asks where a joint is or what any clip
// is at any time, steps them, returns to a frame and asks what would be — the same requests, the same bits.
//
//     hanim::skeleton::agent::Scene scene(hanim::skeleton::cts::crowd(8));
//     mlibs::agent::SubjectSession session({&scene});
//     // → {"op":"skeleton.set","character":0,"parameter":"speed","value":3}   → {"op":"skeleton.step","steps":60}
//
// Time: a checkpoint (the crowd's image) every `every` steps and the inputs given since; a frame is reached again
// by restoring the checkpoint before it and giving the inputs again — the same bits, as the world contract says.
//
// Why a character is in its state is answered by observe: the last transition, when, and the conditions it
// asked. A record of every cause (who set the parameter that let it happen) comes with the kernels' record of
// causes (the plan's T1.11).
export module hanim.skeleton.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
import hanim.skeleton;
export import :catalog;

export namespace hanim::skeleton::agent {

struct Options {
    std::int64_t keep = 3600;        // steps of history kept
    std::int64_t every = 60;         // a checkpoint every this many steps
    std::int64_t most_steps = 3600;  // a request's most
};

class Scene final : public mlibs::agent::Subject {
public:
    explicit Scene(Crowd crowd, Options options = {});

    void step();
    [[nodiscard]] const Crowd& crowd() const noexcept { return crowd_; }
    [[nodiscard]] std::int64_t frame() const noexcept { return static_cast<std::int64_t>(crowd_.steps()); }
    [[nodiscard]] std::int64_t earliest() const noexcept { return checkpoints_.empty() ? frame() : checkpoints_.begin()->first; }

    // ── the crowd as a subject ────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "skeleton"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "hanim.skeleton@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .budget = true, .control = true, .time = true, .causes = false, .provenance = false, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return hanim::skeleton::agent::listing; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return hanim::skeleton::agent::guide; }
    [[nodiscard]] std::uint64_t state_hash() const override { return crowd_.state_hash(); }
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

    // What an agent gives a character at a frame, before the step from it: given at once, and again when the frame
    // is played again.
    struct Input {
        enum class Kind : std::uint8_t { play, set, blend } kind = Kind::play;
        std::int64_t  frame = 0;
        std::uint32_t character = 0;
        std::string   name;  // play: the state; set: the parameter
        float         value = 0;  // play: the fade; set: the value
        std::size_t   layer = 0;
        Layer         blend;
    };

private:
    static std::expected<void, Error> give(Crowd& crowd, const Input& input);
    [[nodiscard]] std::string observed(const Crowd& crowd, std::optional<std::int64_t> budget) const;
    [[nodiscard]] std::string status() const;
    [[nodiscard]] bool seek(std::int64_t frame);
    std::expected<void, Error> apply(Input input);

    Options options_;
    Crowd crowd_;
    std::map<std::int64_t, std::vector<std::uint8_t>> checkpoints_;
    std::vector<Input> given_;
};

}  // namespace hanim::skeleton::agent
