// What a frame of the 2D world did besides its laws, said by the world itself (xengine.cellular.rigid's
// `Journal`; the foundation plan's §6.10.1): cells thrown and landed, bodies carried, cells that left a body,
// bodies split, and what was done to it from outside — kept only while someone watches, no part of the world's
// state. What `matter.explain` on a whole world is made from.
import std;
import xengine.matter.json;
import xengine.cellular.rigid;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;
namespace matter = xengine::matter;
namespace ca = xengine::cellular;
namespace bodies = xengine::cellular::rigid;

namespace {
const matter::Program& standard() {
    static const matter::Program program = *matter::standard();
    return program;
}
std::uint32_t material(std::string_view name) { return *standard().material(name); }
constexpr ca::Shape shape{64, 40};
std::uint32_t cell(std::uint32_t x, std::uint32_t y) { return y * shape.width + x; }

// A floor, a wall, a heap of sand on its left; a beam of wood on two posts of stone with a crate of iron on it.
std::unique_ptr<bodies::MatterWorld> scene() {
    auto world = *bodies::MatterWorld::make(standard(), {.shape = shape, .tile = {16, 16}, .seed = 4, .fill = "air"});
    world->place({0, 36, 64, 4}, material("stone"));
    world->place({20, 26, 2, 10}, material("stone"));  // the wall
    world->place({8, 31, 8, 5}, material("sand"));     // the heap
    world->place({34, 28, 2, 8}, material("stone")), world->place({46, 28, 2, 8}, material("stone"));
    world->place({34, 26, 14, 2}, material("wood"));   // the beam
    world->place({39, 22, 4, 4}, material("iron"));    // the crate
    (void)world->carve({39, 22, 4, 4});
    return world;
}
}  // namespace

UTest cases{"matter.journal", [] {
    "watching is no part of the world: the same hash each frame watched or not, and a branch is not watched"_test = [] {
        auto plain = scene(), watched = scene();
        watched->watch(true);
        for (int frame = 0; frame < 150; ++frame) {
            if (frame == 20) (void)plain->blast({12, 33}, 5.0f, 22.0f), (void)watched->blast({12, 33}, 5.0f, 22.0f);
            plain->step(), watched->step();
            if (plain->state_hash() != watched->state_hash()) {
                expect(false) << std::format("frame {}: the watched world parts from the other", frame + 1);
                break;
            }
        }
        expect(!plain->watched() && plain->journal().thrown.empty() && plain->journal().lattice.empty()) << "nothing kept where no one watches";
        auto branch = *watched->branch();
        expect(!branch->watched());
        expect(eq(branch->state_hash(), watched->state_hash()));
    };

    "a blast throws the heap over the wall: each cell that lands says the cell and the frame it was thrown from, and the blast is on record"_test = [] {
        auto world = scene();
        world->watch(true);
        for (int frame = 0; frame < 20; ++frame) world->step();
        const std::uint32_t threw = world->blast({12, 33}, 5.0f, 22.0f);
        expect(fatal(threw > 10u)) << std::format("{} cells thrown", threw);
        world->step();
        const bodies::Journal& first = world->journal();
        expect(eq(first.frame, std::uint64_t{21}));
        expect(fatal(eq(first.edits.size(), std::size_t{1})));
        expect(first.edits[0].kind == bodies::Journal::EditKind::blast && first.edits[0].centre.x == 12 && first.edits[0].radius == 5.0f);
        expect(eq(first.thrown.size(), std::size_t{threw})) << "every cell the blast threw is said, in the frame it was thrown";
        std::set<std::uint32_t> from;
        for (const auto& thrown : first.thrown) {
            expect(thrown.cause == bodies::Journal::Cause::blast && thrown.material == material("sand"));
            from.insert(thrown.cell);
        }
        expect(eq(from.size(), std::size_t{threw})) << "each from a cell of its own";
        expect(eq(first.lattice.size(), world->cells().size())) << "the lattice's own output is kept beside what the rest then wrote";
        // To rest: what lands says where it came from.
        std::size_t landed = 0, beyond = 0;
        const auto count = [&](const bodies::Journal& journal) {
            for (const auto& land : journal.landed) {
                ++landed;
                expect(from.contains(land.from) && land.thrown_at == 21u && land.material == material("sand")) << std::format("landed at {} from {} thrown at {}", land.cell, land.from, land.thrown_at);
                beyond += land.cell % shape.width > 21;
            }
        };
        count(first);  // some met the heap beside them in the frame they were thrown
        for (int frame = 21; frame < 400; ++frame) {
            world->step();
            count(world->journal());
            expect(world->journal().edits.empty()) << "an edit is said once, in the frame after it";
        }
        expect(eq(landed, std::size_t{threw})) << "everything thrown came down";
        expect(beyond > 0u) << "some of it beyond the wall";
        expect(eq(world->flying().flying(), std::size_t{0}));
    };

    "where a cell came from goes with it into a branch: thrown here, landed there, the same origin"_test = [] {
        auto world = scene();
        for (int frame = 0; frame < 20; ++frame) world->step();
        (void)world->blast({12, 33}, 5.0f, 22.0f);
        for (int frame = 0; frame < 5; ++frame) world->step();
        expect(fatal(world->flying().flying() > 0u));
        auto branch = *world->branch();
        branch->watch(true);
        std::size_t landed = 0;
        for (int frame = 0; frame < 400; ++frame) {
            branch->step();
            for (const auto& land : branch->journal().landed) ++landed, expect(land.thrown_at == 21u && land.from / shape.width >= 28u && land.from / shape.width <= 38u);
        }
        expect(landed > 0u);
    };

    "the beam burns through and the crate comes down: the cells that left the beam, the pieces it fell into, the crate carried"_test = [] {
        auto world = scene();
        world->watch(true);
        // The beam is a body too (it can fall apart); lit under the crate.
        const auto beam = world->carve({34, 26, 14, 2});
        expect(fatal(beam.has_value() && beam->size() == 1u));
        world->set(*standard().channel("temperature"), {40, 26, 2, 2}, 1200 * 256);
        world->step();
        expect(eq(world->journal().edits.size(), std::size_t{2})) << "the carving and the heat, in order";
        expect(world->journal().edits[0].kind == bodies::Journal::EditKind::carve && world->journal().edits[1].kind == bodies::Journal::EditKind::set);
        const auto crate = world->bodies().owner(40, 23);
        expect(fatal(crate.has_value()));
        const float crate_y = world->rigid().pose(*crate)->at.y;
        std::size_t left = 0, split = 0, carried = 0;
        std::uint64_t first_left = 0;
        for (int frame = 1; frame < 1500; ++frame) {
            world->step();
            const bodies::Journal& journal = world->journal();
            for (const auto& gone : journal.left) {
                ++left;
                if (!first_left) first_left = journal.frame;
                expect(gone.cell < shape.width * shape.height);
            }
            split += journal.split.size();
            for (const auto& move : journal.carried) carried += move.body == *crate && move.to.y > move.from.y;
        }
        expect(left > 0u) << "cells of the beam burned and left it";
        expect(split > 0u) << "the beam fell into pieces";
        expect(carried > 0u) << "the crate was carried down";
        expect(world->rigid().pose(*crate)->at.y > crate_y + 0.3f) << "and is lower than it was";
        expect(first_left > 1u);
    };
}};
