module xengine.dynamics.cts;

import std;
import mlibs.simulation;
import mlibs.hash;
import xengine.dynamics;

namespace xengine::dynamics::cts {
namespace {

using mlibs::cts::Check;

SoftBody disc(BodyParams params = {}, std::span<const Plane> planes = {}) {
    std::vector<std::array<double, 2>> outline;
    for (int index = 0; index < 32; ++index) {
        const double angle = 2 * std::numbers::pi * index / 32;
        outline.push_back({100 + 20 * std::cos(angle), 100 + 20 * std::sin(angle)});
    }
    return *SoftBody::ring(outline, params, planes);
}
std::string bits(const SoftBody& body) {
    mlibs::hash::Fnv1a hash;
    for (const auto* list : {&body.x, &body.y, &body.velocity_x, &body.velocity_y})
        for (const double value : *list) hash.u64(std::bit_cast<std::uint64_t>(value));
    return std::format("{:016x}", hash.value());
}

// Every family's cases, in order.
void cases() {
    mlibs::cts::declare({"collision.sweep.face", "xengine.dynamics.collision@1", mlibs::cts::Level::exact, {"collision"}, [](Check& check) {
        const auto hit = sweep({0, 50}, 10, {100, 0}, Box{50, 40, 20, 20});
        check.that(hit.has_value(), "a circle moving right meets the box's left face");
        if (hit) {
            check.equal(hit->t, 0.4, "time of impact (centre at 40: the face at 50 less the radius)");
            check.that(hit->normal == V2{-1, 0}, "the left face's normal");
        }
    }});
    mlibs::cts::declare({"collision.sweep.miss", "xengine.dynamics.collision@1", mlibs::cts::Level::exact, {"collision"}, [](Check& check) {
        check.that(!sweep({0, 0}, 5, {100, 0}, Box{50, 40, 20, 20}).has_value(), "a circle passing above meets nothing");
    }});
    mlibs::cts::declare({"xpbd.rests-on-ground", "xengine.dynamics.xpbd@1", mlibs::cts::Level::exact, {"xpbd"}, [](Check& check) {
        const std::array ground{Plane{.normal_x = 0, .normal_y = -1, .offset = -300}};
        auto body = disc({.shape_stiffness = 0.3}, ground);
        for (int step = 0; step < 120 * 4; ++step) SoftBodyModel::step(body, {});
        const auto r = report(body);
        check.that(std::abs(r.bottom - 300) < 0.5, std::format("at rest on the ground (bottom {})", r.bottom));
        check.that(r.area > 0.95 * r.rest_area, "its area held within 5 %");
        check.evidence("state", bits(body));
    }});
    mlibs::cts::declare({"xpbd.stands-on-ledge", "xengine.dynamics.xpbd@1", mlibs::cts::Level::exact, {"xpbd"}, [](Check& check) {
        auto body = disc({.shape_stiffness = 0.3});
        BodyInput input;
        input.ledges[0] = {40, 160, 200};
        input.ledge_count = 1;
        for (int step = 0; step < 120 * 3; ++step) SoftBodyModel::step(body, input);
        const auto r = report(body);
        check.that(std::abs(r.bottom - 200) < 0.5 && !r.contacts.empty(), std::format("on the ledge (bottom {})", r.bottom));
    }});
    mlibs::cts::declare({"xpbd.replays-bit-for-bit", "xengine.dynamics.xpbd@1", mlibs::cts::Level::exact, {"xpbd"}, [](Check& check) {
        const std::array ground{Plane{.normal_x = 0, .normal_y = -1, .offset = -300}};
        mlibs::simulation::Timeline<SoftBodyModel> history(disc({}, ground), 600, 30);
        BodyInput input;
        input.walk = 60;
        for (int step = 0; step < 300; ++step) history.step(input);
        const std::string first = bits(history.state());
        check.that(history.seek(120), "back to frame 120");
        for (int step = 120; step < 300; ++step) history.step(input);
        check.equal(bits(history.state()), first, "the same inputs again, the same bits");
        check.evidence("state", first);
    }});
    mlibs::cts::declare({"material.defaults", "xengine.dynamics.material@1", mlibs::cts::Level::exact, {"material"}, [](Check& check) {
        const auto table = MaterialTable::defaults();
        for (const char* name : {"rubber", "wood", "stone", "jelly", "cloth", "sand", "water"})
            check.that(table.find(name) != nullptr, std::format("{} in the defaults", name));
        const auto params = params_of(*table.find("stone"), 3);
        check.equal(params.mass, 7.5, "stone's mass over an area of 3");
    }});
}

}  // namespace

std::size_t case_count() noexcept { return 6; }

void declare(Subject subject, mlibs::cts::Against who, std::source_location at) {
    if (who.subject.empty()) who.subject = subject.name;
    if (!who.has) who.has = [families = subject.families](std::string_view family) { return std::ranges::find(families, family) != families.end(); };
    mlibs::cts::against(std::move(who), cases, at);
}

}  // namespace xengine::dynamics::cts
