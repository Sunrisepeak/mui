// xengine.cellular.rigid.agent — the lattice of a world of matter as what the matter subject stands on (the
// package's feature `world-agent`; the foundation plan's §6.10.1): the same `matter.observe`, `matter.explain`
// and `matter.branch` an agent has of a field of its own, on a world whose cells also fly and are bodies'.
//
//     auto world = *rigid::MatterWorld::make(program, options);
//     rigid_agent::WorldLattice lattice(*world);
//     matter_agent::Matter matter(shared_program, lattice, {.seed = world->seed()});
//     lattice.by(player);                 // who what is done to the world from now is by
//     world->blast({12, 33}, 5, 22);      // the game's own code, as it always was
//     world->step(), lattice.stepped(*world);
//     // → {"op":"matter.explain","cell":[27,35]}
//     //   landed, from [11,32] thrown at frame 21 ← thrown by a blast ← the blast, by the player
//     // → {"op":"matter.explain","cell":[40,31]}   (a cell of the crate that stood on the beam)
//     //   body: dropped at frame 431, its support the beam ← the beam's cells that left it ← burned out
//
// Time is the game's: the lattice keeps no clock and no history. Its owner steps the world and says so
// (`stepped`), and says when it went back (`rewound`). What the record is made from is the world's journal,
// which the lattice asks the world to keep; a world no lattice watches costs what it did.
export module xengine.cellular.rigid.agent;

import std;
import openas;
export import xengine.cellular.agent;
export import xengine.cellular.rigid;

export namespace xengine::cellular::rigid_agent {

class WorldLattice final : public agent::Lattice {
public:
    // Of `world`, which it asks to keep its journal. The world outlives it, or is replaced by `stepped`.
    explicit WorldLattice(rigid::MatterWorld& world);
    WorldLattice(const WorldLattice&) = delete;
    WorldLattice& operator=(const WorldLattice&) = delete;

    // The world made a step: its witnesses are told what the frame did — what was done to it since the step
    // before, what the laws did, what flew, landed and left a body. `world` is the world now: the same one, or
    // the copy a game's state holds this frame.
    void stepped(rigid::MatterWorld& world);
    // The world is at an earlier frame (a seek, an image restored): what was on record after it is another branch's.
    void rewound(rigid::MatterWorld& world);
    // Who what is done to the world from now is by, until said again: the journal's edits carry it. As made:
    // the owner.
    void by(const openas::Actor& actor);

    [[nodiscard]] const agent::FieldState& lattice_state() const noexcept override { return state_; }
    [[nodiscard]] std::int64_t lattice_frame() const noexcept override { return static_cast<std::int64_t>(world_->frames()); }
    // Made at once, between frames (a world's edit is not a frame), and on record at once.
    bool lattice_edit(const agent::Edit& edit, const openas::Actor& actor) override;
    void lattice_watch(agent::Witness* witness) override;
    void lattice_unwatch(agent::Witness* witness) noexcept override { std::erase(witnesses_, witness); }
    [[nodiscard]] std::uint64_t lattice_hash() const override { return world_->state_hash(); }
    // A branch of the whole world run ahead: bodies and cells in flight with it. `each` is told what the laws
    // did each step.
    [[nodiscard]] Ahead lattice_ahead(std::span<const agent::Edit> edits, std::int64_t steps, const Each& each) const override;
    [[nodiscard]] std::uint64_t lattice_body(std::uint32_t cell) const override;

private:
    void bind(rigid::MatterWorld& world);
    void mirror(agent::FieldState& state, const rigid::MatterWorld& world, std::span<const std::uint32_t> cells) const;
    [[nodiscard]] std::uint32_t number_of(const openas::Actor& actor);

    rigid::MatterWorld*          world_;
    agent::FieldState            state_;      // the world's lattice as a field's state is read: its cells now
    std::vector<agent::Witness*> witnesses_;
    std::vector<openas::Actor>   actors_;     // [0] the owner; an edit's `by` numbers them
    std::uint32_t                by_ = 0;
    std::vector<agent::Wrote>    wrote_;      // a frame's scratch
    // What a body's coming loose is told from, kept frame to frame (and dropped when the world goes back): whom
    // each body touched at the frame before, when each last lost a cell or fell apart and what it came apart
    // from, and each one's motion since it was last still.
    struct Track {
        std::vector<std::uint64_t> touched, earlier;    // at the end of the frame before, and of the one before that
        std::int64_t               changed = -1;        // the last frame it lost a cell, split or was gone
        std::uint64_t              of = 0;              // the body it is a piece of (0: none)
        std::int64_t               since = 0, last = 0; // moving since which frame; the last frame it moved
        float                      start_x = 0, start_y = 0;
        std::vector<std::uint64_t> supports;            // whom it touched as it began to move
        bool                       told = false;
    };
    std::unordered_map<std::uint64_t, Track> tracks_;
    std::vector<xengine::dynamics::rigid::BodyId> touching_;  // a frame's scratch
};

}  // namespace xengine::cellular::rigid_agent
