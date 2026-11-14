module xrender.mesh;

import std;

namespace xrender::mesh {

namespace {

using mlibs::math::Vec4;
namespace math = mlibs::math;
namespace geom = mlibs::geom;

// A triangle's corner as the pass carries it: where it is in clip space now and was the frame before, its
// normal and its place in the world.
struct Corner {
    Vec4 clip, before;
    Vec3 normal, place;
    Vec2 uv{0, 0};
    Vec4 tangent{0, 0, 0, 0};  // in the world; all 0: the mesh has none
};
Vec4 mixed(const Vec4& from, const Vec4& to, float share) noexcept {
    return {from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share, from.z + (to.z - from.z) * share, from.w + (to.w - from.w) * share};
}
Corner mixed(const Corner& from, const Corner& to, float share) noexcept {
    return {mixed(from.clip, to.clip, share), mixed(from.before, to.before, share), math::mix(from.normal, to.normal, share), math::mix(from.place, to.place, share),
            {from.uv.x + (to.uv.x - from.uv.x) * share, from.uv.y + (to.uv.y - from.uv.y) * share},
            {from.tangent.x + (to.tangent.x - from.tangent.x) * share, from.tangent.y + (to.tangent.y - from.tangent.y) * share, from.tangent.z + (to.tangent.z - from.tangent.z) * share,
             from.tangent.w}};
}
// The view's volume and a guard band about it: the depth's two planes, and eight widths to each side — past
// which a corner's place on the screen would be too large a number to snap exactly.
constexpr float guard = 8;
constexpr int planes = 6;
float inside(const Vec4& clip, int plane) noexcept {
    switch (plane) {
        case 0: return clip.z;
        case 1: return clip.w - clip.z;
        case 2: return clip.x + guard * clip.w;
        case 3: return guard * clip.w - clip.x;
        case 4: return clip.y + guard * clip.w;
        default: return guard * clip.w - clip.y;
    }
}
// The view's own sides, for leaving out what is wholly beyond one of them.
float within(const Vec4& clip, int side) noexcept {
    switch (side) {
        case 0: return clip.z;
        case 1: return clip.w - clip.z;
        case 2: return clip.x + clip.w;
        case 3: return clip.w - clip.x;
        case 4: return clip.y + clip.w;
        default: return clip.w - clip.y;
    }
}
using Polygon = std::array<Corner, 12>;
// What of the polygon is inside every plane (Sutherland and Hodgman): its corners in the same order round.
int clipped(Polygon& polygon, int count) noexcept {
    Polygon other;
    for (int plane = 0; plane < planes && count >= 3; ++plane) {
        int kept = 0;
        for (int index = 0; index < count; ++index) {
            const Corner &here = polygon[index], &next = polygon[(index + 1) % count];
            const float at_here = inside(here.clip, plane), at_next = inside(next.clip, plane);
            if (at_here >= 0) other[kept++] = here;
            if ((at_here >= 0) != (at_next >= 0)) other[kept++] = mixed(here, next, at_here / (at_here - at_next));
        }
        polygon = other, count = kept;
    }
    return count;
}

// Linear light to an sRGB byte, by a table made once with mlibs.math's own powers (a platform's `pow` differs).
std::uint8_t encoded(float linear) noexcept {
    static const std::array<std::uint8_t, 4096> table = [] {
        std::array<std::uint8_t, 4096> made{};
        for (std::size_t index = 0; index < made.size(); ++index) {
            const double value = static_cast<double>(index) / 4095.0;
            const double curved = value <= 0.0031308 ? 12.92 * value : 1.055 * math::det::pow(value, 1.0 / 2.4) - 0.055;
            made[index] = static_cast<std::uint8_t>(std::clamp(curved * 255.0 + 0.5, 0.0, 255.0));
        }
        return made;
    }();
    const float held = linear >= 0 ? (linear <= 1 ? linear : 1) : 0;  // not a number: 0
    return table[static_cast<std::size_t>(held * 4095.0f + 0.5f)];
}

// A light as the pixels use it.
struct Lamp {
    scene::LightKind kind = scene::LightKind::sun;
    Vec3  colour;        // times its intensity
    Vec3  at, toward;    // toward: of length 1
    float range = 1, cone_cosine = 0;
};
constexpr std::size_t most_lamps = 32;

struct Raster {
    Targets&       out;
    const Options& options;
    Stats&         stats;
    std::span<const Lamp> lamps;
    std::span<const Caster> casters;
    Material       material;
    Pixel          id;
    bool           still = true;  // the draw is where it was, seen from where it was seen: its pixels moved by 0, exactly
    Mat4           aside;         // this frame's jitter, in clip space
    Vec3           eye{};         // the view's, in the world
    bool           fog = false;
    std::uint64_t  at_pixel = 0;       // the pixel being shaded: what its own rays are drawn from
    Vec3           base{};             // the draw's own base colour: what a texture's light multiplies
    const Texture* texture = nullptr;  // the draw's, or none
    // The draw's other pictures, or none, and what of the material they multiply.
    const Texture* surface = nullptr;
    const Texture* normal_picture = nullptr;
    const Texture* glow = nullptr;
    const Texture* shade = nullptr;
    Material       own;         // the draw's material as it says, before any picture
    float          shaded = 1;  // the share of the light from everywhere the shade picture lets through here

    // Whether anything is between `from` and a light `reach` away along `toward` (of length 1). The ray is taken
    // into each mesh's own space as it is — not made of length 1 again — so a distance along it is the world's.
    bool hidden_from(Vec3 from, Vec3 toward, float reach) noexcept {
        ++stats.rays;
        const geom::Ray ray{from, toward};
        for (const Caster& caster : casters) {
            const std::optional<float> box = geom::meet(ray, caster.bounds);
            if (!box || *box > reach) continue;
            const geom::Ray within{math::point(caster.back, from), math::direction(caster.back, toward)};
            if (geom::meet(within, *caster.mesh, *caster.tree, reach)) {
                ++stats.shadowed;
                return true;
            }
        }
        return false;
    }

