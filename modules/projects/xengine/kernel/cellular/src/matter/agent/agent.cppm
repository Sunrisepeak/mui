// xengine.cellular.matter.agent — a field of matter as a subject of the agent kernel (D-169, D-173, D-174;
// the package's feature `matter-agent`): the lattice's field in matter's own words. An agent reads a region by
// material and in units, places a material, applies a quantity, and asks why a cell is what it is — the law that
// held, what it read, which neighbours gave what it needed and why they could.
//
//     auto world = cellular::matter_agent::World::make(*matter::standard(), {64, 48}, "air");
//     mlibs::agent::SubjectSession session({world->field.get(), world->matter.get()});
//     // → {"op":"matter.place","material":"wood","region":[10,40,12,4]}
//     // → {"op":"matter.apply","channel":"temperature","set":900,"region":[10,43,1,1]}
//     // → {"op":"field.step","steps":300}          (time is the field's)
//     // → {"op":"matter.explain","cell":[14,42]}   → it ignited at step 212: `burns` held, having read
//     //    temperature 571 K ≥ ignition 570 K; its heat came from [13,42], wood alight since step 170 …
//
// The record of causes is made at each frame's commit (the field's Witness): after a step, each cell is
// compared with the cell it was a step ago — where it was then, by the lattice's `origins`, since a cell may
// change and change places in one step — and those whose material or named state changed are judged again by
// the matter kernel's `why` on the step's input: what happened is kept with what the law read and who gave it.
// Nothing is recorded in a step's path; with no one watching a field costs what it did.
export module xengine.cellular.matter.agent;

import std;
import openas;
import mlibs.agent;
import mlibs.control;
import mlibs.json;
export import xengine.cellular.agent;
export import xengine.cellular.matter;
export import :catalog;

export namespace xengine::cellular::matter_agent {

// What a record is of: a cell became another material, a channel that names its events left 0 (rose: `ignite`)
// or returned to it (fell: `go_out`), an actor placed a material or applied a quantity. And what a lattice that
// is a world's says besides (agent::Wrote): an actor's blast, a cell thrown out of the lattice, a cell landed in
// it again, a cell that left a body, a body that came loose of what it rested on.
enum class RecordKind : std::uint8_t { became, rose, fell, placed, applied, blast, thrown, landed, left, dropped };

// The cell beyond the field's edge, where a record names what is outside (a bath).
inline constexpr std::uint32_t outside = std::numeric_limits<std::uint32_t>::max();

// What a law read, as stored.
struct Read {
    xengine::matter::Source source = xengine::matter::Source::constant;
    std::uint32_t           index = 0, value = 0;
};
// A neighbour's part in what happened. gave: a flow of a channel toward the cell, or its part of a near sum,
// where the law needed more of it (heat to ignite, oxygen to burn). took: a flow away from the cell, where
// less made it so (the water that took a fire's heat; what lava gave its heat to as it set). beside: what was
// next to it when a near sum fell short (the water over a fire that has no air). With what the neighbour was
// then, and which of its named states were on.
enum class Part : std::uint8_t { gave, took, beside };
struct Gave {
    Part          part = Part::gave;
    std::uint32_t cell = outside;
    // channel: a flow of it toward the cell; near_property, near_channel: its part of that near sum.
    xengine::matter::Source source = xengine::matter::Source::channel;
    std::uint32_t index = 0;          // the channel or column
    std::uint32_t amount = 0;         // beside: its part of the sum, which may be 0
    std::uint32_t material = 0;
    std::uint32_t states = 0;         // a bit a channel: its named states that were not 0
};
struct Record {
    std::int64_t  frame = 0, step = 0;  // the field's frame it was made at, and the steps run then
    RecordKind    kind = RecordKind::became;
    std::uint32_t cell = 0;             // where: y × width + x (an edit: its region's first cell)
    std::uint32_t law = xengine::matter::none;  // the law that held (rose, became) or stopped holding (fell)
    std::uint32_t failed = 0;           // fell: the comparison of the law that no longer holds
    std::uint32_t channel = 0;          // rose, fell, applied
    // became: materials; rose, fell: the channel's values; placed: `to` the material; applied: `to` the amount;
    // thrown: `from` the material, `to` by what (agent::Wrote::By); landed: `to` the material, `from` the cell
    // it was thrown out of; left: `from` the material.
    std::uint32_t from = 0, to = 0;
    std::int64_t  since = 0;            // landed: the frame it was thrown at
    std::uint64_t body = 0;             // left: the body it was of; dropped: the body
    std::uint64_t support = 0;          // dropped: the body it had rested on
    std::array<Read, 8> reads{};
    std::array<Gave, 4> gave{};
    std::uint8_t  read_count = 0, gave_count = 0;
    // An edit.
    Tile          region;
    agent::EditKind edit = agent::EditKind::step;
    std::uint32_t actor = 0;            // which of the actors on record
};

struct Options {
    agent::Options field{.boundary = nothing_beyond};  // or open_to(outside): `outside` the caller's, alive as long as the field
    Lowering       lowering;
    std::size_t    records_kept = 16384;  // the record of causes: the last so many
    std::int64_t   most_steps = 4096;     // matter.branch's most
};

class Matter final : public mlibs::agent::Subject, public agent::Witness {
public:
    // The matter subject of `field` — a field of its own, or a world's lattice — which `program` lowered with
    // `lowering` steps. It asks the lattice to tell it each frame (among whoever else watches); the lattice
    // outlives it.
    Matter(std::shared_ptr<const xengine::matter::Program> program, agent::Lattice& field, Lowering lowering = {}, std::size_t records_kept = 16384,
           std::int64_t most_steps = 4096);
    ~Matter() override;
    Matter(const Matter&) = delete;
    Matter& operator=(const Matter&) = delete;

