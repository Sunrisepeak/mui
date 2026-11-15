module xengine.dynamics.scene;

import std;

namespace xengine::dynamics::rigid3 {

std::span<const mlibs::math::Vec3> Scene::winds(const xengine::field::Field& air, const xengine::particles::Cloud& cloud) {
    winds_.clear();
    for (const mlibs::math::Vec3 at : cloud.at()) winds_.push_back(air.wind_at(at));
    return winds_;
}

std::expected<xengine::matter::Report, xengine::matter::Failure> Scene::step(const World& world, Objects& objects, xengine::particles::Cloud& cloud,
                                                                             std::span<const BodyId> surface_bodies, xengine::field::Field& air) {
    const xengine::field::Guests bodies = objects.guests_in(world, air);
    cloud.pair();
    for (const mlibs::math::Vec3 at : cloud.at()) air.cover(at, 0.5f);
    const xengine::field::Guests particles{.state = cloud.state_to_step(), .pairs = cloud.pairs(), .at = cloud.at()};
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
    const std::array<xengine::field::Guests, 2> guests{bodies, particles};
    auto stepped = air.step(guests, between_);
    if (!stepped) return stepped;
    // Who each instance of the step is, for the causes both keep: a body, a particle, else none (a cell).
    const std::uint32_t bodies_first = air.first_guest(0), particles_first = air.first_guest(1);
    const auto count = static_cast<std::uint32_t>(cloud.size());
    const std::span<const std::uint32_t> ids = cloud.ids();
    const auto particle = [&](std::uint32_t instance) -> xengine::matter::Who {
        return instance >= particles_first && instance < particles_first + count ? xengine::particles::who(ids[instance - particles_first]) : xengine::matter::Who{};
    };
    objects.joined(air, 0, particle);
    const std::span<const BodyId> bodies_now = objects.bodies();
    cloud.stepped(air.events(), particles_first, [&](std::uint32_t instance) { return air.why(instance); }, [&](std::uint32_t instance) -> xengine::matter::CameFrom {
        if (instance >= bodies_first && instance < bodies_first + bodies_now.size()) return std::pair{who(bodies_now[instance - bodies_first]), std::optional<std::array<float, 3>>{}};
        if (instance < air.cells()) {
            const auto centre = air.centre(instance);
            return std::pair{xengine::matter::Who{}, std::array<float, 3>{centre.x, centre.y, centre.z}};
        }
        return std::nullopt;
    });
    return stepped;
}

std::vector<xengine::matter::Explained> Scene::explain(xengine::matter::Who who, const Objects& objects, const xengine::particles::Cloud& cloud, std::size_t depth) {
    return xengine::matter::Causes::explain(who, [&](xengine::matter::Who one) {
        return one.carrier == bodies_carrier ? objects.causes().of(one) : one.carrier == xengine::particles::particles_carrier ? cloud.causes().of(one) : std::span<const xengine::matter::Cause>();
    }, depth);
}

}  // namespace xengine::dynamics::rigid3
