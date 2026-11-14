// xrender.particle: particles over a frame the mesh pass drew — where they land, their order, how they fade at
// a wall and hide behind it, which one is under a point, a picture's shape.

import std;
import xrender.particle;
import mlibs.geom;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

namespace particle = xrender::particle;
namespace mesh = xrender::mesh;
namespace scene = xrender::scene;
namespace math = mlibs::math;
namespace geom = mlibs::geom;

namespace {

constexpr int width = 160, height = 120;

// A wall 6 ahead of the eye (a sheet stood up, facing it), dark, and the eye at the origin looking down −z.
struct Room {
    std::vector<geom::ClusteredMesh> made;
    std::vector<const geom::ClusteredMesh*> meshes;
    std::vector<mesh::Material> materials{{{0.05f, 0.05f, 0.05f}, {}}};
    scene::Scene world;
    scene::View view{.view = math::look_at({0, 0, 0}, {0, 0, -1}, {0, 1, 0}), .projection = math::perspective(1.0f, static_cast<float>(width) / height, 0.1f, 100.0f)};
    explicit Room(bool wall = true) {
        made.push_back(*geom::clustered(geom::sheet(40, 40, 1, 1)));
        meshes.push_back(&made[0]);
        if (wall) expect(world.add({.id = 1, .mesh = 0, .material = 0, .world = math::translation({0, 0, -6}) * math::rotation(math::Quat::about({1, 0, 0}, 1.5707964f)), .bounds = geom::bounds(made[0].mesh)}).has_value());
    }
    mesh::Targets frame() const {
        mesh::Targets out;
        out.resize(width, height);
        scene::Extracted seen;
        scene::extract(world, view, seen);
        mesh::Pass pass;
        pass.draw(world, seen, view, {meshes, materials}, out, {.background = {0, 0, 0}, .ambient = {0, 0, 0}, .shadows = false});
        return out;
    }
};
float brightness(const mesh::Targets& frame, int x, int y) {
    const math::Vec3 light = frame.light[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
    return light.x + light.y + light.z;
}

}  // namespace

UTest cases{"xrender.particle", [] {
    "a spark lands where the view puts it: its middle whole, its edge soft, nothing past its size — and the same bits again"_test = [] {
        const Room room(false);
        const std::array<particle::Particle, 1> spark{{{.at = {0, 0, -3}, .size = 0.5f, .colour = {2, 1, 0.5f, 1}, .id = 7}}};
        const std::array<particle::Batch, 1> batches{{{.particles = spark, .blend = particle::Blend::add, .id = 40}}};
        mesh::Targets frame = room.frame();
        const particle::Drawn drawn = particle::draw(batches, room.view, frame);
        // Its middle is the frame's; 0.5 across at 3 with a vertical view of 1 radian is 0.5 / (2·3·tan 0.5) of the height.
        const float across = 0.5f / (2 * 3 * std::tan(0.5f)) * height;
        const math::Vec3 middle = frame.light[static_cast<std::size_t>(height / 2) * width + width / 2];
        std::println("a spark {:.1f} pixels across: {} pixels reached; its middle ({:.3f}, {:.3f}, {:.3f})", across, drawn.pixels, middle.x, middle.y, middle.z);
        expect(eq(drawn.particles, 1u) && eq(drawn.outside, 0u));
        expect(lt(std::abs(middle.x - 2), 1e-5f) && lt(std::abs(middle.y - 1), 1e-5f) && lt(std::abs(middle.z - 0.5f), 1e-5f)) << "whole at its middle";
        // Every reached pixel within the round spot's radius; nothing past it.
        std::size_t lit = 0;
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width; ++x) {
                const float apart = std::hypot(static_cast<float>(x) + 0.5f - width / 2.0f, static_cast<float>(y) + 0.5f - height / 2.0f);
                if (brightness(frame, x, y) > 0) {
                    ++lit;
                    expect(le(apart, across * 0.5f + 0.01f)) << x << "," << y;
                }
            }
        // A disc whole to half its radius and soft beyond: about π r² pixels, a little less.
        const float disc = 3.14159265f * across * across / 4;
        expect(gt(static_cast<float>(lit), 0.85f * disc) && le(static_cast<float>(lit), disc * 1.02f)) << lit << " of about " << disc;
        mesh::Targets again = room.frame();
        particle::draw(batches, room.view, again);
        expect(frame.light == again.light) << "the same bits";
    };

    "laid over by how much they hide, the farthest first, whatever order they are given in; added, the order is the sum's"_test = [] {
        const Room room(false);
        const particle::Particle near_red{.at = {0, 0, -3}, .size = 1, .colour = {1, 0, 0, 0.6f}}, far_blue{.at = {0.1f, 0, -4}, .size = 1.4f, .colour = {0, 0, 1, 0.8f}};
        const std::array<particle::Particle, 2> one_way{near_red, far_blue}, other_way{far_blue, near_red};
        mesh::Targets first = room.frame(), second = room.frame();
        particle::draw(std::array<particle::Batch, 1>{{{.particles = one_way, .blend = particle::Blend::over}}}, room.view, first);
        particle::draw(std::array<particle::Batch, 1>{{{.particles = other_way, .blend = particle::Blend::over}}}, room.view, second);
        expect(first.light == second.light && first.clear == second.clear) << "the farthest first, either way";
        const math::Vec3 middle = first.light[static_cast<std::size_t>(height / 2) * width + width / 2];
        // Blue (0.8) under red (0.6): red 0.6, blue 0.8 × 0.4.
        expect(lt(std::abs(middle.x - 0.6f), 1e-5f) && lt(std::abs(middle.z - 0.32f), 1e-5f)) << middle.x << " " << middle.z;
        expect(lt(std::abs(first.clear[static_cast<std::size_t>(height / 2) * width + width / 2] - 0.08f), 1e-5f)) << "what shows through both: 0.2 × 0.4";
        // Two batches interleave by distance too: the near red in one, the far blue in the other.
        mesh::Targets split = room.frame();
        const std::array<particle::Particle, 1> reds{near_red}, blues{far_blue};
        particle::draw(std::array<particle::Batch, 2>{{{.particles = reds, .blend = particle::Blend::over}, {.particles = blues, .blend = particle::Blend::over}}}, room.view, split);
        expect(split.light == first.light) << "across batches as within one";
    };

    "at a wall a puff fades as it nears it, and behind it is not seen; soft 0 cuts where it goes in"_test = [] {
        const Room room;
        const std::size_t middle = static_cast<std::size_t>(height / 2) * width + width / 2;
        const mesh::Targets bare = room.frame();
        const auto added = [&](float distance, float soft) {
            mesh::Targets frame = room.frame();
            const std::array<particle::Particle, 1> puff{{{.at = {0, 0, -distance}, .size = 1, .colour = {1, 1, 1, 1}}}};
            particle::draw(std::array<particle::Batch, 1>{{{.particles = puff, .blend = particle::Blend::add, .soft = soft}}}, room.view, frame);
            return frame.light[middle].x - bare.light[middle].x;
        };
        const float far_off = added(4, 0.5f), quarter = added(5.875f, 0.5f), half = added(5.75f, 0.5f), behind = added(6.5f, 0.5f), cut = added(5.99f, 0);
        std::println("a puff before the wall at 6: 2 away {:.4f}, 0.25 away {:.4f}, 0.125 away {:.4f}; behind it {:.4f}; cut hard 0.01 away {:.4f}", far_off, half, quarter, behind, cut);
        expect(lt(std::abs(far_off - 1), 1e-5f)) << "far from the wall, whole";
        expect(lt(std::abs(half - 0.5f), 2e-3f) && lt(std::abs(quarter - 0.25f), 2e-3f)) << "by how near, over `soft`";
        expect(eq(behind, 0.0f)) << "behind the wall: nothing";
        expect(lt(std::abs(cut - 1), 1e-5f)) << "soft 0: whole up to the wall";
    };

    "which particle is under a point: the nearest whose shape is there, its batch and its own number; and none past them"_test = [] {
        const Room room(false);
        const std::array<particle::Particle, 2> near_and_far{{{.at = {0, 0, -3}, .size = 0.6f, .id = 1}, {.at = {0, 0, -5}, .size = 3, .id = 2}}};
        const std::array<particle::Batch, 1> batches{{{.particles = near_and_far, .id = 40}}};
        const auto middle = particle::pick(batches, room.view, width / 2.0f, height / 2.0f, width, height);
        expect(middle.has_value() && eq(middle->batch, scene::Id{40}) && eq(middle->particle, 1u) && lt(std::abs(middle->distance - 3), 1e-4f));
        const auto aside = particle::pick(batches, room.view, width / 2.0f + 15, height / 2.0f, width, height);
        expect(aside.has_value() && eq(aside->particle, 2u)) << "past the near one's spot, the far one";
        expect(!particle::pick(batches, room.view, 2, 2, width, height).has_value()) << "a corner of the frame: nothing";
    };

    "a picture's shape and light: a ring lets the middle through, turned a quarter its stripe stands up"_test = [] {
        const Room room(false);
        // A ring: there between 0.5 and 0.9 of its half-width; and a stripe: the left half, red.
        std::vector<std::uint8_t> ring(64 * 64 * 4, 0), stripe(64 * 64 * 4, 0);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const float radius = std::hypot(x + 0.5f - 32, y + 0.5f - 32) / 32;
                const std::size_t at = static_cast<std::size_t>(y * 64 + x) * 4;
                if (radius > 0.5f && radius < 0.9f) ring[at] = ring[at + 1] = ring[at + 2] = ring[at + 3] = 255;
                if (x < 32) stripe[at] = stripe[at + 3] = 255;
            }
        const mesh::Texture ringed = *mesh::Texture::made(ring, 64, 64), striped = *mesh::Texture::made(stripe, 64, 64);
        const std::array<particle::Particle, 1> one{{{.at = {0, 0, -3}, .size = 1}}};
        mesh::Targets frame = room.frame();
        particle::draw(std::array<particle::Batch, 1>{{{.particles = one, .sprite = &ringed}}}, room.view, frame);
        const float across = 1 / (2 * 3 * std::tan(0.5f)) * height;  // pixels the particle is across
        expect(eq(brightness(frame, width / 2, height / 2), 0.0f)) << "the ring's middle is clear";
        expect(gt(brightness(frame, width / 2 + static_cast<int>(across * 0.35f), height / 2), 2.5f)) << "its band is there";
        // The stripe: to the left at no turn; a quarter turn (counter-clockwise as seen) puts it below.
        const auto stripe_at = [&](float turn, int dx, int dy) {
            mesh::Targets drawn = room.frame();
            const std::array<particle::Particle, 1> turned{{{.at = {0, 0, -3}, .size = 1, .turn = turn}}};
            particle::draw(std::array<particle::Batch, 1>{{{.particles = turned, .sprite = &striped}}}, room.view, drawn);
            return drawn.light[static_cast<std::size_t>(height / 2 + dy) * width + static_cast<std::size_t>(width / 2 + dx)].x;
        };
        const int quarter = static_cast<int>(across * 0.25f);
        expect(gt(stripe_at(0, -quarter, 0), 0.9f) && eq(stripe_at(0, quarter, 0), 0.0f)) << "left, not right";
        expect(gt(stripe_at(1.5707964f, 0, quarter), 0.9f) && eq(stripe_at(1.5707964f, 0, -quarter), 0.0f)) << "turned a quarter: below, not above";

        // Half clear: a white picture whose every texel hides half. Added, it gives half its light; laid over a
        // light of 1, it hides half of it and gives half of its own — the same as a spot's middle at half its alpha.
        std::vector<std::uint8_t> half(16 * 16 * 4, 255);
        for (std::size_t texel = 0; texel < 16 * 16; ++texel) half[texel * 4 + 3] = 128;
        const mesh::Texture misty = *mesh::Texture::made(half, 16, 16);
        const auto middle_of = [&](particle::Blend blend, const mesh::Texture* sprite, float alpha) {
            mesh::Targets drawn = room.frame();
            for (math::Vec3& light : drawn.light) light = {1, 1, 1};
            const std::array<particle::Particle, 1> lone{{{.at = {0, 0, -3}, .size = 1, .colour = {0.5f, 0.5f, 0.5f, alpha}}}};
            particle::draw(std::array<particle::Batch, 1>{{{.particles = lone, .blend = blend, .sprite = sprite}}}, room.view, drawn);
            return drawn.light[static_cast<std::size_t>(height / 2) * width + static_cast<std::size_t>(width / 2)].x;
        };
        const float share = 128.0f / 255;
        expect(lt(std::abs(middle_of(particle::Blend::add, &misty, 1) - (1 + 0.5f * share)), 1e-4f)) << "added: half its light";
        expect(lt(std::abs(middle_of(particle::Blend::over, &misty, 1) - (1 - share + 0.5f * share)), 1e-4f)) << "laid over: half hidden, half given";
        expect(lt(std::abs(middle_of(particle::Blend::over, &misty, 1) - middle_of(particle::Blend::over, nullptr, share)), 1e-4f)) << "as a spot at that alpha";
    };
}};