    // The share of the light from everywhere that reaches `place`: of rays about the normal (likelier the nearer
    // it), those that go the reach without meeting anything.
    float open_to(Vec3 normal, Vec3 place) noexcept {
        if (options.occlusion_rays == 0 || casters.empty()) return 1;
        math::SplitMix64 random{math::mix64(math::mix64(options.occlusion_seed) + at_pixel)};
        const Vec3 lifted = place + normal * 2e-3f;
        const Vec3 aside = math::normalized(math::cross(std::abs(normal.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, normal)), other = math::cross(normal, aside);
        std::uint32_t free = 0;
        for (std::uint32_t ray = 0; ray < options.occlusion_rays; ++ray) {
            const float first = static_cast<float>(math::unit24(random.next())), turn = static_cast<float>(math::unit24(random.next())) * 6.2831853f;
            const float out = std::sqrt(first);
            const Vec3 way = math::normalized(aside * (out * static_cast<float>(math::det::cos(static_cast<double>(turn)))) + other * (out * static_cast<float>(math::det::sin(static_cast<double>(turn)))) +
                                              normal * std::sqrt(std::max(1.0f - first, 0.0f)));
            const std::uint64_t before = stats.shadowed;
            const bool met = hidden_from(lifted, way, options.occlusion_reach);
            --stats.rays, stats.shadowed = before, ++stats.occlusion_rays;  // counted as what they are
            if (met) ++stats.occluded;
            else ++free;
        }
        return static_cast<float>(free) / static_cast<float>(options.occlusion_rays);
    }

    // What reaches a surface open to the sky by `open` of the ways about it: the open share — and, by the ways
    // that are shut, what the things in them send back, taken to be of the surface's own colour and lit as it
    // is (light going to and fro in a hollow: open / (1 − colour · (1 − open)), the form Jimenez and others
    // fitted in 2016). A dark surface's corner is dark; a light one's is filled by its own light.
    [[nodiscard]] Vec3 reaching(float open) const noexcept {
        if (open >= 1) return {1, 1, 1};
        const auto of = [&](float colour) { return open / (1.0f - std::clamp(colour, 0.0f, 0.98f) * (1.0f - open)); };
        return {of(material.base.x), of(material.base.y), of(material.base.z)};
    }

    Vec3 lit(Vec3 normal, Vec3 place) noexcept {
        if (!material.matt()) return shone(normal, place);
        Vec3 light = math::scaled(options.ambient, reaching(open_to(normal, place))) * shaded;
        // A ray starts a little off the surface, or it meets the surface it left.
        const Vec3 lifted = place + normal * 2e-3f;
        for (const Lamp& lamp : lamps) {
            if (lamp.kind == scene::LightKind::sun) {
                const float facing = -math::dot(normal, lamp.toward);
                if (facing > 0 && !(!casters.empty() && hidden_from(lifted, -lamp.toward, std::numeric_limits<float>::max()))) light = light + lamp.colour * facing;
                continue;
            }
            const Vec3 to = lamp.at - place;
            const float apart = math::length(to);
            if (!(apart < lamp.range) || !(apart > 0)) continue;
            const Vec3 along = to * (1.0f / apart);
            const float facing = math::dot(normal, along);
            if (!(facing > 0)) continue;
            if (lamp.kind == scene::LightKind::spot && -math::dot(along, lamp.toward) < lamp.cone_cosine) continue;
            if (!casters.empty() && hidden_from(lifted, along, apart)) continue;
            const float near = 1.0f - (apart / lamp.range) * (apart / lamp.range);
            light = light + lamp.colour * (facing * near * near);
        }
        return math::scaled(material.base, light) + material.emissive;
    }

    // The same lights on a surface that is not matt: each light's share by `reflected`.
    Vec3 shone(Vec3 normal, Vec3 place) noexcept {
        const Vec3 toward_eye = math::normalized(eye - place);
        Vec3 sent = math::scaled(math::scaled(reflected_ambient(material), options.ambient), reaching(open_to(normal, place))) * shaded + material.emissive;
        if (options.environment.on) sent = sent + mirrored(material, options.environment, normal, toward_eye);
        const Vec3 lifted = place + normal * 2e-3f;
        for (const Lamp& lamp : lamps) {
            if (lamp.kind == scene::LightKind::sun) {
                const float facing = -math::dot(normal, lamp.toward);
                if (facing > 0 && !(!casters.empty() && hidden_from(lifted, -lamp.toward, std::numeric_limits<float>::max())))
                    sent = sent + math::scaled(reflected(material, normal, toward_eye, -lamp.toward, facing), lamp.colour);
                continue;
            }
            const Vec3 to = lamp.at - place;
            const float apart = math::length(to);
            if (!(apart < lamp.range) || !(apart > 0)) continue;
            const Vec3 along = to * (1.0f / apart);
            const float facing = math::dot(normal, along);
            if (!(facing > 0)) continue;
            if (lamp.kind == scene::LightKind::spot && -math::dot(along, lamp.toward) < lamp.cone_cosine) continue;
            if (!casters.empty() && hidden_from(lifted, along, apart)) continue;
            const float near = 1.0f - (apart / lamp.range) * (apart / lamp.range);
            sent = sent + math::scaled(reflected(material, normal, toward_eye, along, facing), lamp.colour * (near * near));
        }
        return sent;
    }

    // One triangle, its corners inside the view's volume. True where it faces the view (and so was filled).
    bool fill(const Corner& first, const Corner& second, const Corner& third) noexcept {
        // Counter-clockwise as seen is its front. With y down that is a negative area by the rule below, so the
        // corners are taken the other way round: a front is above 0, and "inside" is above 0 for every edge.
        const std::array<const Corner*, 3> corners{&first, &third, &second};
        // For a normal picture on a mesh with no tangents: the way the surface goes as the picture's first
        // coordinate grows, and against its second (up the picture), from this triangle's own corners.
        const bool tangents = !(first.tangent.x == 0 && first.tangent.y == 0 && first.tangent.z == 0);
        std::array<Vec3, 2> bent{};
        if (normal_picture && !tangents) {
            const Vec3 one = corners[1]->place - corners[0]->place, two = corners[2]->place - corners[0]->place;
            const Vec2 uv_one{corners[1]->uv.x - corners[0]->uv.x, corners[1]->uv.y - corners[0]->uv.y}, uv_two{corners[2]->uv.x - corners[0]->uv.x, corners[2]->uv.y - corners[0]->uv.y};
            const float turn = uv_one.x * uv_two.y - uv_two.x * uv_one.y;
            if (turn != 0) bent = {(one * uv_two.y - two * uv_one.y) * (1.0f / turn), (one * uv_two.x - two * uv_one.x) * (1.0f / turn)};
        }
        std::array<double, 3> x{}, y{};
        std::array<float, 3> z{}, inverse{};
        for (int index = 0; index < 3; ++index) {
            const Vec4 clip = aside * corners[index]->clip;
            inverse[index] = 1.0f / clip.w;
            z[index] = clip.z * inverse[index];
            // The screen, y down, in 1/256 of a pixel: whole numbers, so the edges below are exact.
            x[index] = std::floor((static_cast<double>(clip.x * inverse[index]) * 0.5 + 0.5) * out.width * 256.0 + 0.5);
            y[index] = std::floor((0.5 - static_cast<double>(clip.y * inverse[index]) * 0.5) * out.height * 256.0 + 0.5);
        }
        const double area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
        if (!(area > 0)) return false;  // seen from behind, or no area
        const int left = std::max(0, static_cast<int>(std::floor(std::min({x[0], x[1], x[2]}) / 256.0)));
        const int right = std::min(out.width - 1, static_cast<int>(std::floor(std::max({x[0], x[1], x[2]}) / 256.0)));
        const int top = std::max(0, static_cast<int>(std::floor(std::min({y[0], y[1], y[2]}) / 256.0)));
        const int bottom = std::min(out.height - 1, static_cast<int>(std::floor(std::max({y[0], y[1], y[2]}) / 256.0)));
        // An edge from `a` to `b` and a point: above 0 inside. On the edge itself the pixel is the triangle's only
        // where the edge is its top or its left — so of two triangles that share an edge exactly one has it.
        const auto edge = [&](int a, int b, double px, double py) { return (x[b] - x[a]) * (py - y[a]) - (y[b] - y[a]) * (px - x[a]); };
        const auto owns = [&](int a, int b) { return y[b] < y[a] || (y[b] == y[a] && x[b] > x[a]); };
        const std::array<bool, 3> owned{owns(1, 2), owns(2, 0), owns(0, 1)};
        for (int row = top; row <= bottom; ++row) {
            const double py = row * 256.0 + 128.0;
            for (int column = left; column <= right; ++column) {
                const double px = column * 256.0 + 128.0;
                const std::array<double, 3> weight{edge(1, 2, px, py), edge(2, 0, px, py), edge(0, 1, px, py)};
                bool in = true;
                for (int index = 0; index < 3; ++index) in = in && (weight[index] > 0 || (weight[index] == 0 && owned[index]));
                if (!in) continue;
                const std::array<float, 3> share{static_cast<float>(weight[0] / area), static_cast<float>(weight[1] / area), static_cast<float>(weight[2] / area)};
                const float depth = share[0] * z[0] + share[1] * z[1] + share[2] * z[2];
                const std::size_t pixel = static_cast<std::size_t>(row) * out.width + column;
                if (options.reversed_depth ? !(depth > out.depth[pixel]) : !(depth < out.depth[pixel])) {
                    ++stats.hidden;
                    continue;
                }
                ++stats.written;
                // What varies across the surface varies with 1/w across the screen.
                const std::array<float, 3> scaled{share[0] * inverse[0], share[1] * inverse[1], share[2] * inverse[2]};
                const float whole = 1.0f / (scaled[0] + scaled[1] + scaled[2]);
                const std::array<float, 3> part{scaled[0] * whole, scaled[1] * whole, scaled[2] * whole};
                Vec3 normal = math::normalized(corners[0]->normal * part[0] + corners[1]->normal * part[1] + corners[2]->normal * part[2]);
                const Vec3 place = corners[0]->place * part[0] + corners[1]->place * part[1] + corners[2]->place * part[2];
                const Vec4 before{corners[0]->before.x * part[0] + corners[1]->before.x * part[1] + corners[2]->before.x * part[2],
                                  corners[0]->before.y * part[0] + corners[1]->before.y * part[1] + corners[2]->before.y * part[2], 0,
                                  corners[0]->before.w * part[0] + corners[1]->before.w * part[1] + corners[2]->before.w * part[2]};
                Vec2 moved{0, 0};
                if (!still && before.w > 0) {
                    const float was_x = (before.x / before.w * 0.5f + 0.5f) * static_cast<float>(out.width), was_y = (0.5f - before.y / before.w * 0.5f) * static_cast<float>(out.height);
                    // The sample is `jitter` aside of the pixel's middle; the surface there, seen without it, is not.
                    moved = {static_cast<float>(column) + 0.5f - options.jitter.x - was_x, static_cast<float>(row) + 0.5f - options.jitter.y - was_y};
                }
                if (texture || surface || normal_picture || glow || shade) {
                    // The surface's place in the picture here, a pixel to the right and a pixel down: how many texels
                    // a pixel covers is how far apart they are.
                    const auto uv_at = [&](double at_x, double at_y) {
                        const std::array<float, 3> there{static_cast<float>(edge(1, 2, at_x, at_y) / area) * inverse[0], static_cast<float>(edge(2, 0, at_x, at_y) / area) * inverse[1],
                                                         static_cast<float>(edge(0, 1, at_x, at_y) / area) * inverse[2]};
                        const float all = 1.0f / (there[0] + there[1] + there[2]);
                        return Vec2{(corners[0]->uv.x * there[0] + corners[1]->uv.x * there[1] + corners[2]->uv.x * there[2]) * all,
                                    (corners[0]->uv.y * there[0] + corners[1]->uv.y * there[1] + corners[2]->uv.y * there[2]) * all};
                    };
                    const Vec2 here = uv_at(px, py), right = uv_at(px + 256.0, py), down = uv_at(px, py + 256.0);
                    const Vec2 across{right.x - here.x, right.y - here.y}, along{down.x - here.x, down.y - here.y};
                    if (texture) material.base = math::scaled(base, texture->over(here, across, along));
                    if (surface) {
                        const Vec3 said = surface->over(here, across, along);
                        material.roughness = own.roughness * said.y, material.metallic = own.metallic * said.z;
                    }
                    if (glow) material.emissive = math::scaled(own.emissive, glow->over(here, across, along));
                    if (shade) shaded = 1.0f + own.shade_strength * (shade->over(here, across, along).x - 1.0f);
                    if (normal_picture) {
                        // The picture's three ways in the world here: along it (the mesh's tangent, or the way the
                        // surface goes as the picture's first coordinate grows), up it, and out (the normal).
                        Vec3 right_way = bent[0], up_way = bent[1];
                        if (tangents) {
                            const Vec3 said{corners[0]->tangent.x * part[0] + corners[1]->tangent.x * part[1] + corners[2]->tangent.x * part[2],
                                            corners[0]->tangent.y * part[0] + corners[1]->tangent.y * part[1] + corners[2]->tangent.y * part[2],
                                            corners[0]->tangent.z * part[0] + corners[1]->tangent.z * part[1] + corners[2]->tangent.z * part[2]};
                            right_way = said, up_way = math::cross(normal, said) * corners[0]->tangent.w;
                        }
                        right_way = right_way - normal * math::dot(normal, right_way);
                        if (math::dot(right_way, right_way) > 0) {
                            right_way = math::normalized(right_way);
                            const Vec3 other = math::cross(normal, right_way);
                            up_way = math::dot(other, up_way) < 0 ? other * -1.0f : other;
                            const Vec3 said = normal_picture->over(here, across, along);
                            const Vec3 about{(said.x * 2 - 1) * own.normal_scale, (said.y * 2 - 1) * own.normal_scale, said.z * 2 - 1};
                            const Vec3 turned = right_way * about.x + up_way * about.y + normal * about.z;
                            if (math::dot(turned, turned) > 0) normal = math::normalized(turned);
                        }
                    }
                    ++stats.textured;
                }
                at_pixel = pixel;
                Vec3 colour = lit(normal, place);
                if (fog) {
                    const Vec3 to = place - eye;
                    const float apart = math::length(to);
                    const float share = apart > 0 ? through(options.fog, eye, to * (1.0f / apart), apart) : 1.0f;
                    colour = options.fog.colour + (colour - options.fog.colour) * share;
                    ++stats.fogged;
                }
                out.depth[pixel] = depth, out.normal[pixel] = normal, out.velocity[pixel] = moved, out.ids[pixel] = id, out.light[pixel] = colour;
                out.colour[pixel * 4] = encoded(colour.x), out.colour[pixel * 4 + 1] = encoded(colour.y), out.colour[pixel * 4 + 2] = encoded(colour.z), out.colour[pixel * 4 + 3] = 255;
            }
        }
        return true;
    }
};

Vec4 moved(const Mat4& matrix, Vec3 point) noexcept { return matrix * Vec4{point, 1.0f}; }

}  // namespace

void Targets::resize(int new_width, int new_height) {
    width = std::max(new_width, 0), height = std::max(new_height, 0);
    const std::size_t pixels = static_cast<std::size_t>(width) * height;
    colour.resize(pixels * 4), light.resize(pixels), depth.resize(pixels), normal.resize(pixels), velocity.resize(pixels), ids.resize(pixels);
}

Vec3 reflected(const Material& material, Vec3 normal, Vec3 toward_eye, Vec3 toward_light, float facing) noexcept {
    const float rough = std::clamp(material.roughness, 0.04f, 1.0f), metal = std::clamp(material.metallic, 0.0f, 1.0f);
    const Vec3 matt = material.base * ((1.0f - metal) * facing);
    const float seen = math::dot(normal, toward_eye);
    if (!(seen > 0)) return matt;  // seen from behind (a smooth normal at a rim): nothing mirrored
    const Vec3 half = math::normalized(toward_eye + toward_light);
    const float along_half = std::max(math::dot(normal, half), 0.0f), eye_half = std::max(math::dot(toward_eye, half), 0.0f);
    // How many of the surface's small faces look along `half` (GGX; without the 1/π a matt surface here does
    // not have either).
    const float spread = rough * rough * rough * rough;
    const float under = along_half * along_half * (spread - 1.0f) + 1.0f;
    const float facets = spread / (under * under);
    // How much of them one hides from another, toward the light and toward the eye.
    const float k = (rough + 1.0f) * (rough + 1.0f) * 0.125f;
    const float unhidden = 1.0f / ((facing * (1.0f - k) + k) * (seen * (1.0f - k) + k));
    // How much is mirrored rather than taken in: more at a glancing look.
    const float glance = 1.0f - eye_half, glance5 = glance * glance * glance * glance * glance;
    const Vec3 head_on = Vec3{0.04f, 0.04f, 0.04f} * (1.0f - metal) + material.base * metal;
    const Vec3 mirrored = head_on + (Vec3{1, 1, 1} - head_on) * glance5;
    return matt + mirrored * (facets * unhidden * 0.25f * facing);
}
Vec3 mirrored(const Material& material, const Environment& environment, Vec3 normal, Vec3 toward_eye) noexcept {
    const float rough = std::clamp(material.roughness, 0.04f, 1.0f), metal = std::clamp(material.metallic, 0.0f, 1.0f);
    const float seen = std::max(math::dot(normal, toward_eye), 0.0f);
    const Vec3 way = normal * (2.0f * seen) - toward_eye;  // the eye's way, turned about the normal
    const float size = math::length(way);
    const Vec3 there = environment.toward(size > 0 ? way * (1.0f / size) : normal);
    const Vec3 blurred = there + (environment.level - there) * rough;
    const float glance = 1.0f - seen, glance5 = glance * glance * glance * glance * glance;
    const Vec3 head_on = Vec3{0.04f, 0.04f, 0.04f} * (1.0f - metal) + material.base * metal;
    // A rough surface mirrors less of a glancing look than a smooth one does.
    const Vec3 share = head_on + (Vec3{1, 1, 1} - head_on) * (glance5 * (1.0f - rough));
    return math::scaled(share, blurred);
}
Vec3 reflected_ambient(const Material& material) noexcept {
    const float metal = std::clamp(material.metallic, 0.0f, 1.0f);
    return material.base * (1.0f - metal) + (Vec3{0.04f, 0.04f, 0.04f} * (1.0f - metal) + material.base * metal);
}

float through(const Fog& fog, Vec3 eye, Vec3 toward, float length) noexcept {
    if (!(fog.density > 0) || !(length > 0)) return 1;
    // The density where the eye is; and how fast it thins along the ray (a unit of length).
    const double here = static_cast<double>(fog.density) * (fog.falloff > 0 ? math::det::exp(std::clamp(-static_cast<double>(fog.falloff) * (eye.y - fog.base), -60.0, 60.0)) : 1.0);
    const double thins = static_cast<double>(fog.falloff) * toward.y;
    const bool endless = !(length < std::numeric_limits<float>::max());
    double depth = 0;  // ∫ density along the ray: here · (1 − e^(−thins · length)) / thins
    if (endless) {
        if (!(thins > 1e-9)) return 0;  // level or downward for ever: nothing comes through
        depth = here / thins;
    } else if (std::abs(thins * length) < 1e-5) {
        depth = here * length;
    } else {
        depth = here * (1.0 - math::det::exp(std::clamp(-thins * length, -60.0, 60.0))) / thins;
    }
    return depth > 60 ? 0.0f : static_cast<float>(math::det::exp(-depth));
}

void Pass::draw(const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, const Assets& assets, Targets& out, const Options& options) {
    last_ = {};
    out.draws.assign(extracted.draws.begin(), extracted.draws.end());
    const std::size_t pixels = static_cast<std::size_t>(out.width) * out.height;
    if (out.depth.size() != pixels) out.resize(out.width, out.height);
    std::ranges::fill(out.depth, options.reversed_depth ? 0.0f : 1.0f);
    std::ranges::fill(out.normal, Vec3{}), std::ranges::fill(out.velocity, Vec2{0, 0}), std::ranges::fill(out.ids, Pixel{}), std::ranges::fill(out.light, options.background);
    const std::array<std::uint8_t, 4> clear{encoded(options.background.x), encoded(options.background.y), encoded(options.background.z), 255};
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) std::copy_n(clear.begin(), 4, out.colour.begin() + static_cast<std::ptrdiff_t>(pixel * 4));

