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
};

}  // namespace xengine::cellular::rigid_agent
