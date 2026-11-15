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
    objects.joined(air, 0);
    cloud.stepped(air.events(), air.first_guest(1));
    return stepped;
}

}  // namespace xengine::dynamics::rigid3