    const Mat4 clip = view.projection * view.view;
    const geom::Frustum frustum = geom::Frustum::of(clip);
    const std::optional<Mat4> camera = math::inverse(view.view);
    const std::span<const scene::Instance> instances = scene.instances();

    std::array<Lamp, most_lamps> lamps{};
    std::size_t lamp_count = 0;
    for (const std::uint32_t index : extracted.lights) {
        if (lamp_count == lamps.size() || index >= scene.lights().size()) break;
        const scene::Light& light = scene.lights()[index];
        lamps[lamp_count++] = {light.kind, light.colour * light.intensity, light.at, math::normalized(light.toward), std::max(light.range, 1e-6f),
                               static_cast<float>(math::det::cos(static_cast<double>(light.cone)))};
    }
    // What can cast a shadow: every instance of the scene with a tree, seen by this view or not.
    casters_.clear();
    if (options.shadows && !assets.trees.empty())
        for (const scene::Instance& instance : instances) {
            if (instance.hidden || !instance.casts || instance.mesh >= assets.meshes.size() || instance.mesh >= assets.trees.size() || !assets.meshes[instance.mesh] || !assets.trees[instance.mesh]) continue;
            const std::optional<Mat4> back = math::inverse(instance.world);
            if (back) casters_.push_back({*back, geom::moved(instance.world, instance.bounds), &assets.meshes[instance.mesh]->mesh, assets.trees[instance.mesh]});
        }
    // The jitter: every corner moved by so much of a pixel — in clip space, by so much of its own w.
    Mat4 aside;
    if (out.width > 0 && out.height > 0) aside.at(3, 0) = 2.0f * options.jitter.x / static_cast<float>(out.width), aside.at(3, 1) = -2.0f * options.jitter.y / static_cast<float>(out.height);
    // Applied to each corner after the clipping (`aside * clip`: the column that multiplies the corner's own w).
    Raster raster{out, options, last_, std::span(lamps).first(lamp_count), casters_, {}, {}, true, aside};
    raster.fog = options.fog.density > 0 && camera.has_value();
    if (camera) raster.eye = math::point(*camera, {0, 0, 0});

