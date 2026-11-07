// Soft bodies: a ring dropped on the ground falls, flattens on landing,
// bounces, keeps its area and comes to rest on top of the ground; a hand
// drags it; every run and every replay is the same bits.
import std;
import xengine.simulation;
import xengine.simulation.bodies;
import mlibs.testing;

using namespace mlibs::testing;
namespace sim = xengine::simulation;

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
            sim::Timeline<sim::SoftBodyModel> history(ball(), 1200, 60);
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

    return suite.run(argc, argv);
}
