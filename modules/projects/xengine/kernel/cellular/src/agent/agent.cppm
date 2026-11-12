// xengine.cellular.agent — a field of cells as a subject of the agent kernel
// (D-173, D-174): its state is the cells and the steps run; an agent reads a
// region as numbers (not the cells dumped), places values, steps it, returns
// to a frame, and asks what would be — the same requests, the same bits.
//
//     auto field = cellular::agent::Field::make(std::move(program), {256, 256});
//     mlibs::agent::SubjectSession session({&*field});
//     // → {"op":"field.place","channel":0,"value":1,"region":[100,0,8,8]}
//     // → {"op":"field.step","steps":60}   → {"op":"field.observe","region":[0,200,256,56]}
//
// A frame is one thing done to the field: a step of the program, or a value
// placed. Both are recorded (mlibs.simulation's timeline), so any frame kept
// is reached again exactly. A step runs the tiles that are awake (the
// kernel's sparse execution: the same bits as running all); every answer
// that says where the field is says how many tiles sleep.
//
// What it cannot do yet it says: why a cell holds what it holds (causes) and
// which rule wrote it (the source of what it puts out) come with the matter
// kernel's record of causes (the plan's T1.11).
export module xengine.cellular.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.simulation;
import mlibs.json;
import xengine.cellular.api;
export import :catalog;

export namespace xengine::cellular::agent {

// A field's whole state: plain data, copied whole.
struct FieldState {
    std::shared_ptr<const Program> program;   // what runs it; shared by every copy
    Shape                          shape;
    Boundary                       boundary;
    std::vector<std::uint32_t>     cells;     // channel-major: [channel][y][x]
    std::vector<std::uint32_t>     next;      // a step's output, swapped in: the cells of a step ago, which a tile asleep keeps
    std::int64_t                   steps = 0; // how many steps the program ran (a placement is a frame, not a step)
    Shape                          tile{32, 32};
    std::vector<std::uint8_t>      activity;  // a byte a tile (Execution::activity): what is at rest is not run
};

// What one frame does: the program runs a step, or a value is placed.
struct Edit {
    std::uint32_t place = 0;  // 0: a step
    std::uint32_t channel = 0, value = 0;
    Tile          region;
};

struct FieldModel {
    using State = FieldState;
    using Input = Edit;
    static void step(State& state, const Input& input) noexcept;
};

struct Options {
    Boundary     boundary;
    Shape        tile{32, 32};      // the tiles a step runs or leaves asleep
    std::int64_t keep = 240;        // frames of history kept
    std::int64_t most_steps = 4096; // a request's most
    std::size_t  most_cells = 4096; // field.cells' most a request
};

class Field final : public mlibs::agent::Subject {
public:
    // A field of `shape` run by `program`; its cells channel-major, all zero when none are given.
    [[nodiscard]] static std::expected<Field, std::string> make(Program program, Shape shape, std::vector<std::uint32_t> cells = {}, Options options = {});

    // The owner's own way in: the same frames a request makes.
    void step() { timeline_.step({}); }
    void place(std::uint32_t channel, std::uint32_t value, Tile region) { timeline_.step({1, channel, value, region}); }
    [[nodiscard]] const FieldState& state() const noexcept { return timeline_.state(); }
    [[nodiscard]] std::int64_t frame() const noexcept { return timeline_.frame(); }

    // ── the field as a subject ────────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "field"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.cellular.field@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .budget = true, .control = true, .time = true, .causes = false, .provenance = false, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::cellular::agent::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return hash_of(timeline_.state()); }
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

    [[nodiscard]] static std::uint64_t hash_of(const FieldState& state) noexcept;

private:
    Field(const FieldState& initial, Options options) : options_(options), timeline_(initial, options.keep, 60) {}
    // A region a request names, within the field; the whole field when it names none.
    [[nodiscard]] std::expected<Tile, std::string> region_of(mlibs::json::Node node, std::string_view name, bool required) const;
    [[nodiscard]] std::string observed(const FieldState& state, std::int64_t frame, Tile region, std::optional<std::int64_t> budget) const;
    [[nodiscard]] std::string status() const;

    Options                                 options_;
    mlibs::simulation::Timeline<FieldModel> timeline_;
};

}  // namespace xengine::cellular::agent