    for (std::uint32_t number = 0; number < extracted.draws.size() && pixels; ++number) {
        const scene::Draw& draw = extracted.draws[number];
        if (draw.instance >= instances.size() || draw.mesh >= assets.meshes.size() || !assets.meshes[draw.mesh] || draw.material >= assets.materials.size()) {
            ++last_.missing;
            continue;
        }
        ++last_.draws;
        const scene::Instance& instance = instances[draw.instance];
        const geom::ClusteredMesh& clustered = *assets.meshes[draw.mesh];
        const geom::Mesh& mesh = clustered.mesh;
        raster.material = assets.materials[draw.material];
        raster.base = raster.material.base;
        raster.own = raster.material, raster.shaded = 1;
        const auto picture = [&](std::uint32_t which) { return (which < assets.textures.size() && !mesh.uvs.empty()) ? assets.textures[which] : nullptr; };
        raster.texture = picture(raster.material.texture), raster.surface = picture(raster.material.surface), raster.normal_picture = picture(raster.material.normal);
        raster.glow = picture(raster.material.glow), raster.shade = picture(raster.material.shade);
        const Mat4 to_clip = clip * instance.world;
        // Where it was a frame ago: its own place then, through the view then. New this frame: where it is.
        Mat4 before = to_clip;
        if (remembered_) {
            const auto found = std::ranges::lower_bound(before_ids_, instance.id);
            before = before_clip_ * (found != before_ids_.end() && *found == instance.id ? before_world_[static_cast<std::size_t>(found - before_ids_.begin())] : instance.world);
        }
        raster.still = before.values == to_clip.values;
        // Normals go by the inverse transposed; a world that cannot be inverted has no surface to light.
        const std::optional<Mat4> back = math::inverse(instance.world);
        const Mat4 for_normals = back ? math::transposed(*back) : instance.world;
        const std::array<float, 3> stretch{math::length(math::direction(instance.world, {1, 0, 0})), math::length(math::direction(instance.world, {0, 1, 0})),
                                           math::length(math::direction(instance.world, {0, 0, 1}))};
        const float largest = std::max({stretch[0], stretch[1], stretch[2]}), smallest = std::min({stretch[0], stretch[1], stretch[2]});
        // A cluster's cone is a cone still only where the instance is stretched the same every way.
        const bool even = back && camera && largest - smallest <= largest * 1e-4f;
        const Vec3 eye = even ? math::point(*back, math::point(*camera, {0, 0, 0})) : Vec3{};

        for (std::uint32_t cluster_number = 0; cluster_number < clustered.clusters.size(); ++cluster_number) {
            const geom::Cluster& cluster = clustered.clusters[cluster_number];
            ++last_.clusters;
            if (options.cull_clusters) {
                if (!frustum.holds(geom::Sphere{math::point(instance.world, cluster.bounds.centre), cluster.bounds.radius * largest})) {
                    ++last_.clusters_outside;
                    continue;
                }
                if (even && geom::faces_away(cluster, eye)) {
                    ++last_.clusters_away;
                    continue;
                }
            }
            for (std::uint32_t triangle = 0; triangle < cluster.triangle_count; ++triangle) {
                const std::size_t first = static_cast<std::size_t>(cluster.first_index) + std::size_t{triangle} * 3;
                if (first + 2 >= mesh.indices.size()) break;
                ++last_.triangles;
                Polygon polygon;
                std::array<Vec3, 3> at{};
                for (int corner = 0; corner < 3; ++corner) {
                    const std::uint32_t vertex = mesh.indices[first + static_cast<std::size_t>(corner)];
                    at[corner] = vertex < mesh.positions.size() ? mesh.positions[vertex] : Vec3{};
                    polygon[corner] = {moved(to_clip, at[corner]), moved(before, at[corner]),
                                       vertex < mesh.normals.size() ? math::direction(for_normals, mesh.normals[vertex]) : Vec3{}, math::point(instance.world, at[corner]),
                                       vertex < mesh.uvs.size() ? mesh.uvs[vertex] : Vec2{0, 0}, {}};
                    if (raster.normal_picture && vertex < mesh.tangents.size()) {
                        const Vec3 along = math::direction(instance.world, {mesh.tangents[vertex].x, mesh.tangents[vertex].y, mesh.tangents[vertex].z});
                        polygon[corner].tangent = {along.x, along.y, along.z, mesh.tangents[vertex].w};
                    }
                }
                if (mesh.normals.empty()) {
                    const Vec3 face = math::direction(for_normals, math::cross(at[1] - at[0], at[2] - at[0]));
                    polygon[0].normal = polygon[1].normal = polygon[2].normal = face;
                }
                // Wholly beyond one side of the view: nothing of it is in the frame.
                bool beyond = false, crossing = false;
                for (int side = 0; side < planes; ++side) {
                    const std::array<bool, 3> out_of{within(polygon[0].clip, side) < 0, within(polygon[1].clip, side) < 0, within(polygon[2].clip, side) < 0};
                    beyond = beyond || (out_of[0] && out_of[1] && out_of[2]);
                    const std::array<bool, 3> past{inside(polygon[0].clip, side) < 0, inside(polygon[1].clip, side) < 0, inside(polygon[2].clip, side) < 0};
                    crossing = crossing || past[0] || past[1] || past[2];
                }
                if (beyond) {
                    ++last_.outside;
                    continue;
                }
                int count = 3;
                if (crossing) {
                    ++last_.clipped;
                    count = clipped(polygon, 3);
                }
                raster.id = {number, cluster_number, static_cast<std::uint32_t>(first / 3)};
                bool filled = false;
                for (int piece = 1; piece + 1 < count; ++piece) filled = raster.fill(polygon[0], polygon[piece], polygon[piece + 1]) || filled;
                if (filled) ++last_.filled;
                else if (count >= 3) ++last_.facing_away;
                else ++last_.outside;
            }
        }
    }

