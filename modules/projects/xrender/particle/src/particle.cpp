module xrender.particle;

import std;
import mlibs.math;

namespace xrender::particle {

namespace {

namespace math = mlibs::math;
using math::Mat4;
using math::Vec2;

// One particle as the eye sees it: its middle in the eye's space, half its width, its turn undone.
struct Placed {
    Vec3          middle;     // in the eye's space (looking down −z)
    float         half = 0;
    float         cos_turn = 1, sin_turn = 0;
    float         distance = 0;  // −middle.z: how far in front of the eye
    std::uint32_t batch = 0, index = 0;
};

// The eye's way through a pixel's middle, in the eye's space (any length; z below 0 ahead).
struct Rays {
    Mat4  inverse;
    int   width = 0, height = 0;
    [[nodiscard]] Vec4 through(float x, float y, float depth) const noexcept {
        const float across = x / static_cast<float>(width) * 2 - 1, up = 1 - y / static_cast<float>(height) * 2;
        return inverse * Vec4{across, up, depth, 1};
    }
};

// Where the eye's ray through (x, y) meets the particle's square, in the square's own measure (−1 … 1 each way
// inside): u to the right, v up; and how far along the ray that is.
struct Met {
    Vec2  at;
    float far = 0;
};
std::optional<Met> on_square(const Rays& rays, const Placed& placed, float x, float y) noexcept {
    const Vec4 way = rays.through(x, y, 0.5f);
    if (!(way.w != 0)) return std::nullopt;
    const Vec3 along{way.x / way.w, way.y / way.w, way.z / way.w};
    if (!(along.z < 0)) return std::nullopt;
    const float reach = placed.middle.z / along.z;
    const float right = along.x * reach - placed.middle.x, up = along.y * reach - placed.middle.y;
    // Turned back by the particle's turn.
    const float u = (right * placed.cos_turn + up * placed.sin_turn) / placed.half, v = (up * placed.cos_turn - right * placed.sin_turn) / placed.half;
    return Met{{u, v}, math::length(along) * reach};
}

// A round spot: whole to half its radius, then down to nothing at its edge (smoothly).
float spot(Vec2 at) noexcept {
    const float radius = std::sqrt(at.x * at.x + at.y * at.y);
    if (radius >= 1) return 0;
    if (radius <= 0.5f) return 1;
    const float edge = (radius - 0.5f) * 2;
    return 1 - edge * edge * (3 - 2 * edge);
}

struct Shape {
    Vec3  light;  // what it gives here, before its colour and fading (times its cover)
    float cover = 0;
};
Shape shape_at(const Batch& batch, Vec2 at, float footprint) noexcept {
    if (!(at.x >= -1 && at.x <= 1 && at.y >= -1 && at.y <= 1)) return {};
    if (!batch.sprite) {
        const float cover = spot(at);
        return {{cover, cover, cover}, cover};
    }
    // `seen_at` gives the light of what is there; the shape's light is what it gives, so times its cover — as the
    // spot's (a half-clear texel adds half its light, and lays on half).
    const mesh::Texture::Seen seen = batch.sprite->seen_at({(at.x + 1) * 0.5f, (1 - at.y) * 0.5f}, footprint);
    return {seen.light * seen.cover, seen.cover};
}

std::vector<Placed> placed_of(std::span<const Batch> batches, const Mat4& view, std::uint32_t& outside) {
    std::vector<Placed> out;
    for (std::uint32_t batch = 0; batch < batches.size(); ++batch)
        for (std::uint32_t index = 0; index < batches[batch].particles.size(); ++index) {
            const Particle& one = batches[batch].particles[index];
            const Vec4 middle = view * Vec4{one.at, 1};
            if (!(middle.z < 0) || !(one.size > 0)) {
                ++outside;
                continue;
            }
            out.push_back({{middle.x, middle.y, middle.z}, one.size * 0.5f, math::det::cos(one.turn), math::det::sin(one.turn), -middle.z, batch, index});
        }
    // The farthest first; at one distance, in the batches' order.
    std::ranges::stable_sort(out, [](const Placed& one, const Placed& other) { return one.distance > other.distance; });
    return out;
}

}  // namespace

Drawn draw(std::span<const Batch> batches, const scene::View& view, mesh::Targets& frame) {
    Drawn out;
    const std::optional<Mat4> inverse = math::inverse(view.projection);
    if (!inverse || frame.width <= 0 || frame.height <= 0) return out;
    const Rays rays{*inverse, frame.width, frame.height};
    const Mat4 to_clip = view.projection;
    const std::vector<Placed> placed = placed_of(batches, view.view, out.outside);
    out.particles = static_cast<std::uint32_t>(placed.size()) + out.outside;
    const bool deep = frame.depth.size() == static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height);
    const float pixels_a_unit = std::abs(view.projection.at(1, 1)) * static_cast<float>(frame.height) * 0.5f;  // at a distance of 1
    for (const Placed& one : placed) {
        const Batch& batch = batches[one.batch];
        const Particle& particle = batch.particles[one.index];
        // The pixels its square can reach: its four corners' places on the screen.
        float low_x = std::numeric_limits<float>::max(), low_y = low_x, high_x = std::numeric_limits<float>::lowest(), high_y = high_x;
        bool seen = true;
        for (const auto& [across, up] : {std::pair{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}}) {
            const float right = (across * one.cos_turn - up * one.sin_turn) * one.half, above = (across * one.sin_turn + up * one.cos_turn) * one.half;
            const Vec4 clip = to_clip * Vec4{one.middle.x + right, one.middle.y + above, one.middle.z, 1};
            if (!(clip.w > 0)) {
                seen = false;
                break;
            }
            const float x = (clip.x / clip.w * 0.5f + 0.5f) * static_cast<float>(frame.width), y = (0.5f - clip.y / clip.w * 0.5f) * static_cast<float>(frame.height);
            low_x = std::min(low_x, x), high_x = std::max(high_x, x), low_y = std::min(low_y, y), high_y = std::max(high_y, y);
        }
        const int first_x = seen ? std::max(0, static_cast<int>(std::floor(low_x))) : 0, last_x = seen ? std::min(frame.width - 1, static_cast<int>(std::ceil(high_x))) : -1;
        const int first_y = seen ? std::max(0, static_cast<int>(std::floor(low_y))) : 0, last_y = seen ? std::min(frame.height - 1, static_cast<int>(std::ceil(high_y))) : -1;
        if (first_x > last_x || first_y > last_y) {
            ++out.outside;
            continue;
        }
        // Texels of the sprite a pixel covers: its width over the particle's width in pixels.
        const float across_pixels = 2 * one.half * pixels_a_unit / one.distance;
        const float footprint = batch.sprite && across_pixels > 0 ? static_cast<float>(batch.sprite->width) / across_pixels : 1;
        const Vec3 tint{particle.colour.x, particle.colour.y, particle.colour.z};
        for (int y = first_y; y <= last_y; ++y)
            for (int x = first_x; x <= last_x; ++x) {
                const float middle_x = static_cast<float>(x) + 0.5f, middle_y = static_cast<float>(y) + 0.5f;
                const std::optional<Met> met = on_square(rays, one, middle_x, middle_y);
                if (!met) continue;
                const Shape shape = shape_at(batch, met->at, footprint);
                if (!(shape.cover > 0)) continue;
                const std::size_t pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) + static_cast<std::size_t>(x);
                // Faded by how near it is to what is behind it here, along the eye's ray.
                float fade = 1;
                if (deep) {
                    const Vec4 surface = rays.through(middle_x, middle_y, frame.depth[pixel]);
                    if (surface.w != 0) {
                        const float behind = math::length(Vec3{surface.x / surface.w, surface.y / surface.w, surface.z / surface.w});  // how far the surface is
                        if (std::isfinite(behind) && behind > 0) {
                            const float gap = behind - met->far;
                            fade = batch.soft > 0 ? std::clamp(gap / batch.soft, 0.0f, 1.0f) : (gap > 0 ? 1.0f : 0.0f);
                        }
                    }
                }
                if (!(fade > 0)) continue;
                ++out.pixels;
                const Vec3 given = math::scaled(shape.light, tint) * fade;
                if (batch.blend == Blend::add) {
                    frame.light[pixel] = frame.light[pixel] + given;
                } else {
                    const float hides = std::clamp(particle.colour.w * shape.cover * fade, 0.0f, 1.0f);
                    frame.light[pixel] = frame.light[pixel] * (1 - hides) + given * particle.colour.w;
                    if (pixel < frame.clear.size()) frame.clear[pixel] *= 1 - hides;
                }
            }
    }
    return out;
}

std::optional<Picked> pick(std::span<const Batch> batches, const scene::View& view, float x, float y, int width, int height) {
    const std::optional<Mat4> inverse = math::inverse(view.projection);
    if (!inverse || width <= 0 || height <= 0) return std::nullopt;
    const Rays rays{*inverse, width, height};
    std::uint32_t outside = 0;
    const std::vector<Placed> placed = placed_of(batches, view.view, outside);
    const float pixels_a_unit = std::abs(view.projection.at(1, 1)) * static_cast<float>(height) * 0.5f;
    // The nearest last: the list is farthest first.
    for (auto one = placed.rbegin(); one != placed.rend(); ++one) {
        const Batch& batch = batches[one->batch];
        const std::optional<Met> met = on_square(rays, *one, x, y);
        if (!met) continue;
        const float across_pixels = 2 * one->half * pixels_a_unit / one->distance;
        const float footprint = batch.sprite && across_pixels > 0 ? static_cast<float>(batch.sprite->width) / across_pixels : 1;
        if (shape_at(batch, met->at, footprint).cover > 0.5f) return Picked{batch.id, batch.particles[one->index].id, met->far};
    }
    return std::nullopt;
}

}  // namespace xrender::particle
