module xengine.nav.cts;

import std;
import mlibs.cts;

namespace xengine::nav::cts {

void Builder::quad(Vec a, Vec b, Vec c, Vec d) {
    const auto base = static_cast<std::uint32_t>(ground.points.size());
    ground.points.insert(ground.points.end(), {a, b, c, d});
    ground.corners.insert(ground.corners.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void Builder::box(Vec low, Vec high) {
    const float x0 = low.x, y0 = low.y, z0 = low.z, x1 = high.x, y1 = high.y, z1 = high.z;
    quad({x0, y1, z0}, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0});  // top
    quad({x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1});  // bottom
    quad({x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, {x1, y0, z0});  // -z
    quad({x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1});  // +z
    quad({x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0});  // -x
    quad({x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, {x1, y0, z1});  // +x
}

void Builder::floor(Vec low, Vec high) { quad({low.x, low.y, low.z}, {low.x, low.y, high.z}, {high.x, low.y, high.z}, {high.x, low.y, low.z}); }

Ground room() {
    Builder made;
    made.floor({-15, 0, -15}, {15, 0, 15});
    made.box({-0.2f, 0, -15}, {0.2f, 2, 8});
    made.box({6, 0, -12}, {12, 1.5f, -6});
    return made.ground;
}

Link ramp_link() { return {.from = {5, 0, -9}, .to = {7.2f, 1.5f, -9}, .radius = 0.5f, .both_ways = true}; }

World crowd(int walkers) {
    const Link link = ramp_link();
    auto world = World::build(room(), {}, {}, std::span(&link, 1));
    for (int index = 0; index < walkers; ++index) {
        const auto walker = *world->add({.at = {-12.0f + static_cast<float>(index % 8) * 1.3f, 0, -12.0f + static_cast<float>(index / 8) * 1.3f}});
        const Vec to = index % 2 == 0 ? Vec{8, 0, 4} : Vec{10, 1.5f, -9};
        (void)world->target(walker, to);
    }
    return std::move(*world);
}

void Family::disturb(State& world) const {
    (void)world.place(Obstacle::cylinder({0, 0, 11.5f}, 1.2f, 2));
    for (const WalkerId walker : world.walkers()) (void)world.target(walker, {-10, 0, 10});
}

std::unique_ptr<World> Family::branch(const State& world) const {
    auto made = world.branch();
    return made ? std::make_unique<World>(std::move(*made)) : nullptr;
}

void Family::read(const State& world) const {
    for (const WalkerId walker : world.walkers()) (void)world.walker(walker);
    (void)world.path({-10, 0, 0}, {10, 0, 0});
    (void)world.ray({-10, 0, 0}, {10, 0, 0});
    (void)world.nearest({3, 5, 3});
}

void declare(Family family, mlibs::cts::world::Run run) { mlibs::cts::world::declare(family, contract, run); }

}  // namespace xengine::nav::cts