    // The background through the fog: a surface infinitely far along each pixel's own ray.
    if (const std::optional<Mat4> back = raster.fog ? math::inverse(clip) : std::nullopt) {
        for (int row = 0; row < out.height; ++row)
            for (int column = 0; column < out.width; ++column) {
                const std::size_t pixel = static_cast<std::size_t>(row) * out.width + column;
                if (out.ids[pixel].draw != none) continue;
                const Vec3 through_pixel = math::point(*back, {(static_cast<float>(column) + 0.5f) / static_cast<float>(out.width) * 2 - 1,
                                                               1 - (static_cast<float>(row) + 0.5f) / static_cast<float>(out.height) * 2, 0.5f});
                const Vec3 to = through_pixel - raster.eye;
                const float apart = math::length(to);
                if (!(apart > 0)) continue;
                const float share = through(options.fog, raster.eye, to * (1.0f / apart), std::numeric_limits<float>::infinity());
                const Vec3 colour = options.fog.colour + (options.background - options.fog.colour) * share;
                out.light[pixel] = colour;
                out.colour[pixel * 4] = encoded(colour.x), out.colour[pixel * 4 + 1] = encoded(colour.y), out.colour[pixel * 4 + 2] = encoded(colour.z);
                ++last_.fogged;
            }
    }

