// meng.particles: they fall as the step says, bounce off the world's triangles by their material, cool in the
// air and warm each other, die when their life is over, and two clouds — or a cloud and its copy — agree.

import std;
import meng.particles;
import meng.matter.json;
import meng.matter.appearance;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace particles = meng::particles;
namespace matter = meng::matter;
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

UTest cases{"meng.particles", [] {
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

    "coloured by the world's look, the lattice's rule: a hot spark glows and stops glowing as it cools"_test = [] {
        const auto program = standard();
        const auto look = matter::standard_look(*program);
        expect(fatal(look.has_value()));
        const auto shade = look->shade();
        expect(fatal(mlibs::math::color::check(shade, static_cast<std::uint32_t>(program->channels().size())).has_value()));
        auto cloud = particles::Cloud::make(program, {.gravity = {0, 0, 0}, .ambient = *program->material("air"), .ambient_kelvin = 293});
        const auto iron = *program->material("iron");
        const std::array<particles::Spawn, 2> two{{{.at = {0, 0, 0}, .life = 100, .material = iron, .kelvin = 1500.0f}, {.at = {1, 0, 0}, .life = 100, .material = iron}}};
        cloud->emit(two);
        std::array<std::uint32_t, 2> pixels{}, light{};
        mlibs::math::color::shade_run(shade, cloud->state(), cloud->size(), 0, pixels, light);
        const std::uint32_t hot = pixels[0], glow = light[0];
        std::println("iron at 1500 K: pixel {:08x}, light {:08x}; at its start: pixel {:08x}, light {:08x}", hot, glow, pixels[1], light[1]);
        expect(ne(glow, 0u) && eq(light[1], 0u)) << "the hot one gives light, the cold one none";
        expect(ne(hot, pixels[1]));
        for (int step = 0; step < 600; ++step) expect(cloud->step({}).has_value());
        mlibs::math::color::shade_run(shade, cloud->state(), cloud->size(), 0, pixels, light);
        std::println("after 10 s in the air: pixel {:08x}, light {:08x}", pixels[0], light[0]);
        expect(lt(light[0] >> 24, glow >> 24)) << "cooled: less light";
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
                spawns.push_back({.at = {0, 0.5f, 0}, .velocity = {static_cast<float>(math::det::cos(turn)) * 2, 3 + static_cast<float>(index % 7) * 0.2f, static_cast<float>(math::det::sin(turn)) * 2}, .life = 3, .material = *program->material(index % 3 ? "iron" : "stone"), .kelvin = 1200.0f});
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
    "particles that take room: sand poured piles up, water poured runs flat, oil comes up through water, steam rises — by density, phase and friction; the same bits twice"_test = [] {
        const auto program = standard();
        const geom::Mesh floor = geom::sheet(10, 10, 4, 4);
        const geom::Bvh floor_tree = geom::bvh_of(floor);
        const std::array<particles::Surface, 1> ground{{{.mesh = &floor, .tree = &floor_tree, .place = math::translation({0, 0, 0})}}};
        const particles::Options touching{.ambient = *program->material("air"), .touching = true, .passes = 8};
        // Poured from a funnel 0.5 m up (a grain a step, let out at 1.5 m/s: one clear of the last), then left to
        // settle: how high the heap, how far it spread.
        const auto pour = [&](std::string_view material) {
            auto cloud = particles::Cloud::make(program, touching);
            for (int step = 0; step < 520; ++step) {
                if (step < 400) {
                    const float turn = static_cast<float>(step) * 2.39996f;
                    const std::array<particles::Spawn, 1> grain{{{.at = {0.002f * static_cast<float>(math::det::cos(turn)), 0.5f, 0.002f * static_cast<float>(math::det::sin(turn))}, .velocity = {0, -1.5f, 0}, .life = 100, .material = *program->material(material)}}};
                    cloud->emit(grain);
                }
                expect(cloud->step(ground).has_value());
            }
            float high = 0;
            std::vector<float> out;
            for (const math::Vec3 at : cloud->at()) high = std::max(high, at.y), out.push_back(std::sqrt(at.x * at.x + at.z * at.z));
            std::ranges::sort(out);
            return std::tuple{high, out[out.size() * 9 / 10], std::move(*cloud)};
        };
        auto [sand_high, sand_out, sand] = pour("sand");
        auto [water_high, water_out, water] = pour("water");
        std::println("400 grains of sand: a heap {:.3f} m high, 90% within {:.3f} m (height ÷ spread {:.2f}); 400 drops of water: {:.3f} m high, within {:.3f} m ({:.2f})", sand_high, sand_out,
                     sand_high / sand_out, water_high, water_out, water_high / water_out);
        expect(gt(sand_high / sand_out, 0.3f)) << "sand stands in a heap";
        expect(lt(water_high / water_out, 0.1f)) << "water runs flat";
        expect(gt(water_out, sand_out * 2)) << "water spreads far wider";
        auto [again_high, again_out, again] = pour("sand");
        expect(eq(again.fingerprint(), sand.fingerprint())) << "the same bits twice";

        // A tank, water poured over oil lying at its bottom: the oil comes up through it.
        const geom::Mesh tank = geom::box({0.1f, 0.3f, 0.1f});
        const geom::Bvh tank_tree = geom::bvh_of(tank);
        const std::array<particles::Surface, 1> inside{{{.mesh = &tank, .tree = &tank_tree, .place = math::translation({0, 0.3f, 0})}}};
        auto layered = particles::Cloud::make(program, touching);
        std::vector<particles::Spawn> spawns;
        for (int layer = 0; layer < 8; ++layer)
            for (int row = 0; row < 8; ++row)
                for (int column = 0; column < 8; ++column)
                    // Off a perfect lattice by a little (the same little every run): nothing in a real tank stands in rows.
                    spawns.push_back({.at = {-0.07f + 0.02f * static_cast<float>(column) + 0.002f * static_cast<float>((layer * 7 + row * 3) % 5 - 2), 0.012f + 0.021f * static_cast<float>(layer),
                                             -0.07f + 0.02f * static_cast<float>(row) + 0.002f * static_cast<float>((layer * 5 + column * 3) % 5 - 2)}, .life = 100,
                                      .material = *program->material(layer < 2 ? "oil" : "water")});
        layered->emit(spawns);
        const auto mean_height = [&](const particles::Cloud& cloud, std::string_view material) {
            double sum = 0;
            int count = 0;
            for (std::size_t particle = 0; particle < cloud.size(); ++particle)
                if (cloud.material(particle) == *program->material(material)) sum += cloud.at()[particle].y, ++count;
            return count ? sum / count : 0.0;
        };
        const double oil_before = mean_height(*layered, "oil"), water_before = mean_height(*layered, "water");
        for (int step = 0; step < 300; ++step) expect(layered->step(inside).has_value());
        const double oil_after = mean_height(*layered, "oil"), water_after = mean_height(*layered, "water");
        std::println("oil under water in a tank: its mean height {:.3f} m (water {:.3f}); after 5 s {:.3f} m (water {:.3f})", oil_before, water_before, oil_after, water_after);
        expect(gt(oil_after, water_after)) << "oil floats on water";

        // Steam on its own in the air rises; a drop of water falls.
        auto air = particles::Cloud::make(program, touching);
        const std::array<particles::Spawn, 2> two{{{.at = {0, 1, 0}, .life = 10, .material = *program->material("steam"), .kelvin = 400.0f}, {.at = {1, 1, 0}, .life = 10, .material = *program->material("water")}}};
        air->emit(two);
        for (int step = 0; step < 30; ++step) expect(air->step({}).has_value());
        std::println("half a second: steam at {:.2f} m, water at {:.2f} m", air->at()[0].y, air->at()[1].y);
        expect(gt(air->at()[0].y, 1.0f) && lt(air->at()[1].y, 1.0f)) << "steam rises, water falls";
    };
}};
