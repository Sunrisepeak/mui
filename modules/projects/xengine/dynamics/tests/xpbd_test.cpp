// Soft bodies: a ring dropped on the ground falls, flattens on landing,
// bounces, keeps its area and comes to rest on top of the ground; a hand
// drags it; every run and every replay is the same bits.
import std;
import mlibs.simulation;
import xengine.dynamics;
import mlibs.testing;

using namespace mlibs::testing;
namespace sim = xengine::dynamics;

namespace {
// A ring of 32 points, radius 20, about (100, 100); the ground at y = 300.
sim::SoftBody ball(sim::BodyParams params = {}) {
    std::vector<std::array<double, 2>> outline;
    for (int index = 0; index < 32; ++index) {
        const double angle = 2 * std::numbers::pi * index / 32;
        outline.push_back({100 + 20 * std::cos(angle), 100 + 20 * std::sin(angle)});
    }
    const std::array ground{sim::Plane{.normal_x = 0, .normal_y = -1, .offset = -300}};
    auto body = sim::SoftBody::ring(outline, params, ground);
    check(body.has_value());
    if (!body)
        std::abort();
    return std::move(*body);
}
struct Extent {
    double width, height, lowest;
};
Extent extent(const sim::SoftBody& body) {
    const auto [left, right] = std::ranges::minmax(body.x);
    const auto [top, bottom] = std::ranges::minmax(body.y);
    return {right - left, bottom - top, bottom};
}
}  // namespace