    [[nodiscard]] const xengine::matter::Program& program() const noexcept { return *program_; }
    // What is on record, oldest first.
    [[nodiscard]] std::span<const Record> records() const noexcept { return records_; }
    [[nodiscard]] std::span<const openas::Actor> actors() const noexcept { return actors_; }
    // What a record is of, by name: the event its law or its channel names (`ignite`, `boil`, `burn_out`), else
    // `became`, `rose`, `fell`; an edit: `placed`, `applied`. What matter.events filters by.
    [[nodiscard]] std::string happened_as(const Record& record) const;
    // One cell's step from `cells` (the field's state, channel-major), said by the matter kernel: the cell and
    // what is beside it as a carrier of their own. `beside[k]` is the field's cell that instance k + 1 is, or
    // `outside`.
    [[nodiscard]] xengine::matter::Why why(std::span<const std::uint32_t> cells, std::uint32_t cell, std::vector<std::uint32_t>* beside = nullptr) const;

    // The same edits from code — a host's, a game's — each a frame, on record with who made it as an agent's
    // are. A material placed: every channel of the region as a new instance of it has. A quantity applied: one
    // channel set to, raised by or lowered by `amount` as stored (its unit × 2^fraction). False: no such
    // material or channel, the material's own channel, or a region the field does not have.
    bool place(xengine::matter::MaterialId material, Tile region, const openas::Actor& actor = agent::Field::owner());
    bool apply(std::uint32_t channel, agent::EditKind kind, std::uint32_t amount, Tile region, const openas::Actor& actor = agent::Field::owner());

    // ── as a subject ────────────────────────────────────────────────────────
    [[nodiscard]] std::string_view name() const noexcept override { return "matter"; }
    [[nodiscard]] std::string_view interface_name() const noexcept override { return "xengine.matter.lattice@0.1"; }
    [[nodiscard]] mlibs::agent::Capabilities capabilities() const noexcept override {
        // Time is the field's (field.step, field.seek): one clock, said once.
        return {.observation = true, .budget = true, .control = true, .time = false, .causes = true, .provenance = true, .preview = true, .history = false};
    }
    [[nodiscard]] std::span<const mlibs::control::CatalogEntry> operations() const noexcept override { return catalog; }
    [[nodiscard]] std::string_view listing() const noexcept override { return xengine::cellular::matter_agent::listing; }
    [[nodiscard]] std::uint64_t state_hash() const override { return field_->lattice_hash(); }
    void operate(std::string_view operation, const mlibs::agent::Request& request, mlibs::agent::Answer& answer) override;

    // ── as the field's witness ──────────────────────────────────────────────
    void stepped(const agent::FieldState& now, std::span<const std::uint32_t> before, std::span<const std::uint8_t> activity_before, std::int64_t frame) override;
    void edited(const agent::FieldState& now, const agent::Edited& edit) override;
    void rewound(std::int64_t frame) override;
    void wrote(const agent::FieldState& now, std::span<const agent::Wrote> what, std::int64_t frame, std::span<const openas::Actor> actors) override;

private:
    [[nodiscard]] std::optional<agent::Edit> placing(xengine::matter::MaterialId material, Tile region) const;
    // What a step from `before` did, as records (appended to `out`): the same judgement a commit makes, also
    // for a state that is not the field's (a branch run ahead). The tiles `activity_before` says slept are not
    // looked at: nothing happened in them.
    void judge(const agent::FieldState& now, std::span<const std::uint32_t> before, std::span<const std::uint8_t> activity_before, std::int64_t frame,
               std::vector<Record>& out) const;
    void keep(Record record);
    [[nodiscard]] std::uint32_t actor_of(const openas::Actor& actor);
    [[nodiscard]] std::string said(const Record& record, std::span<const Record> among, int depth) const;

    std::shared_ptr<const xengine::matter::Program> program_;
    agent::Lattice*            field_;
    Lowering                   lowering_;
    std::size_t                records_kept_;
    std::int64_t               most_steps_;
    std::vector<Record>        records_;
    std::vector<openas::Actor> actors_;
    std::vector<std::uint32_t> event_channels_;  // the channels that name their events
    mutable std::vector<std::uint32_t> from_;    // scratch: where each cell of a step's output was in its input
    // Where what landed is now: the cell it is in → its landing (the cell and the frame on record), followed as
    // the laws move it. A body that carries it, or its being thrown again, ends the trail.
    struct Landing {
        std::uint32_t cell = 0;
        std::int64_t  frame = 0;
    };
    std::unordered_map<std::uint32_t, Landing>      landings_;
    std::vector<std::pair<std::uint32_t, Landing>>  moved_;  // a step's scratch
};

// A field of matter and its two subjects together: `field` keeps time, `matter` speaks of what it is made of.
struct World {
    std::unique_ptr<agent::Field> field;
    std::unique_ptr<Matter>       matter;

    World() = default;
    World(World&&) noexcept = default;
    // Another world in this one's place: the matter subject goes before the field it watches.
    World& operator=(World&& other) noexcept {
        matter.reset();
        field = std::move(other.field);
        matter = std::move(other.matter);
        return *this;
    }
    ~World() { matter.reset(); }

    // A world of `shape` cells of the material `filled`, stepped by `program` lowered as `options.lowering` says.
    [[nodiscard]] static std::expected<World, std::string> make(xengine::matter::Program program, Shape shape, std::string_view filled, Options options = {});
};

}  // namespace xengine::cellular::matter_agent
