module meng.dynamics.scene;

import std;
import mlibs.geom;

namespace meng::dynamics::rigid3 {

std::span<const mlibs::math::Vec3> Scene::winds(const meng::field::Field& air, const meng::particles::Cloud& cloud) {
    winds_.clear();
    for (const mlibs::math::Vec3 at : cloud.at()) winds_.push_back(air.wind_at(at));
    return winds_;
}

std::expected<meng::matter::Report, meng::matter::Failure> Scene::step(const World& world, Objects& objects, meng::particles::Cloud& cloud,
                                                                             std::span<const BodyId> surface_bodies, meng::field::Field& air) {
    const meng::field::Guests bodies = objects.guests_in(world, air);
    cloud.pair();
    for (const mlibs::math::Vec3 at : cloud.at()) air.cover(at, 0.5f);
    const meng::field::Guests particles{.state = cloud.state_to_step(), .pairs = cloud.pairs(), .at = cloud.at()};
    // A particle against a carried body's surface: beside that body.
    between_.clear();
    const std::span<const BodyId> carried = objects.bodies();
    const std::span<const std::uint32_t> against = cloud.against();
    for (std::size_t particle = 0; particle < against.size(); ++particle) {
        if (against[particle] >= surface_bodies.size()) continue;
        const auto found = std::ranges::find(carried, surface_bodies[against[particle]]);
        if (found == carried.end()) continue;
        between_.push_back({.group_one = 1, .one = static_cast<std::uint32_t>(particle), .group_other = 0, .other = static_cast<std::uint32_t>(found - carried.begin())});
    }
    const std::array<meng::field::Guests, 2> guests{bodies, particles};
    auto stepped = air.step(guests, between_);
    if (!stepped) return stepped;
    // Who each instance of the step is, for the causes both keep: a body, a particle, else none (a cell).
    const std::uint32_t bodies_first = air.first_guest(0), particles_first = air.first_guest(1);
    const auto count = static_cast<std::uint32_t>(cloud.size());
    const std::span<const std::uint32_t> ids = cloud.ids();
    const auto particle = [&](std::uint32_t instance) -> meng::matter::Who {
        return instance >= particles_first && instance < particles_first + count ? meng::particles::who(ids[instance - particles_first]) : meng::matter::Who{};
    };
    objects.joined(air, 0, particle);
    const std::span<const BodyId> bodies_now = objects.bodies();
    cloud.stepped(air.events(), particles_first, [&](std::uint32_t instance) { return air.why(instance); }, [&](std::uint32_t instance) -> meng::matter::CameFrom {
        if (instance >= bodies_first && instance < bodies_first + bodies_now.size()) return std::pair{who(bodies_now[instance - bodies_first]), std::optional<std::array<float, 3>>{}};
        if (instance < air.cells()) {
            const auto centre = air.centre(instance);
            return std::pair{meng::matter::Who{}, std::array<float, 3>{centre.x, centre.y, centre.z}};
        }
        return std::nullopt;
    });
    return stepped;
}

std::span<const meng::particles::Surface> Scene::surfaces(const World& world) {
    namespace math = mlibs::math;
    surfaces_.clear(), surface_bodies_.clear();
    for (const BodyId body : world.bodies()) {
        const auto pose = world.pose(body);
        if (!pose) continue;
        auto found = shaped_.find(body);
        if (found == shaped_.end()) {
            if (!world.triangles(body, corners_) || corners_.empty()) continue;
            auto made = std::make_shared<Shaped>();
            made->mesh.positions = corners_;
            made->mesh.indices.resize(corners_.size());
            std::iota(made->mesh.indices.begin(), made->mesh.indices.end(), std::uint32_t{0});
            made->tree = mlibs::geom::bvh_of(made->mesh);
            found = shaped_.emplace(body, std::move(made)).first;
        }
        surfaces_.push_back({.mesh = &found->second->mesh, .tree = &found->second->tree, .place = math::translation(pose->at) * math::rotation(pose->turn)});
        surface_bodies_.push_back(body);
    }
    return surfaces_;
}

std::expected<meng::matter::Report, meng::matter::Failure> Scene::advance(World& world, Objects& objects, meng::particles::Cloud& cloud, meng::field::Field& air) {
    objects.blow(world, air);
    objects.thrown(world, air);
    world.step();
    cloud.fly(surfaces(world), winds(air, cloud));
    return step(world, objects, cloud, surface_bodies_, air);
}

std::vector<meng::matter::Explained> Scene::explain(meng::matter::Who who, const Objects& objects, const meng::particles::Cloud& cloud, std::size_t depth, std::optional<std::uint32_t> about) {
    return meng::matter::Causes::explain(who, [&](meng::matter::Who one) {
        return one.carrier == bodies_carrier ? objects.causes().of(one) : one.carrier == meng::particles::particles_carrier ? cloud.causes().of(one) : std::span<const meng::matter::Cause>();
    }, depth, about);
}

}  // namespace meng::dynamics::rigid3
