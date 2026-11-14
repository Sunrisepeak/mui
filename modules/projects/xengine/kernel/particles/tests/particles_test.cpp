// xengine.particles: they fall as the step says, bounce off the world's triangles by their material, cool in the
// air and warm each other, die when their life is over, and two clouds — or a cloud and its copy — agree.

import std;
import xengine.particles;
import xengine.matter.json;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace particles = xengine::particles;
namespace matter = xengine::matter;
namespace math = mlibs::math;
namespace geom = mlibs::geom;

namespace {

std::shared_ptr<const matter::Program> standard() {
    static const std::shared_ptr<const matter::Program> program = [] {
        auto made = matter::standard();
        expect(fatal(made.has_value()));
        return std::make_shared<const matter::Program>(std::move(*made));
    }();
    return program;
}

}  // namespace

UTest cases{"xengine.particles", [] {
    "a particle falls as the step says, and is gone when its life is over"_test = [] {
        const auto program = standard();
        auto cloud = particles::Cloud::make(program, {.drag = 0});
        expect(fatal(cloud.has_value())) << (cloud ? "" : cloud.error());
        const std::array<particles::Spawn, 1> one{{{.at = {0, 10, 0}, .life = 1.01f, .material = *program->material("iron")}}};
        expect(eq(cloud->emit(one), 1u));
        for (int step = 0; step < 60; ++step) expect(cloud->step({}).has_value());
        // Speed first, then place: after n steps of dt, it has fallen g dt² n(n+1)/2.
        const float dt = 1.0f / 60, fallen = 9.8f * dt * dt * 60 * 61 / 2;
        expect(lt(std::abs(cloud->at()[0].y - (10 - fallen)), 1e-3f)) << cloud->at()[0].y << " against " << 10 - fallen;
        expect(cloud->step({}).has_value());
        expect(eq(cloud->size(), std::size_t{0})) << "its 1.01 s are over";
    };

    "off a floor it bounces by its material — rubber higher than stone — and every strike is said with its speed"_test = [] {
        const auto program = standard();
        const geom::Mesh floor = geom::sheet(10, 10, 2, 2);
        const geom::Bvh tree = geom::bvh_of(floor);
        const std::array<particles::Surface, 1> world{{{.mesh = &floor, .tree = &tree, .place = math::Mat4{}}}};
        const auto peak = [&](std::string_view material) {
            auto cloud = particles::Cloud::make(program, {.drag = 0});
            const std::array<particles::Spawn, 1> one{{{.at = {0.3f, 1, 0.2f}, .life = 10, .material = *program->material(material)}}};
            cloud->emit(one);
            float lowest = 1, highest_after = 0, first_strike = 0;
            bool struck = false;
            for (int step = 0; step < 240; ++step) {
                expect(cloud->step(world).has_value());
                lowest = std::min(lowest, cloud->at()[0].y);
                if (!cloud->landed().empty() && !struck) struck = true, first_strike = cloud->landed()[0].speed;
                if (struck) highest_after = std::max(highest_after, cloud->at()[0].y);
            }
            std::println("{} from 1 m: struck at {:.2f} m/s, rose to {:.3f} m, never below {:.5f}", material, first_strike, highest_after, lowest);
            expect(ge(lowest, 0.0f)) << material << " went through the floor";
            expect(gt(first_strike, 4.0f) && lt(first_strike, 4.6f)) << "√(2 g · 1 m) ≈ 4.43 m/s";
            return highest_after;
        };
        const float rubber = peak("rubber"), stone = peak("stone");
        expect(gt(rubber, stone)) << "restitution: rubber keeps more of its speed";
    };

    "a hot spark cools in the air; two near each other share their heat; apart they do not"_test = [] {
        const auto program = standard();
        const auto air = *program->material("air"), iron = *program->material("iron");
        const std::uint32_t temperature = *program->channel("temperature");
        auto cloud = particles::Cloud::make(program, {.gravity = {0, 0, 0}, .near = 0.05f, .ambient = air, .ambient_kelvin = 293});
        const std::array<particles::Spawn, 1> spark{{{.at = {0, 0, 0}, .life = 100, .material = iron, .kelvin = 1500.0f}}};
        cloud->emit(spark);
        double last = cloud->value(temperature, 0);
        bool falling = true;
        for (int step = 0; step < 120; ++step) {
            expect(cloud->step({}).has_value());
            const double now = cloud->value(temperature, 0);
            falling = falling && now <= last, last = now;
        }
        std::println("an iron spark at 1500 K in air at 293 K: {:.1f} K after 2 s", last);
        expect(falling && lt(last, 1500.0) && gt(last, 293.0));
        // Two of them, one hot and one cold, with no air: near, they meet in the middle; apart, nothing flows.
        const auto pair = [&](float apart) {
            auto two = particles::Cloud::make(program, {.gravity = {0, 0, 0}, .near = 0.05f});
            const std::array<particles::Spawn, 2> both{{{.at = {0, 0, 0}, .life = 100, .material = iron, .kelvin = 1000.0f}, {.at = {apart, 0, 0}, .life = 100, .material = iron, .kelvin = 300.0f}}};
            two->emit(both);
            for (int step = 0; step < 60; ++step) expect(two->step({}).has_value());
            return std::pair{two->value(temperature, 0), two->value(temperature, 1)};
        };
        const auto [near_hot, near_cold] = pair(0.02f);
        const auto [far_hot, far_cold] = pair(0.5f);
        std::println("two sparks 2 cm apart: {:.1f} K and {:.1f} K; 50 cm apart: {:.1f} K and {:.1f} K", near_hot, near_cold, far_hot, far_cold);
        expect(lt(near_hot, 1000.0) && gt(near_cold, 300.0) && lt(std::abs(near_hot + near_cold - 1300.0), 1.0)) << "the heat shared, none made";
        expect(eq(far_hot, 1000.0) && eq(far_cold, 300.0));
    };

    "the same spawns are the same bits; a copy taken half way is a branch that agrees"_test = [] {
        const auto program = standard();
        const geom::Mesh floor = geom::sheet(10, 10, 4, 4);
        const geom::Bvh tree = geom::bvh_of(floor);
        const std::array<particles::Surface, 1> world{{{.mesh = &floor, .tree = &tree, .place = math::translation({0, -0.5f, 0})}}};
        const auto made = [&] {
            auto cloud = particles::Cloud::make(program, {.ambient = *program->material("air")});
            std::vector<particles::Spawn> spawns;
            for (int index = 0; index < 400; ++index) {
                const float turn = static_cast<float>(index) * 0.7f;
                spawns.push_back({.at = {0, 0.5f, 0}, .velocity = {math::det::cos(turn) * 2, 3 + static_cast<float>(index % 7) * 0.2f, math::det::sin(turn) * 2}, .life = 3, .material = *program->material(index % 3 ? "iron" : "stone"), .kelvin = 1200.0f});
            }
            cloud->emit(spawns);
            return std::move(*cloud);
        };
        particles::Cloud one = made(), other = made();
        for (int step = 0; step < 60; ++step) {
            const auto stepped = one.step(world);
            expect(stepped.has_value() && other.step(world).has_value()) << (stepped ? "" : stepped.error());
        }
        particles::Cloud branch = one;
        for (int step = 0; step < 60; ++step) expect(one.step(world).has_value() && branch.step(world).has_value() && other.step(world).has_value());
        std::println("400 sparks over 2 s: {} left, fingerprint {:016x}", one.size(), one.fingerprint());
        expect(eq(one.fingerprint(), other.fingerprint()) && eq(one.fingerprint(), branch.fingerprint()));
    };
}};
