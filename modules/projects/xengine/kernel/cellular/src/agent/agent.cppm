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
// Who changed a cell is on record (D-169): every edit with the actor the
// session knows, kept beside the history and dropped with the branch a seek
// leaves; field.explain says a cell's edits and whether the program changed it
// in the last step. A witness (the record of causes of a field of matter:
// xengine.cellular.matter.agent) is told each frame at its commit — nothing is
// recorded in a step's path. What it cannot do it says: which rule wrote a
// cell (the source of what it puts out) is not here for plain rules.
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

// What one frame does: the program runs a step, or channels are written over a region — each set to a
// value, or raised or lowered by one (saturating).
enum class EditKind : std::uint8_t { step, set, add, take };
inline constexpr std::array<std::string_view, 4> edit_kind_names{"step", "set", "add", "take"};
struct Edit {
    EditKind      kind = EditKind::step;
    std::uint32_t count = 0;                             // how many channels it writes
    std::array<std::uint32_t, 16> channels{}, values{};  // which, and the value each
    Tile          region;

    // One channel set to a value over a region.
    [[nodiscard]] static constexpr Edit place(std::uint32_t channel, std::uint32_t value, Tile region) noexcept {
        Edit edit{.kind = EditKind::set, .count = 1, .region = region};
        edit.channels[0] = channel;
        edit.values[0] = value;
        return edit;
    }
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
    std::size_t  edits_kept = 256;  // the record of who edited what: the last so many
};

// An edit on record: what was written, at which frame, by whom.
struct Edited {
    std::int64_t  frame = 0;  // the frame it made
    Edit          edit;
    openas::Actor actor;
};

// Who is told what each frame did, at its commit — where a record of causes is made (D-169); a step's own
// path records nothing. Its calls come between requests, never during a step.
class Witness {
public:
    virtual ~Witness() = default;
    // A step ran: the field after it, its cells of a step ago (the step's input, channel-major), and its
    // activity bytes as they were before the step (a tile asleep then was not run: its cells are the input's).
    virtual void stepped(const FieldState& now, std::span<const std::uint32_t> before, std::span<const std::uint8_t> activity_before, std::int64_t frame) = 0;
    virtual void edited(const FieldState& now, const Edited& edit) = 0;
    // The field returned to `frame`: what happened after it is another branch's.
    virtual void rewound(std::int64_t frame) = 0;
};

class Field final : public mlibs::agent::Subject {
public:
    // A field of `shape` run by `program`; its cells channel-major, all zero when none are given.
    [[nodiscard]] static std::expected<Field, std::string> make(Program program, Shape shape, std::vector<std::uint32_t> cells = {}, Options options = {});

    // The owner's own way in: the same frames a request makes. An edit is recorded with who made it (the
    // owner's program, unless said); one that names a channel or cells the field has not is not made.
    void step();
    bool edit(const Edit& edit, const openas::Actor& actor = owner());
    bool place(std::uint32_t channel, std::uint32_t value, Tile region, const openas::Actor& actor = owner()) { return edit(Edit::place(channel, value, region), actor); }
    // Back to a frame within the history kept: what is run from there is a new branch, and the edits and
    // whatever a witness recorded after it are dropped. False outside the history.
    bool seek(std::int64_t frame);
    [[nodiscard]] const FieldState& state() const noexcept { return timeline_.state(); }
    [[nodiscard]] std::int64_t frame() const noexcept { return timeline_.frame(); }
    [[nodiscard]] std::int64_t earliest() const noexcept { return timeline_.earliest(); }
    [[nodiscard]] std::int64_t latest() const noexcept { return timeline_.latest(); }
    [[nodiscard]] const Options& options() const noexcept { return options_; }
    // The edits on record, oldest first.
    [[nodiscard]] std::span<const Edited> edits() const noexcept { return edits_; }
    // Told each frame from now on (one at a time; nullptr: none). Not owned: it outlives the field's use of it.
    void watch(Witness* witness) noexcept { witness_ = witness; }
    // Who an edit is by when the owner makes it itself.
    [[nodiscard]] static const openas::Actor& owner() noexcept;
    // One frame on a state that is not the field's (a copy run ahead): the same step.
    static void advance(FieldState& state, const Edit& edit) noexcept { FieldModel::step(state, edit); }

    // ── the field as a subject ────────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "field"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.cellular.field@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        return {.observation = true, .budget = true, .control = true, .time = true, .causes = true, .provenance = false, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::cellular::agent::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return hash_of(timeline_.state()); }
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

    [[nodiscard]] static std::uint64_t hash_of(const FieldState& state) noexcept;

    // A region a request names ([x, y, width, height]), within the field; the whole field when it names none
    // and need not.
    [[nodiscard]] std::expected<Tile, std::string> region_of(mlibs::json::Node node, std::string_view name, bool required) const;

private:
    Field(const FieldState& initial, Options options) : options_(options), timeline_(initial, options.keep, 60) {}
    [[nodiscard]] std::string observed(const FieldState& state, std::int64_t frame, Tile region, std::optional<std::int64_t> budget) const;
    [[nodiscard]] std::string status() const;

    Options                                 options_;
    mlibs::simulation::Timeline<FieldModel> timeline_;
    std::vector<Edited>                     edits_;
    Witness*                                witness_ = nullptr;
    std::vector<std::uint8_t>               activity_before_;  // for a witness: the tiles' bytes a step began with
};

}  // namespace xengine::cellular::agent
