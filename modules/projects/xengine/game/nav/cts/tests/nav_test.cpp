// A navigation world: built from a room's triangles, asked for paths, rays and the nearest place; walked by a
// crowd through a doorway and up a link; an obstacle and a link added and taken away; held to the world contract
// (the same steps, the same bits; an image restored runs on; a branch from bytes alone runs alike).
import std;
import xengine.nav;
import xengine.nav.cts;
import mlibs.cts;
import mcpplibs.mtest;

using mcpplibs::mtest::UTest, mcpplibs::mtest::expect, mcpplibs::mtest::eq, mcpplibs::mtest::fatal, mcpplibs::mtest::operator""_test;
namespace nav = xengine::nav;

namespace {
void run(nav::World& world, int steps) {
    for (int step = 0; step < steps; ++step) world.step();
}
}  // namespace

UTest cases{"nav", [] {
    mlibs::cts::against({.subject = "recast-detour-1.6"}, [] { nav::cts::declare(nav::cts::Family{}); });

    "built in tiles; the same triangles, the same mesh; a path around the wall through the doorway"_test = [] {
        auto world = nav::World::build(nav::cts::room());
        expect(fatal(world.has_value()));
        auto again = nav::World::build(nav::cts::room());
        expect(eq(world->state_hash(), again->state_hash())) << "the same triangles, the same tiles";
        expect(world->tiles() >= 9 && world->polygons() > 0) << std::format("{} tiles, {} polygons", world->tiles(), world->polygons());
        const auto way = world->path({-5, 0, -5}, {5, 0, -5});
        expect(fatal(way.has_value()));
        expect(way->reaches && way->corners.size() >= 3);
        const float furthest = std::ranges::max(way->corners, {}, &nav::Vec::z).z;
        expect(furthest > 8.0f) << std::format("through the doorway (z > 8): {}", furthest);
        expect(way->length > 25.0f && way->length < 35.0f) << std::format("{} m", way->length);
        // Up onto the platform: no way without the link.
        const auto up = world->path({4, 0, -9}, {10, 1.5f, -9});
        expect(up && !up->reaches) << "the platform is too high to climb";
        const auto ray = world->ray({-5, 0, -5}, {5, 0, -5});
        expect(ray && !ray->clear && ray->fraction > 0.4f && ray->fraction < 0.5f) << std::format("the wall stops it at {}", ray ? ray->fraction : -1);
        const auto near = world->nearest({3, 5, 3}, {1, 10, 1});
        expect(near && std::abs(near->at.y) < 0.3f);
        expect(!world->nearest({100, 0, 100})) << "far from the mesh";
        expect(!nav::World::build({.points = {{0, 0, 0}}, .corners = {0, 0}})) << "not triangles";
    };

    "a link joins the floor to the platform; an obstacle in the doorway closes it, taken away opens it"_test = [] {
        auto world = nav::World::build(nav::cts::room());
        const auto link = world->join(nav::cts::ramp_link());
        expect(fatal(link.has_value()));
        const auto up = world->path({4, 0, -9}, {10, 1.5f, -9});
        expect(up && up->reaches && std::ranges::count(up->onto_link, true) == 1) << "up the link";
        const auto door = world->place(nav::Obstacle::box({0, 1, 11.5f}, {1, 1, 3.6f}));
        expect(fatal(door.has_value()));
        const auto shut = world->path({-5, 0, -5}, {5, 0, -5});
        expect(shut && !shut->reaches) << "the doorway shut";
        world->take(*door);
        const auto open = world->path({-5, 0, -5}, {5, 0, -5});
        expect(open && open->reaches) << "and open again";
        expect(eq(world->obstacles().size(), std::size_t{0}) && eq(world->links().size(), std::size_t{1}));
        world->unjoin(*link);
        expect(!world->path({4, 0, -9}, {10, 1.5f, -9})->reaches);
        auto fresh = nav::World::build(nav::cts::room());
        expect(eq(world->path({-5, 0, -5}, {5, 0, -5})->length, fresh->path({-5, 0, -5}, {5, 0, -5})->length)) << "the same mesh as never touched";
    };

    "a crowd walks through the doorway and up the link; restored mid-crossing it runs on to the same bits"_test = [] {
        nav::World straight = nav::cts::crowd();
        int crossing = -1;  // the first frame a walker is carried along the link
        for (int frame = 1; frame <= 1500; ++frame) {
            straight.step();
            for (const nav::WalkerId walker : straight.walkers())
                if (crossing < 0 && straight.walker(walker)->going == nav::Going::crossing) crossing = frame;
        }
        expect(fatal(crossing > 0)) << "a walker crossed the link";
        int east = 0, up = 0;
        std::string where;
        for (const nav::WalkerId walker : straight.walkers()) {
            const auto state = straight.walker(walker);
            if (state->at.x > 4 && state->at.y < 0.5f && state->at.z > -3) ++east;
            if (state->at.y > 1.2f) ++up;
            where += std::format(" {}:({:.1f},{:.1f},{:.1f}){}", walker.value, state->at.x, state->at.y, state->at.z, static_cast<int>(state->going));
        }
        expect(east == 4 && up == 4) << std::format("{} through the doorway, {} up the link:{}", east, up, where);
        const std::uint64_t expected = straight.state_hash();
        // Pinned: the same in the dev and release profiles on Linux x86-64 (GCC 16); the wrap's CI pins Detour's own
        // crowd on Linux x86-64 and arm64, macOS, Windows and wasm32.
        expect(eq(expected, std::uint64_t{0x9299f973ab81ffc6})) << "the crowd after 1500 steps, pinned";
        // Saved at each of a few frames (some walkers on the link), restored after walking on, and from bytes.
        for (const int at : {crossing, 200, 600}) {
            nav::World rolled = nav::cts::crowd();
            run(rolled, at);
            const auto saved = rolled.save();
            run(rolled, 37);
            expect(fatal(rolled.restore(saved).has_value()));
            run(rolled, 1500 - at);
            expect(eq(rolled.state_hash(), expected)) << std::format("restored at {}", at);
            auto branch = nav::World::from(saved);
            expect(fatal(branch.has_value()));
            run(*branch, 1500 - at);
            expect(eq(branch->state_hash(), expected)) << std::format("from the bytes of {}", at);
        }
    };
}};