    before_ids_.clear(), before_world_.clear();
    for (const scene::Instance& instance : instances) before_ids_.push_back(instance.id), before_world_.push_back(instance.world);
    before_clip_ = clip;
    remembered_ = true;
}

std::optional<Source> at(const Targets& targets, int x, int y) noexcept {
    if (x < 0 || y < 0 || x >= targets.width || y >= targets.height) return std::nullopt;
    const std::size_t pixel = static_cast<std::size_t>(y) * targets.width + x;
    if (pixel >= targets.ids.size()) return std::nullopt;
    const Pixel id = targets.ids[pixel];
    if (id.draw >= targets.draws.size()) return std::nullopt;
    const scene::Draw& draw = targets.draws[id.draw];
    return Source{draw.id, draw.mesh, draw.material, id.cluster, id.triangle, targets.depth[pixel], targets.normal[pixel]};
}

void objects(const Targets& targets, std::vector<std::uint32_t>& out) {
    out.resize(targets.ids.size());
    for (std::size_t pixel = 0; pixel < targets.ids.size(); ++pixel) out[pixel] = targets.ids[pixel].draw < targets.draws.size() ? targets.ids[pixel].draw + 1 : 0;
}
void describe(const Targets& targets, int x, int y, std::string& members) {
    const std::optional<Source> source = at(targets, x, y);
    if (!source) return;
    members += std::format("\"instance\":{},\"mesh\":{},\"material\":{},\"cluster\":{},\"triangle\":{},\"depth\":{},\"normal\":[{},{},{}]", source->instance, source->mesh, source->material,
                           source->cluster, source->triangle, source->depth, source->normal.x, source->normal.y, source->normal.z);
}

std::optional<Source> pick(const scene::Scene& scene, const scene::View& view, const Assets& assets, float x, float y, int width, int height) {
    if (width <= 0 || height <= 0) return std::nullopt;
    const std::optional<Mat4> back = math::inverse(view.projection * view.view);
    if (!back) return std::nullopt;
    // The ray through the point: from where it is on the near side of the view's volume to where it is deeper in.
    const float ndc_x = x / static_cast<float>(width) * 2 - 1, ndc_y = 1 - y / static_cast<float>(height) * 2;
    const Vec4 first = *back * Vec4{ndc_x, ndc_y, 0.25f, 1}, second = *back * Vec4{ndc_x, ndc_y, 0.75f, 1};
    if (first.w == 0 || second.w == 0) return std::nullopt;
    const Vec3 one = first.xyz() * (1 / first.w), other = second.xyz() * (1 / second.w);
    const std::optional<Mat4> camera = math::inverse(view.view);
    if (!camera) return std::nullopt;
    const Vec3 eye = math::point(*camera, {0, 0, 0});
    // Along the line of the two, away from the eye (which of the two is nearer depends on the depth's convention).
    const Vec3 line = math::dot(one - eye, one - eye) < math::dot(other - eye, other - eye) ? other - one : one - other;
    const float length = math::length(line);
    if (!(length > 0)) return std::nullopt;
    const geom::Ray ray{eye, line * (1 / length)};
    std::optional<Source> found;
    float nearest = std::numeric_limits<float>::max();
    for (const scene::Instance& instance : scene.instances()) {
        if (instance.hidden || !(instance.layers & view.layers) || instance.mesh >= assets.meshes.size() || instance.mesh >= assets.trees.size() || !assets.meshes[instance.mesh] ||
            !assets.trees[instance.mesh])
            continue;
        const std::optional<float> box = geom::meet(ray, geom::moved(instance.world, instance.bounds));
        if (!box || *box > nearest) continue;
        const std::optional<Mat4> into = math::inverse(instance.world);
        if (!into) continue;
        const geom::Mesh& mesh = assets.meshes[instance.mesh]->mesh;
        // In the mesh's own space, not made of length 1 again: a distance along it is the world's. Fronts only, as
        // the frame draws: a triangle met from behind is passed through.
        const geom::Ray within{math::point(*into, ray.from), math::direction(*into, ray.along)};
        const auto front = [&](std::uint32_t triangle) -> std::optional<float> {
            const Vec3 a = mesh.positions[mesh.indices[triangle * 3]], b = mesh.positions[mesh.indices[triangle * 3 + 1]], c = mesh.positions[mesh.indices[triangle * 3 + 2]];
            if (math::dot(math::cross(b - a, c - a), within.along) >= 0) return std::nullopt;
            return geom::meet(within, a, b, c);
        };
        const std::optional<geom::RayHit> hit = geom::nearest(*assets.trees[instance.mesh], within, front, nearest);
        if (!hit || hit->distance >= nearest) continue;
        nearest = hit->distance;
        const Vec3 a = mesh.positions[mesh.indices[hit->item * 3]], b = mesh.positions[mesh.indices[hit->item * 3 + 1]], c = mesh.positions[mesh.indices[hit->item * 3 + 2]];
        const Vec4 clip = view.projection * view.view * Vec4{ray.from + ray.along * hit->distance, 1};
        found = Source{instance.id, instance.mesh, instance.material, none, hit->item, clip.w != 0 ? clip.z / clip.w : 0.0f,
                       math::normalized(math::direction(math::transposed(*into), math::cross(b - a, c - a)))};
    }
    return found;
}

void shown(const Targets& targets, std::vector<Shown>& out) {
    out.assign(targets.draws.size(), {});
    for (int y = 0; y < targets.height; ++y)
        for (int x = 0; x < targets.width; ++x) {
            const std::uint32_t draw = targets.ids[static_cast<std::size_t>(y) * targets.width + x].draw;
            if (draw >= out.size()) continue;
            Shown& one = out[draw];
            if (!one.pixels) one.x0 = x, one.y0 = y, one.x1 = x + 1, one.y1 = y + 1;
            one.x0 = std::min(one.x0, x), one.x1 = std::max(one.x1, x + 1), one.y1 = y + 1;
            ++one.pixels;
        }
    for (std::size_t draw = 0; draw < out.size(); ++draw) out[draw].instance = targets.draws[draw].id;
    std::erase_if(out, [](const Shown& one) { return one.pixels == 0; });
    std::ranges::sort(out, {}, &Shown::instance);
}

Vec2 jitter_of(std::uint32_t number) noexcept {
    // Halton's sequence: a number's digits in a base, mirrored about the point.
    const auto mirrored = [](std::uint32_t index, std::uint32_t base) {
        float share = 1, value = 0;
        while (index) share /= static_cast<float>(base), value += share * static_cast<float>(index % base), index /= base;
        return value;
    };
    const std::uint32_t index = number % 16 + 1;
    return {mirrored(index, 2) - 0.5f, mirrored(index, 3) - 0.5f};
}

Temporal::Told Temporal::resolve(const Targets& frame, std::vector<std::uint8_t>& colour) {
    Told told;
    const std::size_t pixels = static_cast<std::size_t>(frame.width) * frame.height;
    colour.resize(pixels * 4);
    const bool begun = width_ == frame.width && height_ == frame.height && light_.size() == pixels;
    next_.resize(pixels), next_who_.resize(pixels), next_age_.resize(pixels);
    const auto who_at = [&](std::size_t pixel) -> Id {
        const std::uint32_t draw = frame.ids[pixel].draw;
        return draw < frame.draws.size() ? frame.draws[draw].id : 0;
    };
    // Whether the last result had this instance at the place or beside it. Beside counts: at a thing's edge the
    // sample falls on it one frame and off it the next, and the edge is exactly where frames are to be gathered.
    const auto about = [&](int at_x, int at_y, Id who) {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = at_x + dx, ny = at_y + dy;
                if (nx >= 0 && ny >= 0 && nx < width_ && ny < height_ && who_[static_cast<std::size_t>(ny) * width_ + nx] == who) return true;
            }
        return false;
    };
    for (int y = 0; y < frame.height; ++y)
        for (int x = 0; x < frame.width; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y) * frame.width + x;
            const Vec3 now = frame.light[pixel];
            const Id who = who_at(pixel);
            Vec3 result = now;
            std::uint16_t age = 0;
            if (begun) {
                // Where this surface was in the last result.
                const float was_x = static_cast<float>(x) + 0.5f - frame.velocity[pixel].x, was_y = static_cast<float>(y) + 0.5f - frame.velocity[pixel].y;
                const int near_x = static_cast<int>(std::floor(was_x)), near_y = static_cast<int>(std::floor(was_y));
                if (near_x < 0 || near_y < 0 || near_x >= width_ || near_y >= height_) {
                    ++told.outside;
                } else if (!about(near_x, near_y, who)) {
                    ++told.another;
                } else {
                    // Its light there: between the four results about the place (at a pixel's middle, that pixel's).
                    const float fx = was_x - 0.5f, fy = was_y - 0.5f;
                    const int x0 = std::clamp(static_cast<int>(std::floor(fx)), 0, width_ - 1), y0 = std::clamp(static_cast<int>(std::floor(fy)), 0, height_ - 1);
                    const int x1 = std::min(x0 + 1, width_ - 1), y1 = std::min(y0 + 1, height_ - 1);
                    const float share_x = std::clamp(fx - static_cast<float>(x0), 0.0f, 1.0f), share_y = std::clamp(fy - static_cast<float>(y0), 0.0f, 1.0f);
                    const auto past = [&](int px, int py) { return light_[static_cast<std::size_t>(py) * width_ + px]; };
                    Vec3 carried = math::mix(math::mix(past(x0, y0), past(x1, y0), share_x), math::mix(past(x0, y1), past(x1, y1), share_x), share_y);
                    // Held to what the frame shows about this pixel: what has changed does not linger.
                    Vec3 low = now, high = now;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = std::clamp(x + dx, 0, frame.width - 1), ny = std::clamp(y + dy, 0, frame.height - 1);
                            const Vec3 beside = frame.light[static_cast<std::size_t>(ny) * frame.width + nx];
                            low = {std::min(low.x, beside.x), std::min(low.y, beside.y), std::min(low.z, beside.z)};
                            high = {std::max(high.x, beside.x), std::max(high.y, beside.y), std::max(high.z, beside.z)};
                        }
                    carried = {std::clamp(carried.x, low.x, high.x), std::clamp(carried.y, low.y, high.y), std::clamp(carried.z, low.z, high.z)};
                    age = static_cast<std::uint16_t>(std::min<int>(age_[static_cast<std::size_t>(near_y) * width_ + near_x] + 1, 1000));
                    const float share = std::max(1.0f / static_cast<float>(age + 1), least);
                    result = math::mix(carried, now, share);
                    ++told.carried;
                }
            }
            next_[pixel] = result, next_who_[pixel] = who, next_age_[pixel] = age;
            colour[pixel * 4] = encoded(result.x), colour[pixel * 4 + 1] = encoded(result.y), colour[pixel * 4 + 2] = encoded(result.z), colour[pixel * 4 + 3] = 255;
        }
    light_.swap(next_), who_.swap(next_who_), age_.swap(next_age_);
    width_ = frame.width, height_ = frame.height;
    return told;
}

