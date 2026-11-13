module xengine.cellular.rigid.agent;

import std;
import openas;

namespace xengine::cellular::rigid_agent {

namespace {

namespace dyn = xengine::dynamics::rigid;

// A body as a number an answer carries.
constexpr std::uint64_t number(dyn::BodyId body) noexcept { return (static_cast<std::uint64_t>(body.generation) << 32) | static_cast<std::uint32_t>(body.index); }

// An edit this lattice made and told its witnesses of when it made it: the journal's copy is not told again.
constexpr std::uint32_t told = 0x8000'0000u;

// An edit of a field's kind made on a world: channels set over a region, or raised or lowered (saturating).
bool make(rigid::MatterWorld& world, const agent::Edit& edit) {
    const Shape shape = world.shape();
    const Tile region = edit.region;
    if (edit.kind == agent::EditKind::step || !region.width || !region.height || region.x >= shape.width || region.y >= shape.height ||
        region.width > shape.width - region.x || region.height > shape.height - region.y)
        return false;
    const std::size_t area = static_cast<std::size_t>(shape.width) * shape.height;
    const std::size_t channels = world.cells().size() / area;
    for (std::uint32_t written = 0; written < edit.count; ++written)
        if (edit.channels[written] >= channels) return false;
    for (std::uint32_t written = 0; written < edit.count; ++written) {
        const std::uint32_t channel = edit.channels[written], value = edit.values[written];
        if (edit.kind == agent::EditKind::set) {
            world.set(channel, region, value);
            continue;
        }
        for (std::uint32_t y = region.y; y < region.y + region.height; ++y)
            for (std::uint32_t x = region.x; x < region.x + region.width; ++x) {
                const std::uint32_t was = world.cells()[channel * area + static_cast<std::size_t>(y) * shape.width + x];
                const std::uint32_t now = edit.kind == agent::EditKind::add ? (was + value < was ? std::numeric_limits<std::uint32_t>::max() : was + value) : was - std::min(was, value);
                world.set(channel, {x, y, 1, 1}, now);
            }
    }
    return true;
}

}  // namespace

WorldLattice::WorldLattice(rigid::MatterWorld& world) : world_(&world), actors_{agent::Field::owner()} { bind(world); }

void WorldLattice::mirror(agent::FieldState& state, const rigid::MatterWorld& world, std::span<const std::uint32_t> cells) const {
    // The program is the world's own, which outlives every state made of it here: shared without owning.
    if (state.program.get() != &world.lattice()) state.program = std::shared_ptr<const Program>(std::shared_ptr<void>(), &world.lattice());
    state.shape = world.shape();
    state.boundary = world.boundary();
    state.tile = world.tile();
    state.steps = static_cast<std::int64_t>(world.frames());
    state.cells.assign(cells.begin(), cells.end());
    state.activity.assign(world.activity().begin(), world.activity().end());
}

void WorldLattice::bind(rigid::MatterWorld& world) {
    world_ = &world;
    if (!world.watched()) world.watch(true);
    world.by(by_);
    mirror(state_, world, world.cells());
}

std::uint32_t WorldLattice::number_of(const openas::Actor& actor) {
    const auto found = std::ranges::find(actors_, actor);
    if (found != actors_.end()) return static_cast<std::uint32_t>(found - actors_.begin());
    actors_.push_back(actor);
    return static_cast<std::uint32_t>(actors_.size() - 1);
}

void WorldLattice::by(const openas::Actor& actor) {
    by_ = number_of(actor);
    world_->by(by_);
}

void WorldLattice::lattice_watch(agent::Witness* witness) {
    if (witness && std::ranges::find(witnesses_, witness) == witnesses_.end()) witnesses_.push_back(witness);
}

bool WorldLattice::lattice_edit(const agent::Edit& edit, const openas::Actor& actor) {
    world_->by(number_of(actor) | told);
    const bool made = make(*world_, edit);
    world_->by(by_);
    if (!made) return false;
    mirror(state_, *world_, world_->cells());
    const agent::Edited record{.frame = lattice_frame(), .edit = edit, .actor = actor};
    for (agent::Witness* witness : witnesses_) witness->edited(state_, record);
    return true;
}

void WorldLattice::stepped(rigid::MatterWorld& world) {
    const bool watched = world.watched();
    bind(world);
    const rigid::Journal& journal = world.journal();
    // A frame the world was not watched for (the first after a copy, which is never watched) has no journal:
    // its cells are read, and nothing is on record of it.
    if (!watched || journal.frame != world.frames() || witnesses_.empty()) return;
    const std::int64_t frame = static_cast<std::int64_t>(journal.frame);
    const std::uint32_t width = state_.shape.width;
    const std::uint32_t channels = static_cast<std::uint32_t>(world.cells().size() / (static_cast<std::size_t>(width) * state_.shape.height));
    wrote_.clear();
    // What was done to it since the step before: an edit of a field's kind as a field's is; a blast as its own.
    for (const rigid::Journal::Edit& made : journal.edits) {
        if (made.by & told) continue;
        const std::uint32_t who = made.by < actors_.size() ? made.by : 0;
        if (made.kind == rigid::Journal::EditKind::blast) {
            wrote_.push_back({.kind = agent::Wrote::Kind::blast, .region = made.region, .actor = who});
            continue;
        }
        if (made.kind == rigid::Journal::EditKind::carve) continue;
        agent::Edit edit{.kind = agent::EditKind::set, .count = 1, .region = made.region};
        edit.channels[0] = made.channel, edit.values[0] = made.value;
        if (made.kind == rigid::Journal::EditKind::place && channels <= edit.channels.size()) {
            // A material placed: every channel written, the material first.
            edit.count = channels;
            for (std::uint32_t channel = 0; channel < channels; ++channel) edit.channels[channel] = channel;
        }
        const agent::Edited record{.frame = frame - 1, .edit = edit, .actor = actors_[who]};
        for (agent::Witness* witness : witnesses_) witness->edited(state_, record);
    }
    for (const auto& thrown : journal.thrown)
        if (thrown.cause == rigid::Journal::Cause::blast)
            wrote_.push_back({.kind = agent::Wrote::Kind::thrown, .cell = thrown.cell, .material = thrown.material, .by = agent::Wrote::By::blast});
    if (!wrote_.empty())
        for (agent::Witness* witness : witnesses_) witness->wrote(state_, wrote_, frame, actors_);
    // What the laws did: the lattice's step alone, judged on its input.
    if (journal.lattice.size() == world.cells().size()) {
        state_.cells.assign(journal.lattice.begin(), journal.lattice.end());
        for (agent::Witness* witness : witnesses_) witness->stepped(state_, world.before(), journal.activity, frame);
        state_.cells.assign(world.cells().begin(), world.cells().end());
    }
    // And what the rest of the frame wrote after it.
    wrote_.clear();
    for (const auto& thrown : journal.thrown)
        if (thrown.cause != rigid::Journal::Cause::blast)
            wrote_.push_back({.kind = agent::Wrote::Kind::thrown, .cell = thrown.cell, .material = thrown.material,
                              .by = thrown.cause == rigid::Journal::Cause::pressure ? agent::Wrote::By::pressure : agent::Wrote::By::flung});
    for (const auto& landed : journal.landed)
        wrote_.push_back({.kind = agent::Wrote::Kind::landed, .cell = landed.cell, .material = landed.material, .from = landed.from,
                          .thrown_at = static_cast<std::int64_t>(landed.thrown_at)});
    for (const auto& left : journal.left)
        wrote_.push_back({.kind = agent::Wrote::Kind::left, .cell = left.cell, .material = left.cell < world.before().size() ? world.before()[left.cell] : 0,
                          .body = number(left.body)});
    // A body that came loose: one that was still, began to move and has gone a cell's length — with whom it
    // touched as it began, the one that last lost a cell or fell apart first.
    for (const auto& left : journal.left) tracks_[number(left.body)].changed = frame;
    for (const auto& split : journal.split) tracks_[number(split.of)].changed = frame, tracks_[number(split.made)].of = number(split.of);
    for (const dyn::BodyId gone : journal.gone) tracks_[number(gone)].changed = frame;
    const float cell = world.cell(), still = 0.02f * cell;
    for (const auto& carried : journal.carried) {
        Track& track = tracks_[number(carried.body)];
        if (std::hypot(carried.to.x - carried.from.x, carried.to.y - carried.from.y) < still) {
            track.since = 0;
            continue;
        }
        if (!track.since || track.last != frame - 1) {
            track.since = frame, track.start_x = carried.from.x, track.start_y = carried.from.y, track.told = false;
            track.supports = track.touched;
            for (const std::uint64_t other : track.earlier)
                if (std::ranges::find(track.supports, other) == track.supports.end()) track.supports.push_back(other);
        }
        track.last = frame;
        if (track.told || std::hypot(carried.to.x - track.start_x, carried.to.y - track.start_y) < cell) continue;
        track.told = true;
        std::uint64_t support = 0;
        std::int64_t changed = -1;
        // Of whom it touched, the one that changed last — or the body that one came apart from, which is the
        // one whose cells are on record as having left it.
        for (std::uint64_t other : track.supports) {
            std::int64_t at = -1;
            for (int up = 0; up < 8; ++up) {
                const auto found = tracks_.find(other);
                if (found == tracks_.end()) break;
                if (found->second.changed >= 0 && found->second.changed <= track.since) at = found->second.changed;
                if (at >= 0 || !found->second.of) break;
                other = found->second.of;
            }
            if (!support || at > changed) support = other, changed = at;
        }
        const auto x = static_cast<std::uint32_t>(std::clamp(track.start_x / cell, 0.0f, static_cast<float>(width - 1)));
        const auto y = static_cast<std::uint32_t>(std::clamp(track.start_y / cell, 0.0f, static_cast<float>(state_.shape.height - 1)));
        wrote_.push_back({.kind = agent::Wrote::Kind::dropped, .cell = y * width + x, .body = number(carried.body), .support = support});
        // Said at the frame it began to move.
        const agent::Wrote dropped = wrote_.back();
        wrote_.pop_back();
        for (agent::Witness* witness : witnesses_) witness->wrote(state_, {&dropped, 1}, track.since, actors_);
    }
    if (!wrote_.empty())
        for (agent::Witness* witness : witnesses_) witness->wrote(state_, wrote_, frame, actors_);
    // Whom each body touches now: what the next frame's coming loose is told from. The lattice's own solid cells
    // (fixed bodies of the terrain) are no one's support to explain.
    const std::vector<rigid::View> pieces = world.bodies().bodies();
    for (const rigid::View& view : pieces) {
        touching_.clear();
        world.rigid().touching(view.body, touching_);
        Track& track = tracks_[number(view.body)];
        track.earlier.swap(track.touched);
        track.touched.clear();
        for (const dyn::BodyId other : touching_)
            if (std::ranges::any_of(pieces, [&](const rigid::View& piece) { return piece.body == other; })) track.touched.push_back(number(other));
    }
}

std::uint64_t WorldLattice::lattice_body(std::uint32_t cell) const {
    const std::uint32_t width = state_.shape.width;
    if (!width || cell >= width * state_.shape.height) return 0;
    const auto owner = std::as_const(*world_).bodies().owner(cell % width, cell / width);
    return owner ? number(*owner) : 0;
}

void WorldLattice::rewound(rigid::MatterWorld& world) {
    bind(world);
    tracks_.clear();
    for (agent::Witness* witness : witnesses_) witness->rewound(lattice_frame());
}

agent::Lattice::Ahead WorldLattice::lattice_ahead(std::span<const agent::Edit> edits, std::int64_t steps, const Each& each) const {
    Ahead ahead;
    auto branch = world_->branch();
    if (!branch) {
        ahead.state = state_;
        ahead.hash = lattice_hash();
        return ahead;
    }
    rigid::MatterWorld& world = **branch;
    for (const agent::Edit& edit : edits) (void)make(world, edit);
    world.watch(each != nullptr);
    for (std::int64_t played = 0; played < steps; ++played) {
        world.step();
        ++ahead.frames;
        if (!each) continue;
        const rigid::Journal& journal = world.journal();
        mirror(ahead.state, world, journal.lattice);
        each(ahead.state, world.before(), journal.activity, ahead.frames);
    }
    mirror(ahead.state, world, world.cells());
    ahead.hash = world.state_hash();
    return ahead;
}

}  // namespace xengine::cellular::rigid_agent