int main(int argc, char** argv) {
    Suite suite{"simulation.bodies"};

    suite
        .test("dropped on the ground: it falls, flattens, bounces, keeps its area and rests on top",
              [] {
                  auto body = ball();
                  const double area = body.area();
                  double flattest = 1, deepest = 0, lowest_centre = 0, area_drift = 0;
                  bool rose_again = false;
                  double previous_centre = body.centre()[1];
                  bool falling = true;
                  for (int step = 0; step < 120 * 6; ++step) {
                      sim::SoftBodyModel::step(body, {});
                      const auto shape = extent(body);
                      flattest = std::min(flattest, shape.height / shape.width);
                      deepest = std::max(deepest, shape.lowest - 300);
                      area_drift =
                          std::max(area_drift, std::abs(body.area() - area) / std::abs(area));
                      const double centre = body.centre()[1];
                      lowest_centre = std::max(lowest_centre, centre);
                      if (falling && centre < previous_centre - 1e-6 && lowest_centre > 270) {
                          falling = false;
                          rose_again = true;
                      }
                      previous_centre = centre;
                  }
                  check(flattest < .95,
                        std::format("it flattens on landing: height over width down to {}",
                                    flattest));
                  check(rose_again, "it bounces");
                  check(deepest < .5, std::format("never into the ground beyond {}", deepest));
                  check(area_drift < .05, std::format("its area holds within {}", area_drift));
                  const auto rest = extent(body);
                  check(std::abs(rest.lowest - 300) < .5 && std::abs(body.centre()[1] - 280) < 2,
                        std::format("at rest on the ground: centre {}", body.centre()[1]));
                  double speed = 0;
                  for (std::size_t index = 0; index < body.size(); ++index)
                      speed = std::max(speed, std::abs(body.velocity_y[index]));
                  check(speed < 5, std::format("and still: {}", speed));
              });

    suite.test("a hand drags it and lets it go", [] {
        auto body = ball({.gravity_y = 0});
        for (int step = 0; step < 120; ++step)
            sim::SoftBodyModel::step(body, {.pin = {.particle = 0,
                                                    .x = 160 + step * .5,
                                                    .y = 100,
                                                    .active = true}});
        check(body.x[0] == 160 + 119 * .5, "the held particle where the hand is");
        check(body.centre()[0] > 120,
              std::format("the body came along: centre at {}", body.centre()[0]));
    });

    suite.test("the same run twice, and a replay from a checkpoint, are the same bits", [] {
        const auto run = [] {
            mlibs::simulation::Timeline<sim::SoftBodyModel> history(ball(), 1200, 60);
            for (int step = 0; step < 300; ++step)
                history.step({.pin = {.particle = 3, .x = 100, .y = 60, .active = step < 40}});
            return history;
        };
        auto first = run();
        const auto second = run();
        check(first.state().x == second.state().x && first.state().y == second.state().y,
              "deterministic");
        const auto at_end = first.state();
        check(first.seek(150) && first.seek(300), "back and forth through the checkpoints");
        check(first.state().x == at_end.x && first.state().velocity_y == at_end.velocity_y,
              "the replay reaches the same bits");
    });

    suite.test("malformed bodies are refused", [] {
        const std::array<std::array<double, 2>, 2> two{{{0, 0}, {1, 0}}};
        check(!sim::SoftBody::ring(two));
        const std::array<std::array<double, 2>, 3> line{{{0, 0}, {1, 0}, {2, 0}}};
        check(!sim::SoftBody::ring(line), "no area");
        const std::array<std::array<double, 2>, 3> triangle{{{0, 0}, {1, 0}, {0, 1}}};
        check(!sim::SoftBody::ring(triangle, {.substeps = 0}));
        check(!sim::SoftBody::ring(triangle, {.mass = -1}));
    });

    suite.test("ledges: what comes down onto one stands on it; beside it or from below it passes; a push walks it, a kick hops it", [] {
        const auto with_ledge = [](double left, double right) {
            sim::BodyInput in;
            in.ledges[0] = {left, right, 200};
            in.ledge_count = 1;
            return in;
        };
        // Dropped onto a ledge under it: stands on it, above the ground.
        auto on = ball({.shape_stiffness = 0.3});
        for (int step = 0; step < 120 * 3; ++step) sim::SoftBodyModel::step(on, with_ledge(40, 160));
        check(std::abs(extent(on).lowest - 200) < 0.5, std::format("standing on the ledge: {}", extent(on).lowest));
        // Beside it: falls past, to the ground.
        auto beside = ball({.shape_stiffness = 0.3});
        for (int step = 0; step < 120 * 3; ++step) sim::SoftBodyModel::step(beside, with_ledge(300, 400));
        check(std::abs(extent(beside).lowest - 300) < 0.5, std::format("past it, on the ground: {}", extent(beside).lowest));
        // From below: a body under the ledge rises through it (a kick up) and lands on it.
        auto under = ball({.shape_stiffness = 0.3});
        for (int step = 0; step < 120 * 2; ++step) sim::SoftBodyModel::step(under, {});  // on the ground first
        sim::BodyInput hop = with_ledge(40, 160);
        hop.kick_y = -700;
        sim::SoftBodyModel::step(under, hop);
        double highest = 1e9;
        for (int step = 0; step < 120 * 3; ++step) {
            sim::SoftBodyModel::step(under, with_ledge(40, 160));
            highest = std::min(highest, under.centre()[1]);
        }
        check(highest < 180, std::format("through it from below: up to {}", highest));
        check(std::abs(extent(under).lowest - 200) < 0.5, std::format("and down onto it: {}", extent(under).lowest));
        // A push walks it along the ground; standing, it stays.
        auto walker = ball({.shape_stiffness = 0.3});
        for (int step = 0; step < 120; ++step) sim::SoftBodyModel::step(walker, {});
        const double start = walker.centre()[0];
        sim::BodyInput walk;
        walk.push_x = 400;
        for (int step = 0; step < 120; ++step) sim::SoftBodyModel::step(walker, walk);
        check(walker.centre()[0] > start + 20, std::format("pushed: {} → {}", start, walker.centre()[0]));
        // Its feet at a speed: friction holds what touches the floor to it.
        auto strider = ball({.shape_stiffness = 0.3, .friction = 0.5});
        for (int step = 0; step < 120; ++step) sim::SoftBodyModel::step(strider, {});
        const double from = strider.centre()[0];
        sim::BodyInput stride;
        stride.walk = -100;
        for (int step = 0; step < 120; ++step) sim::SoftBodyModel::step(strider, stride);
        check(strider.centre()[0] < from - 25, std::format("walked left at its feet's speed: {} → {}", from, strider.centre()[0]));
        // On a ledge too (a box-like body: no rolling, its feet alone carry it).
        auto on_ledge = ball({.shape_stiffness = 0.5, .friction = 0.5});
        sim::BodyInput standing;
        standing.ledges[0] = {0, 400, 200};
        standing.ledge_count = 1;
        for (int step = 0; step < 240; ++step) sim::SoftBodyModel::step(on_ledge, standing);
        const double at = on_ledge.centre()[0];
        sim::BodyInput going = standing;
        going.walk = 100;
        for (int step = 0; step < 120; ++step) sim::SoftBodyModel::step(on_ledge, going);
        check(on_ledge.centre()[0] > at + 40, std::format("walked along the ledge: {} → {}", at, on_ledge.centre()[0]));
        // Upright: kicked off-centre it turns, then comes back unturned; without, it keeps turning.
        const auto turned = [](const sim::SoftBody& b) {
            const auto [cx, cy] = b.centre();
            return std::atan2(b.y[0] - cy, b.x[0] - cx);  // particle 0 started at angle 0
        };
        for (const double up : {0.0, 1.0}) {
            auto spun = ball({.shape_stiffness = 0.3, .upright = up});
            for (int step = 0; step < 240; ++step) sim::SoftBodyModel::step(spun, {});
            for (std::size_t i = 0; i < spun.size(); ++i) spun.velocity_x[i] += (spun.y[i] - spun.centre()[1]) * 6;  // a spin
            for (int step = 0; step < 240; ++step) sim::SoftBodyModel::step(spun, {});
            if (up > 0) check(std::abs(turned(spun)) < 0.05, std::format("upright: back unturned ({})", turned(spun)));
            else check(std::abs(turned(spun)) > 0.1, std::format("free: it stays turned ({})", turned(spun)));
        }
        // The same inputs, the same bits.
        auto again = ball({.shape_stiffness = 0.3});
        for (int step = 0; step < 120 * 3; ++step) sim::SoftBodyModel::step(again, with_ledge(40, 160));
        check(again.x == on.x && again.y == on.y, "deterministic");
    });

    suite.test("its materials: a table every family reads; a soft body's parameters from one; its stages said in order", [] {
        auto table = sim::MaterialTable::defaults();
        const auto* jelly = table.find("jelly");
        check(jelly != nullptr && jelly->phase == sim::Phase::soft);
        const auto params = sim::params_of(*jelly, 2.0);
        check(params.mass == 2.0 && params.friction == jelly->friction && params.restitution == jelly->restitution &&
              params.shape_stiffness == jelly->shape_stiffness);
        check(table.add({"jelly"}).error() == sim::MaterialError::duplicate_name, "a name once");
        check(table.add({"bad", sim::Phase::solid, 1, 1.5}).error() == sim::MaterialError::invalid_value, "friction 0 to 1");
        const auto added = table.add({"felt", sim::Phase::soft, 0.4, 0.9, 0.1});
        check(added.has_value() && table.at(*added)->name == "felt" && table.id_of("felt") == *added);
        const auto stages = sim::stages();
        check(stages.size() == 7 && stages.front().name == "kick" && stages.back().name == "velocity");
    });

    return suite.run(argc, argv);
}