std::optional<Texture> Texture::made(std::span<const std::uint8_t> rgba, int width, int height, Coding coding) {
    if (width <= 0 || height <= 0 || rgba.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4) return std::nullopt;
    // The display's curve taken off, by a table made once with mlibs.math's own powers.
    static const std::array<float, 256> decoded = [] {
        std::array<float, 256> table{};
        for (std::size_t byte = 0; byte < table.size(); ++byte) {
            const double value = static_cast<double>(byte) / 255.0;
            table[byte] = static_cast<float>(value <= 0.04045 ? value / 12.92 : math::det::pow((value + 0.055) / 1.055, 2.4));
        }
        return table;
    }();
    Texture texture;
    texture.width = width, texture.height = height;
    texture.sizes.push_back(0);
    texture.light.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 / 3 + 2);
    for (std::size_t texel = 0; texel < static_cast<std::size_t>(width) * static_cast<std::size_t>(height); ++texel)
        texture.light.push_back(coding == Coding::colour ? Vec3{decoded[rgba[texel * 4]], decoded[rgba[texel * 4 + 1]], decoded[rgba[texel * 4 + 2]]}
                                                         : Vec3{static_cast<float>(rgba[texel * 4]) / 255.0f, static_cast<float>(rgba[texel * 4 + 1]) / 255.0f, static_cast<float>(rgba[texel * 4 + 2]) / 255.0f});
    // Each next size: half as wide and as high (one at least), a texel the mean of those it covers.
    for (int wide = width, high = height; wide > 1 || high > 1;) {
        const int next_wide = std::max(wide / 2, 1), next_high = std::max(high / 2, 1);
        const std::uint32_t from = texture.sizes.back();
        texture.sizes.push_back(static_cast<std::uint32_t>(texture.light.size()));
        for (int y = 0; y < next_high; ++y)
            for (int x = 0; x < next_wide; ++x) {
                const int x0 = std::min(x * 2, wide - 1), x1 = std::min(x * 2 + 1, wide - 1), y0 = std::min(y * 2, high - 1), y1 = std::min(y * 2 + 1, high - 1);
                const auto of = [&](int at_x, int at_y) { return texture.light[from + static_cast<std::size_t>(at_y) * static_cast<std::size_t>(wide) + static_cast<std::size_t>(at_x)]; };
                texture.light.push_back((of(x0, y0) + of(x1, y0) + of(x0, y1) + of(x1, y1)) * 0.25f);
            }
        wide = next_wide, high = next_high;
    }
    return texture;
}

