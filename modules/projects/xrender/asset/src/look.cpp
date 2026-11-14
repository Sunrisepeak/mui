module xrender.asset;

import std;

namespace xrender::asset {

namespace math = mlibs::math;

scene::View view_of(const Orbit& orbit, const mlibs::geom::Box3& bounds, float aspect) noexcept {
    const float level = static_cast<float>(math::det::sin(static_cast<double>(orbit.phi)));
    const math::Vec3 eye = orbit.target + math::Vec3{level * static_cast<float>(math::det::sin(static_cast<double>(orbit.theta))), static_cast<float>(math::det::cos(static_cast<double>(orbit.phi))),
                                                     level * static_cast<float>(math::det::cos(static_cast<double>(orbit.theta)))} *
                                              orbit.radius;
    // The cuts about the box as the eye sees it: from before its nearest point (never nearer than a thousandth
    // of how far its farthest is) to beyond its farthest.
    const float size = math::length(bounds.half()), apart = math::length(eye - bounds.centre());
    const float farthest = apart + size;
    const float nearest = std::max(apart - size, farthest * 0.002f) * 0.5f;
    return {.view = math::look_at(eye, orbit.target, {0, 1, 0}), .projection = math::perspective(orbit.upright, aspect, nearest, farthest * 2)};
}

std::expected<Looked, std::string> looked(const Imported& imported, const scene::View& view, const Look& how) {
    if (how.width <= 0 || how.height <= 0) return std::unexpected(std::string("a picture of no size"));
    const int samples = std::clamp(how.samples, 1, 4);
    scene::Scene world;
    if (const auto put = place(imported, world); !put) return std::unexpected(put.error());
    std::vector<const mlibs::geom::ClusteredMesh*> meshes;
    std::vector<const mesh::Texture*> textures;
    for (const mlibs::geom::ClusteredMesh& one : imported.meshes) meshes.push_back(&one);
    for (const mesh::Texture& one : imported.textures) textures.push_back(&one);
    mesh::Targets frame;
    frame.resize(how.width * samples, how.height * samples);
    scene::Extracted seen;
    scene::extract(world, view, seen);
    mesh::Pass pass;
    mesh::Options options{.background = {0, 0, 0}};
    const bool behind = how.behind && how.surroundings;
    if (how.surroundings) options.environment = {.on = true, .picture = how.surroundings, .behind = behind};
    pass.draw(world, seen, view, {meshes, imported.materials, {}, textures}, frame, options);
    mesh::Glow glow;
    std::vector<std::uint8_t> large;
    (void)glow.show(frame.light, frame.width, frame.height, {}, large, how.tone);
    Looked out{how.width, how.height, std::vector<std::uint8_t>(static_cast<std::size_t>(how.width) * static_cast<std::size_t>(how.height) * 4, 0), pass.last()};
    for (int y = 0; y < how.height; ++y)
        for (int x = 0; x < how.width; ++x) {
            // Each sample covers the pixel but for what of the background shows through it (`Targets::clear`: all
            // where nothing is drawn, some under a surface seen through or one light goes through with nothing
            // behind it); its light is then of what covers it alone, the background being black.
            std::array<double, 3> sum{0, 0, 0};
            double covered = 0;
            for (int below = 0; below < samples; ++below)
                for (int beside = 0; beside < samples; ++beside) {
                    const std::size_t at = static_cast<std::size_t>(y * samples + below) * static_cast<std::size_t>(frame.width) + static_cast<std::size_t>(x * samples + beside);
                    const double here = 1.0 - std::clamp(static_cast<double>(frame.clear[at]), 0.0, 1.0);
                    if (!(here > 0)) continue;
                    covered += here;
                    for (std::size_t channel = 0; channel < 3; ++channel) sum[channel] += large[at * 4 + channel];
                }
            std::uint8_t* pixel = out.rgba.data() + (static_cast<std::size_t>(y) * static_cast<std::size_t>(how.width) + static_cast<std::size_t>(x)) * 4;
            for (std::size_t channel = 0; channel < 3; ++channel) pixel[channel] = covered > 0 ? static_cast<std::uint8_t>(std::min(sum[channel] / covered + 0.5, 255.0)) : std::uint8_t{0};
            pixel[3] = static_cast<std::uint8_t>(covered * 255.0 / static_cast<double>(samples * samples) + 1e-9);
        }
    return out;
}

}  // namespace xrender::asset
