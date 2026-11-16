// xengine.field.agent — the air as a subject of the agent kernel (D-173, D-174; the M6.5 design's T5.5): its
// state is a Field on mlibs.simulation's timeline; an agent reads the cells and the wind, applies a quantity at a
// place (once, or held: a fire that keeps burning), steps it, returns to a frame, asks what would be, and asks
// why a cell is what it is — every law judged, every flow named by the neighbour it came from. The same requests,
// the same bits.
//
//     field::agent::Air air(*field::Field::make(program, {.air = *program.material("air")}));
//     mlibs::agent::SubjectSession session({&air});
//     // → {"op":"air.set","at":[0.5,0.5,0.5],"channel":"temperature","value":900,"hold":true}
//     // → {"op":"air.step","steps":60}   → {"op":"air.explain","at":[0.5,1.5,0.5]}
//
// What it cannot do yet it says: from something it put out back to what made it (the record of causes across
// carriers, `world.explain`, comes with the world's subject), and an undo apart from returning to a frame.
export module xengine.field.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
import mlibs.simulation;
import xengine.field;
export import :catalog;

export namespace xengine::field::agent {

// A quantity applied to a cell at the start of a step, as stored.
struct Applied {
    std::uint32_t cell = 0, channel = 0, value = 0;
};

// The air on a timeline: a step applies what is held and what is applied once, then steps the field.
struct AirModel {
    struct State {
        Field       field;
        std::string failed;  // the step that could not run, said (the field stays as it was)
    };
    // At most `most_applied` quantities a step (the timeline keeps every frame's input by value).
    static constexpr std::size_t most_applied = 16;
    struct Input {
        std::array<Applied, most_applied> applied{};
        std::uint32_t                     count = 0;
    };
    static void step(State& state, const Input& input);
};

struct Options {
    std::int64_t keep = 1800;        // steps of history kept
    std::int64_t most_steps = 1800;  // a request's most
};

class Air final : public mlibs::agent::Subject {
public:
    explicit Air(Field initial, Options options = {});

    // One step under what is held and what is applied once (the owner's own way in: the step a request makes).
    void step();
    // A quantity held from the next step on (the owner's air.set with `hold`): a fire that keeps burning.
    void hold(Applied applied);
    [[nodiscard]] const Field& state() const noexcept { return timeline_.state().field; }
    [[nodiscard]] std::int64_t frame() const noexcept { return timeline_.frame(); }

    // ── the air as a subject ──────────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "air"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.field.air@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .budget = true, .control = true, .time = true, .causes = true, .provenance = false, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::field::agent::listing; }
    [[nodiscard]] mlibs::control::Guide guide() const noexcept override { return xengine::field::agent::guide; }
    [[nodiscard]] std::uint64_t state_hash() const override { return timeline_.state().field.state_hash(); }
    // Where its record of changes stands: the frame the air is at (`air.explain` answers the same).
    [[nodiscard]] std::string recorded_at() const override { return std::format("{{\"frame\":{}}}", timeline_.frame()); }
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

private:
    // What a request applies: the cell, the channel, the value as stored, and whether it is held.
    struct Setting {
        Applied applied;
        bool    hold = false;
    };
    [[nodiscard]] static std::expected<Setting, std::string> setting_of(const Field& field, const mlibs::json::Node& given);
    [[nodiscard]] AirModel::Input input(bool clear_once);
    [[nodiscard]] std::string observed(const Field& field, std::int64_t frame, std::optional<std::int64_t> budget) const;
    [[nodiscard]] std::string status() const;

    // A change on record: what was asked, by whom, over which frames (a set: the frame it was asked at, and what).
    struct Change {
        std::string            op, actor;
        std::int64_t           from = 0, to = 0;
        std::optional<Applied> applied;
        bool                   hold = false;
    };
    [[nodiscard]] std::string changes_for(std::uint32_t cell) const;

    Options                               options_;
    std::vector<Change>                   changes_;
    mlibs::simulation::Timeline<AirModel> timeline_;
    std::vector<Applied>                  held_, once_;
};

}  // namespace xengine::field::agent