Vec3 Texture::at(Vec2 uv, float footprint) const noexcept {
    if (sizes.empty()) return {1, 1, 1};
    // The size whose texels are a pixel across or just under, and how far toward the next: a footprint of 1 or
    // less is the finest, of 2 the next, of 3 halfway between that and the one of 4.
    std::size_t size = 0;
    float covers = footprint > 1 ? footprint : 1.0f;
    while (covers >= 2 && size + 1 < sizes.size()) covers *= 0.5f, ++size;
    const float toward_next = size + 1 < sizes.size() ? covers - 1.0f : 0.0f;
    const auto within = [&](std::size_t which) {
        const int wide = std::max(width >> which, 1), high = std::max(height >> which, 1);
        // Between the four texels about the place; a texel's middle is its own colour. It repeats.
        const float at_x = (uv.x - std::floor(uv.x)) * static_cast<float>(wide) - 0.5f, at_y = (uv.y - std::floor(uv.y)) * static_cast<float>(high) - 0.5f;
        const float floor_x = std::floor(at_x), floor_y = std::floor(at_y);
        const float share_x = at_x - floor_x, share_y = at_y - floor_y;
        const auto wrapped = [](int value, int count) { return ((value % count) + count) % count; };
        const int x0 = wrapped(static_cast<int>(floor_x), wide), x1 = wrapped(static_cast<int>(floor_x) + 1, wide), y0 = wrapped(static_cast<int>(floor_y), high),
                  y1 = wrapped(static_cast<int>(floor_y) + 1, high);
        const auto of = [&](int tx, int ty) { return light[sizes[which] + static_cast<std::size_t>(ty) * static_cast<std::size_t>(wide) + static_cast<std::size_t>(tx)]; };
        return math::mix(math::mix(of(x0, y0), of(x1, y0), share_x), math::mix(of(x0, y1), of(x1, y1), share_x), share_y);
    };
    const Vec3 here = within(size);
    return toward_next > 0 ? math::mix(here, within(size + 1), toward_next) : here;
}

Vec3 Texture::over(Vec2 uv, Vec2 right, Vec2 down) const noexcept {
    const float wide = static_cast<float>(width), high = static_cast<float>(height);
    const float across = std::sqrt(right.x * right.x * wide * wide + right.y * right.y * high * high);
    const float along = std::sqrt(down.x * down.x * wide * wide + down.y * down.y * high * high);
    if (!(across == across) || !(along == along)) return at(uv, 1);
    // The strip a pixel covers: its length the longer of the two ways, its width the shorter.
    const Vec2 lengthwise = across >= along ? right : down;
    const float longer = std::max(across, along), shorter = std::max(std::min(across, along), 1e-6f);
    const float places = std::clamp(std::ceil(longer / shorter), 1.0f, 8.0f);
    const float footprint = longer / places;
    if (places <= 1) return at(uv, footprint);
    Vec3 sum{0, 0, 0};
    for (float place = 0; place < places; place += 1) {
        const float aside = (place + 0.5f) / places - 0.5f;
        sum = sum + at({uv.x + lengthwise.x * aside, uv.y + lengthwise.y * aside}, footprint);
    }
    return sum * (1.0f / places);
}

namespace {

// One box pass along a line of `count` values `stride` apart: each becomes the mean of those within `radius` of
// it, what is past either end counted as dark. Summed afresh for each value, in one order: no error carried along.
void box_line(const Vec3* from, Vec3* into, int count, std::ptrdiff_t stride, int radius) noexcept {
    const float share = 1.0f / static_cast<float>(radius * 2 + 1);
    for (int index = 0; index < count; ++index) {
        Vec3 sum{0, 0, 0};
        const int first = std::max(index - radius, 0), last = std::min(index + radius, count - 1);
        for (int other = first; other <= last; ++other) sum = sum + from[other * stride];
        into[index * stride] = sum * share;
    }
}

}  // namespace

Glow::Told Glow::show(std::span<const Vec3> light, int width, int height, const Bloom& bloom, std::vector<std::uint8_t>& colour, const Tone& tone) {
    Told told;
    const std::size_t pixels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    colour.resize(pixels * 4);
    const int radius = static_cast<int>(bloom.radius);
    const bool spreads = radius >= 1 && bloom.strength > 0;
    if (spreads) {
        spread_.assign(pixels, Vec3{0, 0, 0});
        other_.resize(pixels);
        for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
            const Vec3 here = light[pixel];
            const float brightest = std::max({here.x, here.y, here.z});
            if (!(brightest > bloom.threshold)) continue;
            spread_[pixel] = here * ((brightest - bloom.threshold) / brightest);
            ++told.bright;
        }
        if (told.bright != 0)
            for (int pass = 0; pass < 3; ++pass) {
                for (int y = 0; y < height; ++y) box_line(&spread_[static_cast<std::size_t>(y) * width], &other_[static_cast<std::size_t>(y) * width], width, 1, radius);
                for (int x = 0; x < width; ++x) box_line(&other_[x], &spread_[x], height, width, radius);
            }
    } else {
        spread_.clear();
    }
    for (std::size_t pixel = 0; pixel < pixels; ++pixel) {
        Vec3 shown = light[pixel];
        if (spreads) shown = shown + spread_[pixel] * bloom.strength;
        if (tone.filmic || tone.exposure != 1) shown = tone.of(shown);
        colour[pixel * 4] = encoded(shown.x), colour[pixel * 4 + 1] = encoded(shown.y), colour[pixel * 4 + 2] = encoded(shown.z), colour[pixel * 4 + 3] = 255;
    }
    return told;
}

}  // namespace xrender::mesh
