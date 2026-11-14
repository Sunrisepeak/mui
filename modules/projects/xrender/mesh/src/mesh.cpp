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
    Vec4 uv{0, 0, 0, 0};       // the mesh's place in its pictures: its first texture coordinates, and its second
    Vec4 tangent{0, 0, 0, 0};  // in the world; all 0: the mesh has none
    Vec4 colour{1, 1, 1, 1};   // the mesh's own at the corner (`Mesh::colours`), or white
};
Vec4 mixed(const Vec4& from, const Vec4& to, float share) noexcept {
    return {from.x + (to.x - from.x) * share, from.y + (to.y - from.y) * share, from.z + (to.z - from.z) * share, from.w + (to.w - from.w) * share};
}
// Of a corner's (or a difference's) two sets of texture coordinates, the one `set` names.
Vec2 set_of(const Vec4& uv, std::uint8_t set) noexcept { return set ? Vec2{uv.z, uv.w} : Vec2{uv.x, uv.y}; }
Corner mixed(const Corner& from, const Corner& to, float share) noexcept {
    return {mixed(from.clip, to.clip, share), mixed(from.before, to.before, share), math::mix(from.normal, to.normal, share), math::mix(from.place, to.place, share),
            mixed(from.uv, to.uv, share),
            {from.tangent.x + (to.tangent.x - from.tangent.x) * share, from.tangent.y + (to.tangent.y - from.tangent.y) * share, from.tangent.z + (to.tangent.z - from.tangent.z) * share,
             from.tangent.w},
            mixed(from.colour, to.colour, share)};
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
    bool  physical = false;
    float inner_cosine = 2;
    // What of its light comes `apart` away along `along` (of length 1, from the place toward the lamp): `near` is
    // windowed's 1 − (apart / range)², the passes taking its square as before.
    [[nodiscard]] float physical_share(float apart, Vec3 along) const noexcept {
        const float edge = kind == scene::LightKind::spot ? soft_edge(cone_cosine, inner_cosine, -math::dot(along, toward)) : 1.0f;
        return physical_reach(range, apart) * edge;
    }
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
    const Texture* clearness = nullptr;  // how much goes through (red), and how thick it is (green)
    const Texture* thick = nullptr;
    Material       own;         // the draw's material as it says, before any picture
    std::vector<Laid>* laid = nullptr;  // the draw is seen through: its samples are kept here, to be laid over the frame
    float          shaded = 1;  // the share of the light from everywhere the shade picture lets through here
    bool           turned_over = false;  // the instance's place mirrors it: its triangles go round the other way as seen
    bool           coloured = false;     // the mesh has a colour at each corner
    // The clear coat's pictures (`Material::coat`, `coat_rough`, `coat_normal`), or none, and the coat's normal at
    // the pixel being shaded: the surface's own before its normal picture, or the coat's own picture's.
    const Texture* coat = nullptr;
    const Texture* coat_rough = nullptr;
    const Texture* coat_normal_picture = nullptr;
    const Texture* sheen_picture = nullptr;
    const Texture* anisotropy_picture = nullptr;
    const Texture* film = nullptr;        // how much film (red), how thick (green)
    const Texture* film_thick = nullptr;
    Vec3           drawn_out{0, 0, 0};  // the way the highlight is drawn out along at this pixel, or none
    Vec3           coat_normal{0, 0, 1};
    // What lets light through shows the light of what is whole behind it (`Material::transmission`): that light
    // at every size, the view's clip space (unjittered) and the instance's place and its inverse.
    const Texture* behind = nullptr;
    Mat4           clip, world, back;

    // The light that comes through the surface at `place` (facing the eye along `normal`) from what is behind
    // it: where the eye's ray, bent into the surface (by `ior`) and gone its thickness, leaves it — straight
    // behind for a thin wall — read from the light of what is whole there, blurred as rough as the surface is,
    // through its colour and what its inside takes of the way gone. `own`: the pixel's place in the frame. Where
    // nothing is drawn behind, the background shows through: `clear`, how much of the pixel it is.
    struct ComeThrough {
        Vec3  light;
        float clear = 0;
    };
    [[nodiscard]] ComeThrough transmitted(Vec3 normal, Vec3 place, Vec2 own) const noexcept {
        const float share = std::clamp(material.transmission, 0.0f, 1.0f) * (1.0f - std::clamp(material.metallic, 0.0f, 1.0f));
        if (!behind || behind->sizes.empty() || !(share > 0)) return {};
        Vec3 leaves = place, through{1, 1, 1};
        if (material.thickness > 0) {
            const Vec3 in = math::normalized(place - eye);
            const float bends = 1.0f / std::max(material.ior, 1e-3f), facing = -math::dot(normal, in);
            const float under = 1.0f - bends * bends * (1.0f - facing * facing);
            const Vec3 way = under < 0 ? in : in * bends + normal * (bends * facing - std::sqrt(under));
            // The thickness is the mesh's own: the way taken into the mesh, so long there, and back to the world.
            const Vec3 own_way = math::normalized(math::direction(back, way));
            const Vec3 gone = math::direction(world, own_way * material.thickness);
            leaves = place + gone;
            const float length = math::length(gone);
            if (material.attenuation_distance > 0 && material.attenuation_distance < std::numeric_limits<float>::infinity() && length > 0) {
                const double times = static_cast<double>(length) / static_cast<double>(material.attenuation_distance);
                const auto of = [&](float colour) { return colour >= 1 ? 1.0f : colour <= 0 ? 0.0f : static_cast<float>(math::det::pow(static_cast<double>(colour), times)); };
                through = {of(material.attenuation.x), of(material.attenuation.y), of(material.attenuation.z)};
            }
        }
        const Vec4 at = clip * Vec4{leaves, 1.0f};
        const Vec2 there = at.w > 0 ? Vec2{at.x / at.w * 0.5f + 0.5f, 0.5f - at.y / at.w * 0.5f} : own;
        // As rough as the surface, the more so the more it bends (none at an index of 1, all at 1.5 and up): its
        // footprint the frame's side to that power (three.js's measure, as a texel count).
        const float rough = std::clamp(material.roughness, 0.0f, 1.0f) * std::clamp(material.ior * 2.0f - 2.0f, 0.0f, 1.0f);
        const float footprint = static_cast<float>(math::det::pow(static_cast<double>(std::max(behind->width, behind->height)), static_cast<double>(rough)));
        const Texture::Seen seen = behind->seen_at(there, footprint);
        const Vec3 tint = math::scaled(material.base, through);
        return {math::scaled(tint, seen.light) * (share * seen.cover), share * (1.0f - seen.cover) * (tint.x + tint.y + tint.z) / 3.0f};
    }

    // Whether anything is between `from` and a light `reach` away along `toward` (of length 1). The ray is taken
    // into each mesh's own space as it is — not made of length 1 again — so a distance along it is the world's.
    bool hidden_from(Vec3 from, Vec3 toward, float reach) noexcept {
        ++stats.rays;
        const geom::Ray ray{from, toward};
        for (const Caster& caster : casters) {
            const std::optional<float> box = geom::meet(ray, caster.bounds);
            if (!box || *box > reach) continue;
            const geom::Ray within{math::point(caster.back, from), math::direction(caster.back, toward)};
            if (meet(within, *caster.mesh, *caster.tree, caster.material, caster.picture, reach)) {
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

    // The light from everywhere: the picture's about this normal where the surroundings are a picture, else the
    // one constant.
    [[nodiscard]] Vec3 from_all_about(Vec3 normal) const noexcept {
        return options.environment.on && options.environment.picture ? options.environment.picture->falling(normal) : options.ambient;
    }

    Vec3 lit(Vec3 normal, Vec3 place) noexcept {
        if (material.unlit) return material.base + material.emissive;
        if (!material.matt()) return shone(normal, place);
        Vec3 light = math::scaled(from_all_about(normal), reaching(open_to(normal, place))) * shaded;
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
            light = light + lamp.colour * (lamp.physical ? facing * lamp.physical_share(apart, along) : facing * near * near);
        }
        return math::scaled(material.base * (1.0f - std::clamp(material.transmission, 0.0f, 1.0f)), light) + material.emissive;
    }

    // The same lights on a surface that is not matt: each light's share by `reflected`.
    Vec3 shone(Vec3 normal, Vec3 place) noexcept {
        const Vec3 toward_eye = math::normalized(eye - place);
        // With a picture the surface's own colour takes what falls on it and what it mirrors is `mirrored`'s
        // alone — by the surface's shade too, as glTF's occlusion is of all the light from about; without, the
        // constant is given to both (and the three colours mirrored besides).
        const bool pictured = options.environment.on && options.environment.picture;
        const Vec3 takes = pictured ? material.base * ((1.0f - std::clamp(material.metallic, 0.0f, 1.0f)) * (1.0f - std::clamp(material.transmission, 0.0f, 1.0f))) : reflected_ambient(material);
        const float open = open_to(normal, place);
        // A clear coat: the same lights on a layer of its own over the surface (`Material::clearcoat`), mirroring
        // 4% head on, by its own normal and roughness; what comes up from under it is less by what it mirrors.
        const float coated = std::clamp(material.clearcoat, 0.0f, 1.0f);
        Material layer;
        layer.base = {0, 0, 0}, layer.roughness = material.clearcoat_roughness, layer.metallic = 0;
        Vec3 coat{0, 0, 0};
        const Vec3 under = math::scaled(math::scaled(takes, from_all_about(normal)), reaching(open)) * shaded;
        // A sheen: the fibres' own light, and what comes from under them less by what they take of an even light.
        const bool sheened = material.sheen.x > 0 || material.sheen.y > 0 || material.sheen.z > 0;
        Vec3 sheen{0, 0, 0};
        if (sheened) sheen = math::scaled(math::scaled(from_all_about(normal), material.sheen), reaching(open)) * (sheen_share(std::max(math::dot(normal, toward_eye), 0.0f), material.sheen_roughness) * shaded);
        Vec3 sent = coated > 0 || sheened ? under : under + material.emissive;
        const bool drawn = material.anisotropy > 0 && math::dot(drawn_out, drawn_out) > 0;
        if (options.environment.on) sent = sent + mirrored(material, options.environment, drawn ? drawn_out_normal(material, normal, drawn_out, toward_eye) : normal, toward_eye) * (pictured ? shaded : 1.0f);
        const auto highlight = [&](Vec3 toward_light, float facing) { return drawn ? reflected_drawn_out(material, normal, drawn_out, toward_eye, toward_light, facing) : reflected(material, normal, toward_eye, toward_light, facing); };
        if (coated > 0) {
            if (!pictured) coat = math::scaled(math::scaled(reflected_ambient(layer), from_all_about(coat_normal)), reaching(open)) * shaded;
            if (options.environment.on) coat = coat + mirrored(layer, options.environment, coat_normal, toward_eye) * (pictured ? shaded : 1.0f);
        }
        const Vec3 lifted = place + normal * 2e-3f;
        for (const Lamp& lamp : lamps) {
            if (lamp.kind == scene::LightKind::sun) {
                const float facing = -math::dot(normal, lamp.toward), coat_facing = coated > 0 ? -math::dot(coat_normal, lamp.toward) : 0.0f;
                if (!(facing > 0) && !(coat_facing > 0)) continue;
                if (!casters.empty() && hidden_from(lifted, -lamp.toward, std::numeric_limits<float>::max())) continue;
                if (facing > 0) sent = sent + math::scaled(highlight(-lamp.toward, facing), lamp.colour);
                if (sheened && facing > 0) sheen = sheen + math::scaled(material.sheen, lamp.colour) * sheen_reflected(material.sheen_roughness, normal, toward_eye, -lamp.toward, facing);
                if (coat_facing > 0) coat = coat + math::scaled(reflected(layer, coat_normal, toward_eye, -lamp.toward, coat_facing), lamp.colour);
                continue;
            }
            const Vec3 to = lamp.at - place;
            const float apart = math::length(to);
            if (!(apart < lamp.range) || !(apart > 0)) continue;
            const Vec3 along = to * (1.0f / apart);
            const float facing = math::dot(normal, along), coat_facing = coated > 0 ? math::dot(coat_normal, along) : 0.0f;
            if (!(facing > 0) && !(coat_facing > 0)) continue;
            if (lamp.kind == scene::LightKind::spot && -math::dot(along, lamp.toward) < lamp.cone_cosine) continue;
            if (!casters.empty() && hidden_from(lifted, along, apart)) continue;
            const float near = 1.0f - (apart / lamp.range) * (apart / lamp.range);
            const Vec3 arriving = lamp.colour * (lamp.physical ? lamp.physical_share(apart, along) : near * near);
            if (facing > 0) sent = sent + math::scaled(highlight(along, facing), arriving);
            if (sheened && facing > 0) sheen = sheen + math::scaled(material.sheen, arriving) * sheen_reflected(material.sheen_roughness, normal, toward_eye, along, facing);
            if (coat_facing > 0) coat = coat + math::scaled(reflected(layer, coat_normal, toward_eye, along, coat_facing), arriving);
        }
        if (!(coated > 0) && !sheened) return sent;
        if (sheened) {
            const float brightest = std::max({material.sheen.x, material.sheen.y, material.sheen.z});
            sent = sent * (1.0f - brightest * sheen_share(std::max(math::dot(normal, toward_eye), 0.0f), material.sheen_roughness)) + sheen;
        }
        if (!(coated > 0)) return material.emissive + sent;
        // How much the coat mirrors of the eye's way (Schlick, 4% head on): that much less comes up through it.
        const float glance = 1.0f - std::clamp(math::dot(coat_normal, toward_eye), 0.0f, 1.0f), glance5 = glance * glance * glance * glance * glance;
        const float coat_mirrors = 0.04f + 0.96f * glance5;
        return material.emissive + sent * (1.0f - coated * coat_mirrors) + coat * coated;
    }

    // One triangle, its corners inside the view's volume. True where it faces the view (and so was filled).
    bool fill(const Corner& first, const Corner& second, const Corner& third) noexcept {
        // Counter-clockwise as seen is its front. With y down that is a negative area by the rule below, so the
        // corners are taken the other way round: a front is above 0, and "inside" is above 0 for every edge.
        // An instance its place mirrors (a negative scale) shows its fronts the other way round.
        std::array<const Corner*, 3> corners{&first, turned_over ? &second : &third, turned_over ? &third : &second};
        // For a normal picture on a mesh with no tangents: the way the surface goes as the picture's first
        // coordinate grows, and against its second (up the picture), from this triangle's own corners.
        const bool tangents = !(first.tangent.x == 0 && first.tangent.y == 0 && first.tangent.z == 0);
        std::array<Vec3, 2> bent{};
        if ((normal_picture || own.anisotropy > 0) && !tangents) {
            const Vec3 one = corners[1]->place - corners[0]->place, two = corners[2]->place - corners[0]->place;
            const Placing& laid_so = own.placing.normal;
            const Vec2 first_uv = set_of(corners[0]->uv, laid_so.set), second_uv = set_of(corners[1]->uv, laid_so.set), third_uv = set_of(corners[2]->uv, laid_so.set);
            const Vec2 uv_one = laid_so.moved({second_uv.x - first_uv.x, second_uv.y - first_uv.y}), uv_two = laid_so.moved({third_uv.x - first_uv.x, third_uv.y - first_uv.y});
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
        double area = (x[1] - x[0]) * (y[2] - y[0]) - (y[1] - y[0]) * (x[2] - x[0]);
        if (area < 0 && own.two_sided) {
            // Seen from behind and to be seen: the same triangle gone round the other way.
            std::swap(corners[1], corners[2]), std::swap(x[1], x[2]), std::swap(y[1], y[2]), std::swap(z[1], z[2]), std::swap(inverse[1], inverse[2]);
            area = -area;
        }
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
                // What varies across the surface varies with 1/w across the screen.
                const std::array<float, 3> scaled{share[0] * inverse[0], share[1] * inverse[1], share[2] * inverse[2]};
                const float whole = 1.0f / (scaled[0] + scaled[1] + scaled[2]);
                const std::array<float, 3> part{scaled[0] * whole, scaled[1] * whole, scaled[2] * whole};
                Vec3 normal = math::normalized(corners[0]->normal * part[0] + corners[1]->normal * part[1] + corners[2]->normal * part[2]);
                const Vec3 place = corners[0]->place * part[0] + corners[1]->place * part[1] + corners[2]->place * part[2];
                // A surface seen from both sides faces the eye wherever it is seen: by its own normal at the pixel,
                // whichever way round its triangle goes (what a device's shader can ask as well).
                if (own.two_sided && math::dot(normal, eye - place) < 0) normal = normal * -1.0f;
                const Vec4 before{corners[0]->before.x * part[0] + corners[1]->before.x * part[1] + corners[2]->before.x * part[2],
                                  corners[0]->before.y * part[0] + corners[1]->before.y * part[1] + corners[2]->before.y * part[2], 0,
                                  corners[0]->before.w * part[0] + corners[1]->before.w * part[1] + corners[2]->before.w * part[2]};
                Vec2 moved{0, 0};
                if (!still && before.w > 0) {
                    const float was_x = (before.x / before.w * 0.5f + 0.5f) * static_cast<float>(out.width), was_y = (0.5f - before.y / before.w * 0.5f) * static_cast<float>(out.height);
                    // The sample is `jitter` aside of the pixel's middle; the surface there, seen without it, is not.
                    moved = {static_cast<float>(column) + 0.5f - options.jitter.x - was_x, static_cast<float>(row) + 0.5f - options.jitter.y - was_y};
                }
                float hides = own.opacity;  // how much of what is behind it the surface hides here
                if (coloured) {
                    const auto at = [&](float Vec4::* part_of) { return (*corners[0]).colour.*part_of * part[0] + (*corners[1]).colour.*part_of * part[1] + (*corners[2]).colour.*part_of * part[2]; };
                    material.base = math::scaled(base, Vec3{at(&Vec4::x), at(&Vec4::y), at(&Vec4::z)}), hides = own.opacity * at(&Vec4::w);
                }
                coat_normal = normal;
                Vec3 drawn_said{1, 0.5f, 1};  // the anisotropy picture's say here: along the tangent, whole
                if (texture || surface || normal_picture || glow || shade || clearness || thick || coat || coat_rough || coat_normal_picture || sheen_picture || anisotropy_picture || film || film_thick) {
                    // The surface's place in the picture here, a pixel to the right and a pixel down: how many texels
                    // a pixel covers is how far apart they are.
                    const auto uv_at = [&](double at_x, double at_y) {
                        const std::array<float, 3> there{static_cast<float>(edge(1, 2, at_x, at_y) / area) * inverse[0], static_cast<float>(edge(2, 0, at_x, at_y) / area) * inverse[1],
                                                         static_cast<float>(edge(0, 1, at_x, at_y) / area) * inverse[2]};
                        const float all = 1.0f / (there[0] + there[1] + there[2]);
                        const auto of = [&](float Vec4::* part_of) { return ((*corners[0]).uv.*part_of * there[0] + (*corners[1]).uv.*part_of * there[1] + (*corners[2]).uv.*part_of * there[2]) * all; };
                        return Vec4{of(&Vec4::x), of(&Vec4::y), of(&Vec4::z), of(&Vec4::w)};
                    };
                    // The differences are taken as a device takes them: within the pixel's own two by two (columns
                    // and rows paired from the first), so an odd pixel looks back to its pair and an even one ahead.
                    const double to_pair_x = column % 2 ? -256.0 : 256.0, to_pair_y = row % 2 ? -256.0 : 256.0;
                    const Vec4 here_both = uv_at(px, py), beside = uv_at(px + to_pair_x, py), below = uv_at(px, py + to_pair_y);
                    const float way_x = column % 2 ? -1.0f : 1.0f, way_y = row % 2 ? -1.0f : 1.0f;
                    const Vec4 across_both{(beside.x - here_both.x) * way_x, (beside.y - here_both.y) * way_x, (beside.z - here_both.z) * way_x, (beside.w - here_both.w) * way_x};
                    const Vec4 along_both{(below.x - here_both.x) * way_y, (below.y - here_both.y) * way_y, (below.z - here_both.z) * way_y, (below.w - here_both.w) * way_y};
                    // A picture's place here and its two differences, as its placing lays it.
                    struct Place {
                        Vec2 here, across, along;
                    };
                    const auto laid = [&](const Placing& placing) {
                        return Place{placing.of(set_of(here_both, placing.set)), placing.moved(set_of(across_both, placing.set)), placing.moved(set_of(along_both, placing.set))};
                    };
                    if (texture) {
                        const Place at = laid(own.placing.base);
                        const Texture::Seen there = texture->seen(at.here, at.across, at.along);
                        material.base = math::scaled(coloured ? material.base : base, there.light), hides *= there.cover;
                    }
                    if (own.cutout > 0 && hides < own.cutout) {
                        ++stats.cut;
                        continue;
                    }
                    if (surface) {
                        const Place at = laid(own.placing.surface);
                        const Vec3 said = surface->over(at.here, at.across, at.along);
                        material.roughness = own.roughness * said.y, material.metallic = own.metallic * said.z;
                    }
                    if (glow) {
                        const Place at = laid(own.placing.glow);
                        material.emissive = math::scaled(own.emissive, glow->over(at.here, at.across, at.along));
                    }
                    if (clearness) {
                        const Place at = laid(own.placing.clearness);
                        material.transmission = own.transmission * clearness->over(at.here, at.across, at.along).x;
                    }
                    if (thick) {
                        const Place at = laid(own.placing.thick);
                        material.thickness = own.thickness * thick->over(at.here, at.across, at.along).y;
                    }
                    if (shade) {
                        const Place at = laid(own.placing.shade);
                        shaded = 1.0f + own.shade_strength * (shade->over(at.here, at.across, at.along).x - 1.0f);
                    }
                    // A normal picture's say about `about`: the picture's three ways in the world here — along it (the
                    // mesh's tangent, or the way the surface goes as the picture's first coordinate grows), up it, and
                    // out (the normal) — and the normal it turns them to.
                    const auto bent_by = [&](const Texture& picture, const Placing& placing, float scale, Vec3 about) {
                        Vec3 right_way = bent[0], up_way = bent[1];
                        if (tangents) {
                            const Vec3 said{corners[0]->tangent.x * part[0] + corners[1]->tangent.x * part[1] + corners[2]->tangent.x * part[2],
                                            corners[0]->tangent.y * part[0] + corners[1]->tangent.y * part[1] + corners[2]->tangent.y * part[2],
                                            corners[0]->tangent.z * part[0] + corners[1]->tangent.z * part[1] + corners[2]->tangent.z * part[2]};
                            right_way = said, up_way = math::cross(about, said) * corners[0]->tangent.w;
                        }
                        right_way = right_way - about * math::dot(about, right_way);
                        if (!(math::dot(right_way, right_way) > 0)) return about;
                        right_way = math::normalized(right_way);
                        const Vec3 other = math::cross(about, right_way);
                        up_way = math::dot(other, up_way) < 0 ? other * -1.0f : other;
                        const Place at = laid(placing);
                        const Vec3 said = picture.over(at.here, at.across, at.along);
                        const Vec3 way{(said.x * 2 - 1) * scale, (said.y * 2 - 1) * scale, said.z * 2 - 1};
                        const Vec3 turned = right_way * way.x + up_way * way.y + about * way.z;
                        return math::dot(turned, turned) > 0 ? math::normalized(turned) : about;
                    };
                    if (coat) {
                        const Place at = laid(own.placing.coat);
                        material.clearcoat = own.clearcoat * coat->over(at.here, at.across, at.along).x;
                    }
                    if (coat_rough) {
                        const Place at = laid(own.placing.coat_rough);
                        material.clearcoat_roughness = own.clearcoat_roughness * coat_rough->over(at.here, at.across, at.along).y;
                    }
                    if (film) {
                        const Place at = laid(own.placing.film);
                        material.iridescence = own.iridescence * film->over(at.here, at.across, at.along).x;
                    }
                    if (film_thick) {
                        const Place at = laid(own.placing.film_thick);
                        material.film_thickness = own.film_thinnest + (own.film_thickness - own.film_thinnest) * film_thick->over(at.here, at.across, at.along).y;
                    }
                    if (anisotropy_picture) {
                        const Place at = laid(own.placing.anisotropy);
                        drawn_said = anisotropy_picture->over(at.here, at.across, at.along);
                    }
                    if (sheen_picture) {
                        const Place at = laid(own.placing.sheen);
                        material.sheen = math::scaled(own.sheen, sheen_picture->over(at.here, at.across, at.along));
                    }
                    // The coat's normal is the surface's own, before its normal picture — or its own picture's.
                    if (coat_normal_picture) coat_normal = bent_by(*coat_normal_picture, own.placing.coat_normal, own.coat_normal_scale, normal);
                    if (normal_picture) normal = bent_by(*normal_picture, own.placing.normal, own.normal_scale, normal);
                    ++stats.textured;
                }
                if (own.cutout > 0 && hides < own.cutout) {  // with no picture: the material's own opacity alone
                    ++stats.cut;
                    continue;
                }
                if (laid && !(hides > 0)) continue;  // seen through wholly: nothing of it
                if (!laid) ++stats.written;
                at_pixel = pixel;
                // The way the highlight is drawn out along: the tangent (the mesh's, or the way the picture's first
                // coordinate grows) turned by the material's rotation and its picture's way, in the surface.
                drawn_out = {0, 0, 0};
                if (own.anisotropy > 0) {
                    material.anisotropy = own.anisotropy * drawn_said.z;
                    Vec3 right_way = bent[0], up_way = bent[1];
                    if (tangents) {
                        right_way = {corners[0]->tangent.x * part[0] + corners[1]->tangent.x * part[1] + corners[2]->tangent.x * part[2],
                                     corners[0]->tangent.y * part[0] + corners[1]->tangent.y * part[1] + corners[2]->tangent.y * part[2],
                                     corners[0]->tangent.z * part[0] + corners[1]->tangent.z * part[1] + corners[2]->tangent.z * part[2]};
                        up_way = math::cross(normal, right_way) * corners[0]->tangent.w;
                    }
                    right_way = right_way - normal * math::dot(normal, right_way);
                    if (math::dot(right_way, right_way) > 0) {
                        right_way = math::normalized(right_way);
                        const Vec3 other = math::cross(normal, right_way);
                        up_way = math::dot(other, up_way) < 0 ? other * -1.0f : other;
                        const Vec2 said{drawn_said.x * 2 - 1, drawn_said.y * 2 - 1};
                        const float turn_cos = static_cast<float>(math::det::cos(static_cast<double>(own.anisotropy_rotation))), turn_sin = static_cast<float>(math::det::sin(static_cast<double>(own.anisotropy_rotation)));
                        const Vec2 way{said.x * turn_cos - said.y * turn_sin, said.x * turn_sin + said.y * turn_cos};
                        const Vec3 along = right_way * way.x + up_way * way.y;
                        if (math::dot(along, along) > 0) drawn_out = math::normalized(along);
                    }
                }
                Vec3 colour = lit(normal, place);
                float clear = 0;  // how much of the background shows through it
                if (behind && !material.unlit) {
                    const ComeThrough came = transmitted(normal, place, {(static_cast<float>(column) + 0.5f) / static_cast<float>(out.width), (static_cast<float>(row) + 0.5f) / static_cast<float>(out.height)});
                    colour = colour + came.light + options.background * came.clear, clear = came.clear;
                }
                if (fog) {
                    const Vec3 to = place - eye;
                    const float apart = math::length(to);
                    const float share = apart > 0 ? through(options.fog, eye, to * (1.0f / apart), apart) : 1.0f;
                    colour = options.fog.colour + (colour - options.fog.colour) * share;
                    ++stats.fogged;
                }
                if (laid) {
                    ++stats.through;
                    laid->push_back({{static_cast<std::uint32_t>(pixel), id, depth, std::min(hides, 1.0f)}, colour});
                    continue;
                }
                out.depth[pixel] = depth, out.normal[pixel] = normal, out.velocity[pixel] = moved, out.ids[pixel] = id, out.light[pixel] = colour, out.clear[pixel] = clear;
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
    colour.resize(pixels * 4), light.resize(pixels), depth.resize(pixels), normal.resize(pixels), velocity.resize(pixels), ids.resize(pixels), clear.resize(pixels);
}

Placing Placing::made(Vec2 offset, float turn, Vec2 scale, std::uint8_t set) noexcept {
    const float cosine = static_cast<float>(math::det::cos(static_cast<double>(turn))), sine = static_cast<float>(math::det::sin(static_cast<double>(turn)));
    return {{cosine * scale.x, sine * scale.y, offset.x}, {-sine * scale.x, cosine * scale.y, offset.y}, set};
}

LampShape lamp_shape(const scene::Light& light) noexcept {
    LampShape shape;
    shape.physical = light.falloff == scene::Falloff::physical;
    shape.colour = shape.physical ? light.colour * static_cast<float>(static_cast<double>(light.intensity) / math::det::pi) : light.colour * light.intensity;
    shape.cone_cosine = static_cast<float>(math::det::cos(static_cast<double>(light.cone)));
    if (shape.physical && light.inner_cone >= 0 && light.inner_cone < light.cone) shape.inner_cosine = static_cast<float>(math::det::cos(static_cast<double>(light.inner_cone)));
    return shape;
}
float physical_reach(float range, float apart) noexcept {
    const float share = apart / range, fourth = share * share * share * share;
    const float window = std::clamp(1.0f - fourth, 0.0f, 1.0f);
    return window * window / std::max(apart * apart, 1e-4f);
}
float soft_edge(float cone_cosine, float inner_cosine, float cosine) noexcept {
    if (!(inner_cosine <= 1)) return cosine >= cone_cosine ? 1.0f : 0.0f;
    const float share = std::clamp((cosine - cone_cosine) / std::max(inner_cosine - cone_cosine, 1e-6f), 0.0f, 1.0f);
    return share * share;
}
Mirrors mirrors_of(const Material& material) noexcept {
    const float metal = std::clamp(material.metallic, 0.0f, 1.0f), strength = std::clamp(material.specular, 0.0f, 1.0f);
    // Worked out in double: at an index of 1.5 exactly the float nearest 4%.
    const double ratio = (static_cast<double>(material.ior) - 1.0) / (static_cast<double>(material.ior) + 1.0);
    const float own = static_cast<float>(ratio * ratio);
    const Vec3 dielectric{std::min(own * material.specular_colour.x, 1.0f) * strength, std::min(own * material.specular_colour.y, 1.0f) * strength, std::min(own * material.specular_colour.z, 1.0f) * strength};
    return {dielectric * (1.0f - metal) + material.base * metal, 1.0f - (1.0f - strength) * (1.0f - metal)};
}
Vec3 film_mirrors(float film_ior, float thickness, float seen, Vec3 head_on) noexcept {
    const auto square = [](double value) { return value * value; };
    const auto schlick = [](double head, double cosine) { const double glance = 1.0 - cosine; return head + (1.0 - head) * glance * glance * glance * glance * glance; };
    const double outside = 1.0, cosine = std::clamp(static_cast<double>(seen), 0.0, 1.0);
    // A film no thickness is the air (a smooth step to the film's index over the first 0.03 nanometres).
    const double step = std::clamp(static_cast<double>(thickness) / 0.03, 0.0, 1.0), smooth = step * step * (3.0 - 2.0 * step);
    const double film = outside + (static_cast<double>(film_ior) - outside) * smooth;
    const double under_squared = 1.0 - square(outside / film) * (1.0 - cosine * cosine);
    if (under_squared < 0) return {1, 1, 1};  // all of it thrown back within the film
    const double under = math::det::sqrt(under_squared);
    const double first = schlick(square((film - outside) / (film + outside)), cosine), through = 1.0 - first;
    const double film_turn = film < outside ? math::det::pi : 0.0, back_turn = math::det::pi - film_turn;
    const double optical = 2.0 * film * static_cast<double>(thickness) * under;  // nanometres
    const std::array<double, 3> head{head_on.x, head_on.y, head_on.z};
    std::array<double, 3> out{};
    std::array<double, 3> base_index{}, second{}, turn{};
    for (int channel = 0; channel < 3; ++channel) {
        const double root = math::det::sqrt(std::clamp(head[static_cast<std::size_t>(channel)], 0.0, 0.9999));
        base_index[static_cast<std::size_t>(channel)] = (1.0 + root) / (1.0 - root);
        second[static_cast<std::size_t>(channel)] = schlick(square((base_index[static_cast<std::size_t>(channel)] - film) / (base_index[static_cast<std::size_t>(channel)] + film)), under);
        turn[static_cast<std::size_t>(channel)] = back_turn + (base_index[static_cast<std::size_t>(channel)] < film ? math::det::pi : 0.0);
    }
    // The eye's three colours' sensitivity to a path `difference` nanometres longer, turned by `shift`, as sRGB.
    const auto sensed = [](double difference, const std::array<double, 3>& shift) {
        const double phase = math::det::two_pi * difference * 1.0e-9;
        constexpr std::array<double, 3> value{5.4856e-13, 4.4201e-13, 5.2481e-13}, place{1.6810e+06, 1.7953e+06, 2.2084e+06}, spread{4.3278e+09, 9.3046e+09, 6.6121e+09};
        std::array<double, 3> xyz{};
        for (std::size_t channel = 0; channel < 3; ++channel)
            xyz[channel] = value[channel] * math::det::sqrt(math::det::two_pi * spread[channel]) * math::det::cos(place[channel] * phase + shift[channel]) * math::det::exp(-phase * phase * spread[channel]);
        xyz[0] += 9.7470e-14 * math::det::sqrt(math::det::two_pi * 4.5282e+09) * math::det::cos(2.2399e+06 * phase + shift[0]) * math::det::exp(-4.5282e+09 * phase * phase);
        for (double& one : xyz) one /= 1.0685e-7;
        return std::array<double, 3>{3.2404542 * xyz[0] - 1.5371385 * xyz[1] - 0.4985314 * xyz[2], -0.9692660 * xyz[0] + 1.8760108 * xyz[1] + 0.0415560 * xyz[2],
                                     0.0556434 * xyz[0] - 0.2040259 * xyz[1] + 1.0572252 * xyz[2]};
    };
    std::array<double, 3> compound{}, reflected_twice{}, order{};
    for (std::size_t channel = 0; channel < 3; ++channel) {
        compound[channel] = std::clamp(first * second[channel], 1e-5, 0.9999);
        reflected_twice[channel] = square(through) * second[channel] / (1.0 - compound[channel]);
        out[channel] = first + reflected_twice[channel];
        order[channel] = reflected_twice[channel] - through;
    }
    for (int times = 1; times <= 2; ++times) {
        std::array<double, 3> shift{};
        for (std::size_t channel = 0; channel < 3; ++channel) order[channel] *= math::det::sqrt(compound[channel]), shift[channel] = static_cast<double>(times) * turn[channel];
        const std::array<double, 3> felt = sensed(static_cast<double>(times) * optical, shift);
        for (std::size_t channel = 0; channel < 3; ++channel) out[channel] += order[channel] * 2.0 * felt[channel];
    }
    return {static_cast<float>(std::max(out[0], 0.0)), static_cast<float>(std::max(out[1], 0.0)), static_cast<float>(std::max(out[2], 0.0))};
}
Vec3 mirrors_at(const Material& material, float glance5, float seen) noexcept {
    const Mirrors ends = mirrors_of(material);
    const Vec3 plain = ends.head_on + (Vec3{ends.edge, ends.edge, ends.edge} - ends.head_on) * glance5;
    const float iridescent = std::clamp(material.iridescence, 0.0f, 1.0f);
    if (!(iridescent > 0)) return plain;
    return plain + (film_mirrors(material.film_ior, material.film_thickness, seen, ends.head_on) - plain) * iridescent;
}
Vec3 reflected(const Material& material, Vec3 normal, Vec3 toward_eye, Vec3 toward_light, float facing) noexcept {
    const float rough = std::clamp(material.roughness, 0.04f, 1.0f), metal = std::clamp(material.metallic, 0.0f, 1.0f);
    // What goes on through the surface (`transmission`) is not scattered back.
    const Vec3 matt = material.base * ((1.0f - metal) * (1.0f - std::clamp(material.transmission, 0.0f, 1.0f)) * facing);
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
    const Vec3 mirrored = mirrors_at(material, glance5, seen);
    return matt + math::scaled(mirrored, kept(material, seen)) * (facets * unhidden * 0.25f * facing);
}
Vec3 reflected_drawn_out(const Material& material, Vec3 normal, Vec3 along, Vec3 toward_eye, Vec3 toward_light, float facing) noexcept {
    const float rough = std::clamp(material.roughness, 0.04f, 1.0f), metal = std::clamp(material.metallic, 0.0f, 1.0f);
    const Vec3 matt = material.base * ((1.0f - metal) * (1.0f - std::clamp(material.transmission, 0.0f, 1.0f)) * facing);
    const float seen = math::dot(normal, toward_eye);
    if (!(seen > 0)) return matt;
    const float strength = std::clamp(material.anisotropy, 0.0f, 1.0f);
    const float alpha = rough * rough, along_alpha = alpha + (1.0f - alpha) * strength * strength, across_alpha = alpha;
    const Vec3 across = math::cross(normal, along);
    const Vec3 half = math::normalized(toward_eye + toward_light);
    const float half_along = math::dot(along, half) / along_alpha, half_across = math::dot(across, half) / across_alpha, half_up = math::dot(normal, half);
    const float spread = half_along * half_along + half_across * half_across + half_up * half_up;
    const float facets = 1.0f / (along_alpha * across_alpha * spread * spread);
    const auto reach = [&](Vec3 way, float up) {
        const float on_along = math::dot(along, way) * along_alpha, on_across = math::dot(across, way) * across_alpha;
        return std::sqrt(on_along * on_along + on_across * on_across + up * up);
    };
    const float visible = 0.5f / (facing * reach(toward_eye, seen) + seen * reach(toward_light, facing));
    const float eye_half = std::max(math::dot(toward_eye, half), 0.0f), glance = 1.0f - eye_half, glance5 = glance * glance * glance * glance * glance;
    const Vec3 mirrored = mirrors_at(material, glance5, seen);
    return matt + math::scaled(mirrored, kept(material, seen)) * (facets * visible * facing);
}
Vec3 drawn_out_normal(const Material& material, Vec3 normal, Vec3 along, Vec3 toward_eye) noexcept {
    const float rough = std::clamp(material.roughness, 0.04f, 1.0f), strength = std::clamp(material.anisotropy, 0.0f, 1.0f);
    const Vec3 across_eye = math::cross(along, toward_eye), toward = math::cross(across_eye, along);
    const float bend = 1.0f - strength * (1.0f - rough), bend4 = bend * bend * bend * bend;
    const Vec3 bent = toward + (normal - toward) * bend4;
    return math::dot(bent, bent) > 0 ? math::normalized(bent) : normal;
}
Vec3 mirrored(const Material& material, const Environment& environment, Vec3 normal, Vec3 toward_eye) noexcept {
    const float rough = std::clamp(material.roughness, 0.04f, 1.0f), metal = std::clamp(material.metallic, 0.0f, 1.0f);
    const float seen = std::max(math::dot(normal, toward_eye), 0.0f);
    const Vec3 way = normal * (2.0f * seen) - toward_eye;  // the eye's way, turned about the normal
    const float size = math::length(way);
    if (environment.picture) {
        const Mirrors ends = mirrors_of(material);
        const Vec2 share = environment.picture->share(seen, rough);
        // A rough surface's highlight is not about the eye's way turned over: its small faces lean it toward
        // the normal — by the highlight's own spread (the roughness to the fourth: nothing to speak of below
        // 0.3, wholly at the roughest), the lean that is nearest the light followed along rays at every
        // roughness tried (README: the lean, measured).
        const float spread = rough * rough, toward_way = 1.0f - spread * spread;
        const Vec3 turned = size > 0 ? way * (1.0f / size) : normal;
        const Vec3 leaned = normal + (turned - normal) * toward_way;
        const float leaned_size = math::length(leaned);
        Vec3 shared = ends.head_on * share.x + Vec3{share.y * ends.edge, share.y * ends.edge, share.y * ends.edge};
        // A film: what it mirrors at this look in place of Schlick's, over the share of an even light.
        if (material.iridescence > 0)
            shared = shared + (film_mirrors(material.film_ior, material.film_thickness, seen, ends.head_on) * (share.x + share.y) - shared) * std::clamp(material.iridescence, 0.0f, 1.0f);
        return math::scaled(math::scaled(shared, kept(material, seen)), environment.picture->blurred(leaned_size > 0 ? leaned * (1.0f / leaned_size) : normal, rough));
    }
    const Vec3 there = environment.toward(size > 0 ? way * (1.0f / size) : normal);
    const Vec3 blurred = there + (environment.level - there) * rough;
    const float glance = 1.0f - seen, glance5 = glance * glance * glance * glance * glance;
    const Mirrors ends = mirrors_of(material);
    // A rough surface mirrors less of a glancing look than a smooth one does.
    const Vec3 share = mirrors_at(material, glance5 * (1.0f - rough), seen);
    return math::scaled(share, blurred);
}
Vec3 reflected_ambient(const Material& material) noexcept {
    const float metal = std::clamp(material.metallic, 0.0f, 1.0f);
    return material.base * ((1.0f - metal) * (1.0f - std::clamp(material.transmission, 0.0f, 1.0f))) + mirrors_of(material).head_on;
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

namespace {

// Whether a surface that cuts itself out is there where a ray met triangle `corner` of `mesh`, `distance` along:
// its opacity, by the mesh's colour and its picture's cover at the place, against the cut.
bool there(const geom::Ray& ray, const geom::Mesh& mesh, const std::array<std::uint32_t, 3>& corner, float distance, const Material& material, const Texture* cover) noexcept {
    float hides = material.opacity;
    const std::vector<Vec2>& uvs = material.placing.base.set ? mesh.second_uvs : mesh.uvs;
    const bool pictured = cover && corner[0] < uvs.size() && corner[1] < uvs.size() && corner[2] < uvs.size();
    const bool coloured = corner[0] < mesh.colours.size() && corner[1] < mesh.colours.size() && corner[2] < mesh.colours.size();
    if (pictured || coloured) {
        // Where on the triangle: each corner's share is the area across from it.
        const Vec3 first = mesh.positions[corner[0]], second = mesh.positions[corner[1]], third = mesh.positions[corner[2]];
        const Vec3 met = ray.from + ray.along * distance, face = math::cross(second - first, third - first);
        const float whole = math::dot(face, face);
        if (whole > 0) {
            const float share_second = math::dot(math::cross(met - first, third - first), face) / whole, share_third = math::dot(math::cross(second - first, met - first), face) / whole;
            const float share_first = 1 - share_second - share_third;
            if (coloured) hides *= mesh.colours[corner[0]].w * share_first + mesh.colours[corner[1]].w * share_second + mesh.colours[corner[2]].w * share_third;
            if (pictured)
                hides *= cover->cover_at(material.placing.base.of({uvs[corner[0]].x * share_first + uvs[corner[1]].x * share_second + uvs[corner[2]].x * share_third,
                                                                   uvs[corner[0]].y * share_first + uvs[corner[1]].y * share_second + uvs[corner[2]].y * share_third}));
        }
    }
    return !(hides < material.cutout);
}
const Texture* cover_of(const Material& material, const Texture* picture, const geom::Mesh& mesh) noexcept {
    return material.cutout > 0 && picture && !picture->cover.empty() && !(material.placing.base.set ? mesh.second_uvs : mesh.uvs).empty() ? picture : nullptr;
}

}  // namespace

std::optional<geom::RayHit> meet(const geom::Ray& ray, const geom::Mesh& mesh, const geom::Bvh& tree, const Material* material, const Texture* picture, float farthest) {
    if (!material || !(material->cutout > 0)) return geom::meet(ray, mesh, tree, farthest);
    const Texture* cover = cover_of(*material, picture, mesh);
    return geom::nearest(
        tree, ray,
        [&](std::uint32_t triangle) -> std::optional<float> {
            const std::size_t at = static_cast<std::size_t>(triangle) * 3;
            if (at + 2 >= mesh.indices.size()) return std::nullopt;
            const std::array<std::uint32_t, 3> corner{mesh.indices[at], mesh.indices[at + 1], mesh.indices[at + 2]};
            if (corner[0] >= mesh.positions.size() || corner[1] >= mesh.positions.size() || corner[2] >= mesh.positions.size()) return std::nullopt;
            const std::optional<float> distance = geom::meet(ray, mesh.positions[corner[0]], mesh.positions[corner[1]], mesh.positions[corner[2]]);
            return distance && there(ray, mesh, corner, *distance, *material, cover) ? distance : std::nullopt;
        },
        farthest);
}

void Pass::draw(const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, const Assets& assets, Targets& out, const Options& options) {
    last_ = {};
    out.draws.assign(extracted.draws.begin(), extracted.draws.end());
    const std::size_t pixels = static_cast<std::size_t>(out.width) * out.height;
    if (out.depth.size() != pixels) out.resize(out.width, out.height);
    std::ranges::fill(out.depth, options.reversed_depth ? 0.0f : 1.0f);
    std::ranges::fill(out.normal, Vec3{}), std::ranges::fill(out.velocity, Vec2{0, 0}), std::ranges::fill(out.ids, Pixel{}), std::ranges::fill(out.light, options.background);
    out.through.clear(), laid_.clear();
    std::ranges::fill(out.clear, options.environment.on && options.environment.behind && options.environment.picture ? 0.0f : 1.0f);
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
        const LampShape shape = lamp_shape(light);
        lamps[lamp_count++] = {light.kind, shape.colour, light.at, math::normalized(light.toward), std::max(light.range, 1e-6f), shape.cone_cosine, shape.physical, shape.inner_cosine};
    }
    // What can cast a shadow: every instance of the scene with a tree, seen by this view or not.
    casters_.clear();
    if (options.shadows && !assets.trees.empty())
        for (const scene::Instance& instance : instances) {
            if (instance.hidden || !instance.casts || instance.mesh >= assets.meshes.size() || instance.mesh >= assets.trees.size() || !assets.meshes[instance.mesh] || !assets.trees[instance.mesh]) continue;
            const Material* material = instance.material < assets.materials.size() ? &assets.materials[instance.material] : nullptr;
            if (material && material->see_through) continue;  // it hides nothing from a light
            const std::optional<Mat4> back = math::inverse(instance.world);
            if (!back) continue;
            const Texture* picture = material && material->texture < assets.textures.size() ? assets.textures[material->texture] : nullptr;
            casters_.push_back({*back, geom::moved(instance.world, instance.bounds), &assets.meshes[instance.mesh]->mesh, assets.trees[instance.mesh], material, picture});
        }
    // The jitter: every corner moved by so much of a pixel — in clip space, by so much of its own w.
    Mat4 aside;
    if (out.width > 0 && out.height > 0) aside.at(3, 0) = 2.0f * options.jitter.x / static_cast<float>(out.width), aside.at(3, 1) = -2.0f * options.jitter.y / static_cast<float>(out.height);
    // Applied to each corner after the clipping (`aside * clip`: the column that multiplies the corner's own w).
    Raster raster{out, options, last_, std::span(lamps).first(lamp_count), casters_, {}, {}, true, aside};
    raster.fog = options.fog.density > 0 && camera.has_value();
    if (camera) raster.eye = math::point(*camera, {0, 0, 0});

    // Everything whole first; then what lets light through, over the light of what is behind it; then the surfaces
    // seen through, their samples kept to be laid over it.
    raster.clip = clip, raster.behind = nullptr;
    const auto stage_of = [](const Material& material) { return material.see_through ? 2 : material.transmission > 0 ? 1 : 0; };
    // The background through the fog: a surface infinitely far along each pixel's own ray — the picture of the
    // surroundings there, where that is what is behind.
    const auto paint_background = [&] {
    const bool pictured_behind = options.environment.on && options.environment.picture && options.environment.behind;
    if (const std::optional<Mat4> back = raster.fog || pictured_behind ? math::inverse(clip) : std::nullopt) {
        for (int row = 0; row < out.height; ++row)
            for (int column = 0; column < out.width; ++column) {
                const std::size_t pixel = static_cast<std::size_t>(row) * out.width + column;
                if (out.ids[pixel].draw != none) continue;
                const Vec3 through_pixel = math::point(*back, {(static_cast<float>(column) + 0.5f) / static_cast<float>(out.width) * 2 - 1,
                                                               1 - (static_cast<float>(row) + 0.5f) / static_cast<float>(out.height) * 2, 0.5f});
                const Vec3 to = through_pixel - raster.eye;
                const float apart = math::length(to);
                if (!(apart > 0)) continue;
                const Vec3 way = to * (1.0f / apart);
                const Vec3 far = pictured_behind ? options.environment.picture->toward(way) : options.background;
                const float share = raster.fog ? through(options.fog, raster.eye, way, std::numeric_limits<float>::infinity()) : 1.0f;
                const Vec3 colour = raster.fog ? options.fog.colour + (far - options.fog.colour) * share : far;
                out.light[pixel] = colour;
                out.colour[pixel * 4] = encoded(colour.x), out.colour[pixel * 4 + 1] = encoded(colour.y), out.colour[pixel * 4 + 2] = encoded(colour.z);
                if (raster.fog) ++last_.fogged;
            }
    }
    };

    for (const int stage : {0, 1, 2}) {
    // What is whole is drawn: the background behind it now, so that what lets light through sees it.
    if (stage == 1) paint_background();
    for (std::uint32_t number = 0; number < extracted.draws.size() && pixels; ++number) {
        const scene::Draw& draw = extracted.draws[number];
        if (draw.instance >= instances.size() || draw.mesh >= assets.meshes.size() || !assets.meshes[draw.mesh] || draw.material >= assets.materials.size()) {
            if (stage == 0) ++last_.missing;
            continue;
        }
        if (stage_of(assets.materials[draw.material]) != stage) continue;
        const bool seen_through = stage == 2;
        // The light of what is whole, at every size, once: what a surface that lets light through shows behind it.
        if (stage == 1 && !raster.behind) {
            there_.resize(out.ids.size());
            for (std::size_t pixel = 0; pixel < out.ids.size(); ++pixel) there_[pixel] = 1.0f - out.clear[pixel];
            behind_ = Texture::of_light(out.light, there_, out.width, out.height).value_or(Texture{});
            raster.behind = &behind_;
        }
        raster.laid = seen_through ? &laid_ : nullptr;
        ++last_.draws;
        const scene::Instance& instance = instances[draw.instance];
        const geom::ClusteredMesh& clustered = *assets.meshes[draw.mesh];
        const geom::Mesh& mesh = clustered.mesh;
        raster.material = assets.materials[draw.material];
        raster.base = raster.material.base;
        raster.own = raster.material, raster.shaded = 1;
        raster.turned_over = math::dot(math::cross(math::direction(instance.world, {1, 0, 0}), math::direction(instance.world, {0, 1, 0})), math::direction(instance.world, {0, 0, 1})) < 0;
        // A picture is drawn where the mesh has the texture coordinates it is laid by.
        const auto picture = [&](std::uint32_t which, const Placing& placing) {
            return (which < assets.textures.size() && !(placing.set ? mesh.second_uvs : mesh.uvs).empty()) ? assets.textures[which] : nullptr;
        };
        const Placings& placing = raster.material.placing;
        raster.texture = picture(raster.material.texture, placing.base), raster.surface = picture(raster.material.surface, placing.surface), raster.normal_picture = picture(raster.material.normal, placing.normal);
        raster.glow = picture(raster.material.glow, placing.glow), raster.shade = picture(raster.material.shade, placing.shade);
        raster.clearness = picture(raster.material.clearness, placing.clearness), raster.thick = picture(raster.material.thick, placing.thick);
        raster.coat = picture(raster.material.coat, placing.coat), raster.coat_rough = picture(raster.material.coat_rough, placing.coat_rough);
        raster.coat_normal_picture = picture(raster.material.coat_normal, placing.coat_normal), raster.sheen_picture = picture(raster.material.sheen_picture, placing.sheen);
        raster.anisotropy_picture = picture(raster.material.anisotropy_picture, placing.anisotropy);
        raster.film = picture(raster.material.film, placing.film), raster.film_thick = picture(raster.material.film_thick, placing.film_thick);
        raster.coloured = !mesh.colours.empty();
        raster.world = instance.world, raster.back = math::inverse(instance.world).value_or(Mat4{});
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
                if (even && !raster.own.two_sided && geom::faces_away(cluster, eye)) {
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
                                       {vertex < mesh.uvs.size() ? mesh.uvs[vertex].x : 0.0f, vertex < mesh.uvs.size() ? mesh.uvs[vertex].y : 0.0f,
                                        vertex < mesh.second_uvs.size() ? mesh.second_uvs[vertex].x : 0.0f, vertex < mesh.second_uvs.size() ? mesh.second_uvs[vertex].y : 0.0f},
                                       {}, vertex < mesh.colours.size() ? mesh.colours[vertex] : Vec4{1, 1, 1, 1}};
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

    }

    // The surfaces seen through, laid over what is behind them: each pixel's from the nearest on, each taking of
    // the pixel what it hides of what the nearer ones let through — the same light as laying them from the
    // farthest to the nearest, in an order that is the pixel's own (of two as deep, the earlier draw is nearer).
    if (!laid_.empty()) {
        std::ranges::sort(laid_, [&](const Laid& one, const Laid& other) {
            if (one.what.pixel != other.what.pixel) return one.what.pixel < other.what.pixel;
            if (one.what.depth != other.what.depth) return options.reversed_depth ? one.what.depth > other.what.depth : one.what.depth < other.what.depth;
            if (one.what.id.draw != other.what.id.draw) return one.what.id.draw < other.what.id.draw;
            return one.what.id.triangle < other.what.id.triangle;
        });
        out.through.reserve(laid_.size());
        for (std::size_t first = 0; first < laid_.size();) {
            const std::uint32_t pixel = laid_[first].what.pixel;
            float let = 1;
            Vec3 sum{0, 0, 0};
            std::size_t next = first;
            for (; next < laid_.size() && laid_[next].what.pixel == pixel; ++next) {
                Through what = laid_[next].what;
                const float hides = what.share;
                what.share = hides * let;
                sum = sum + laid_[next].light * what.share;
                let *= 1.0f - hides;
                out.through.push_back(what);
            }
            const Vec3 colour = sum + out.light[pixel] * let;
            out.light[pixel] = colour, out.clear[pixel] *= let;
            out.colour[std::size_t{pixel} * 4] = encoded(colour.x), out.colour[std::size_t{pixel} * 4 + 1] = encoded(colour.y), out.colour[std::size_t{pixel} * 4 + 2] = encoded(colour.z);
            first = next;
        }
    }

    before_ids_.clear(), before_world_.clear();
    for (const scene::Instance& instance : instances) before_ids_.push_back(instance.id), before_world_.push_back(instance.world);
    before_clip_ = clip;
    remembered_ = true;
}

std::span<const Through> through_at(const Targets& targets, int x, int y) noexcept {
    if (x < 0 || y < 0 || x >= targets.width || y >= targets.height) return {};
    const std::uint32_t pixel = static_cast<std::uint32_t>(y) * static_cast<std::uint32_t>(targets.width) + static_cast<std::uint32_t>(x);
    const auto found = std::ranges::equal_range(targets.through, pixel, {}, &Through::pixel);
    return {found.begin(), found.end()};
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
    if (source)
        members += std::format("\"instance\":{},\"mesh\":{},\"material\":{},\"cluster\":{},\"triangle\":{},\"depth\":{},\"normal\":[{},{},{}]", source->instance, source->mesh, source->material,
                               source->cluster, source->triangle, source->depth, source->normal.x, source->normal.y, source->normal.z);
    const std::span<const Through> through = through_at(targets, x, y);
    if (through.empty()) return;
    members += source ? ",\"through\":[" : "\"through\":[";
    for (std::size_t index = 0; index < through.size(); ++index) {
        if (through[index].id.draw >= targets.draws.size()) continue;
        const scene::Draw& draw = targets.draws[through[index].id.draw];
        members += std::format("{}{{\"instance\":{},\"material\":{},\"share\":{}}}", index ? "," : "", draw.id, draw.material, through[index].share);
    }
    members += "]";
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
        // In the mesh's own space, not made of length 1 again: a distance along it is the world's. What the frame
        // draws: fronts only unless the material is seen from both sides — a triangle met from behind is passed
        // through — and not where the material cuts the surface out.
        const geom::Ray within{math::point(*into, ray.from), math::direction(*into, ray.along)};
        const Material* material = instance.material < assets.materials.size() ? &assets.materials[instance.material] : nullptr;
        const Texture* cover = material ? cover_of(*material, material->texture < assets.textures.size() ? assets.textures[material->texture] : nullptr, mesh) : nullptr;
        const auto front = [&](std::uint32_t triangle) -> std::optional<float> {
            const std::array<std::uint32_t, 3> corner{mesh.indices[triangle * 3], mesh.indices[triangle * 3 + 1], mesh.indices[triangle * 3 + 2]};
            const Vec3 a = mesh.positions[corner[0]], b = mesh.positions[corner[1]], c = mesh.positions[corner[2]];
            if (!(material && material->two_sided) && math::dot(math::cross(b - a, c - a), within.along) >= 0) return std::nullopt;
            const std::optional<float> distance = geom::meet(within, a, b, c);
            return distance && material && material->cutout > 0 && !there(within, mesh, corner, *distance, *material, cover) ? std::nullopt : distance;
        };
        const std::optional<geom::RayHit> hit = geom::nearest(*assets.trees[instance.mesh], within, front, nearest);
        if (!hit || hit->distance >= nearest) continue;
        nearest = hit->distance;
        const Vec3 a = mesh.positions[mesh.indices[hit->item * 3]], b = mesh.positions[mesh.indices[hit->item * 3 + 1]], c = mesh.positions[mesh.indices[hit->item * 3 + 2]];
        const Vec4 clip = view.projection * view.view * Vec4{ray.from + ray.along * hit->distance, 1};
        Vec3 faces = math::normalized(math::direction(math::transposed(*into), math::cross(b - a, c - a)));
        if (math::dot(faces, ray.along) > 0) faces = faces * -1.0f;  // met from behind (a surface seen from both sides): the side that was met
        found = Source{instance.id, instance.mesh, instance.material, none, hit->item, clip.w != 0 ? clip.z / clip.w : 0.0f, faces};
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
    // A surface seen through reached the frame at each pixel it is laid over.
    for (std::size_t index = 0; index < targets.through.size(); ++index) {
        const Through& what = targets.through[index];
        if (what.id.draw >= out.size() || targets.width <= 0) continue;
        // Once a pixel, however many of its triangles lie over one another there.
        bool counted = false;
        for (std::size_t before = index; before-- > 0 && targets.through[before].pixel == what.pixel && !counted;) counted = targets.through[before].id.draw == what.id.draw;
        if (counted) continue;
        const int x = static_cast<int>(what.pixel % static_cast<std::uint32_t>(targets.width)), y = static_cast<int>(what.pixel / static_cast<std::uint32_t>(targets.width));
        Shown& one = out[what.id.draw];
        if (!one.pixels) one.x0 = x, one.y0 = y, one.x1 = x + 1, one.y1 = y + 1;
        one.x0 = std::min(one.x0, x), one.x1 = std::max(one.x1, x + 1), one.y0 = std::min(one.y0, y), one.y1 = std::max(one.y1, y + 1);
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

namespace {
// Each next size of a texture whose finest is made: half as wide and as high (one at least), a texel the mean of
// those it covers.
void sized(Texture& texture, bool covered) {
    // Each next size: half as wide and as high (one at least), a texel the mean of those it covers.
    for (int wide = texture.width, high = texture.height; wide > 1 || high > 1;) {
        const int next_wide = std::max(wide / 2, 1), next_high = std::max(high / 2, 1);
        const std::uint32_t from = texture.sizes.back();
        texture.sizes.push_back(static_cast<std::uint32_t>(texture.light.size()));
        for (int y = 0; y < next_high; ++y)
            for (int x = 0; x < next_wide; ++x) {
                const int x0 = std::min(x * 2, wide - 1), x1 = std::min(x * 2 + 1, wide - 1), y0 = std::min(y * 2, high - 1), y1 = std::min(y * 2 + 1, high - 1);
                const auto of = [&](int at_x, int at_y) { return texture.light[from + static_cast<std::size_t>(at_y) * static_cast<std::size_t>(wide) + static_cast<std::size_t>(at_x)]; };
                texture.light.push_back((of(x0, y0) + of(x1, y0) + of(x0, y1) + of(x1, y1)) * 0.25f);
                if (covered) {
                    const auto hides = [&](int at_x, int at_y) { return texture.cover[from + static_cast<std::size_t>(at_y) * static_cast<std::size_t>(wide) + static_cast<std::size_t>(at_x)]; };
                    texture.cover.push_back((hides(x0, y0) + hides(x1, y0) + hides(x0, y1) + hides(x1, y1)) * 0.25f);
                }
            }
        wide = next_wide, high = next_high;
    }
}
}  // namespace

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
    const std::size_t texels = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    bool covered = false;
    if (coding == Coding::colour)
        for (std::size_t texel = 0; texel < texels && !covered; ++texel) covered = rgba[texel * 4 + 3] != 255;
    for (std::size_t texel = 0; texel < texels; ++texel)
        texture.light.push_back(coding == Coding::colour ? Vec3{decoded[rgba[texel * 4]], decoded[rgba[texel * 4 + 1]], decoded[rgba[texel * 4 + 2]]}
                                                         : Vec3{static_cast<float>(rgba[texel * 4]) / 255.0f, static_cast<float>(rgba[texel * 4 + 1]) / 255.0f, static_cast<float>(rgba[texel * 4 + 2]) / 255.0f});
    if (covered) {
        texture.cover.reserve(texels * 4 / 3 + 2);
        for (std::size_t texel = 0; texel < texels; ++texel) {
            const float hides = static_cast<float>(rgba[texel * 4 + 3]) / 255.0f;
            texture.cover.push_back(hides);
            texture.light[texel] = texture.light[texel] * hides;
        }
    }
    sized(texture, covered);
    return texture;
}

std::optional<Texture> Texture::of_light(std::span<const Vec3> light, int width, int height) {
    if (width <= 0 || height <= 0 || light.size() < static_cast<std::size_t>(width) * static_cast<std::size_t>(height)) return std::nullopt;
    Texture texture;
    texture.width = width, texture.height = height, texture.across = Edge::stops, texture.down = Edge::stops;
    texture.sizes.push_back(0);
    texture.light.reserve(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4 / 3 + 2);
    texture.light.assign(light.begin(), light.begin() + static_cast<std::ptrdiff_t>(width) * height);
    sized(texture, false);
    return texture;
}

std::optional<Texture> Texture::of_light(std::span<const Vec3> light, std::span<const float> there, int width, int height) {
    const std::size_t texels = static_cast<std::size_t>(std::max(width, 0)) * static_cast<std::size_t>(std::max(height, 0));
    if (width <= 0 || height <= 0 || light.size() < texels || there.size() < texels) return std::nullopt;
    Texture texture;
    texture.width = width, texture.height = height, texture.across = Edge::stops, texture.down = Edge::stops;
    texture.sizes.push_back(0);
    texture.light.reserve(texels * 4 / 3 + 2), texture.cover.reserve(texels * 4 / 3 + 2);
    for (std::size_t texel = 0; texel < texels; ++texel) {
        const float held = std::clamp(there[texel], 0.0f, 1.0f);
        texture.light.push_back(light[texel] * held), texture.cover.push_back(held);
    }
    sized(texture, true);
    return texture;
}

namespace {

constexpr float between(float from, float to, float share) noexcept { return from + (to - from) * share; }

// A picture's light at `uv` for a pixel covering `footprint` texels across — between four texels of a size,
// between two sizes — and with it, where the picture has one, its cover there by the same shares. With a cover
// the light is each texel's times its cover (`Texture::cover`): what comes back is that sum, not yet divided.
Texture::Seen held(const Texture& texture, Vec2 uv, float footprint) noexcept {
    if (texture.sizes.empty()) return {{1, 1, 1}, 1};
    // The size whose texels are a pixel across or just under, and how far toward the next: a footprint of 1 or
    // less is the finest, of 2 the next, of 3 halfway between that and the one of 4.
    std::size_t size = 0;
    float covers = footprint > 1 ? footprint : 1.0f;
    while (covers >= 2 && size + 1 < texture.sizes.size()) covers *= 0.5f, ++size;
    const float toward_next = size + 1 < texture.sizes.size() ? covers - 1.0f : 0.0f;
    const bool covered = !texture.cover.empty();
    const auto within = [&](std::size_t which) {
        const int wide = std::max(texture.width >> which, 1), high = std::max(texture.height >> which, 1);
        // Between the four texels about the place; a texel's middle is its own colour. Past an edge: the picture
        // again, the picture turned about, or the edge's texels (`Edge`).
        struct Along {
            int   first = 0, second = 0;
            float share = 0;
        };
        const auto along = [](float coordinate, int count, Edge edge) {
            float within = coordinate - std::floor(coordinate);
            if (edge == Edge::mirrors) {
                const float twice = coordinate - 2.0f * std::floor(coordinate * 0.5f);  // 0 … 2
                within = twice > 1.0f ? 2.0f - twice : twice;
            } else if (edge == Edge::stops) {
                within = std::clamp(coordinate, 0.0f, 1.0f);
            }
            const float at = within * static_cast<float>(count) - 0.5f, floor_at = std::floor(at);
            const int   first = static_cast<int>(floor_at);
            if (edge == Edge::repeats) return Along{((first % count) + count) % count, (((first + 1) % count) + count) % count, at - floor_at};
            return Along{std::clamp(first, 0, count - 1), std::clamp(first + 1, 0, count - 1), at - floor_at};
        };
        const Along across = along(uv.x, wide, texture.across), down = along(uv.y, high, texture.down);
        const float share_x = across.share, share_y = down.share;
        const int   x0 = across.first, x1 = across.second, y0 = down.first, y1 = down.second;
        const auto place = [&](int tx, int ty) { return texture.sizes[which] + static_cast<std::size_t>(ty) * static_cast<std::size_t>(wide) + static_cast<std::size_t>(tx); };
        const auto of = [&](int tx, int ty) { return texture.light[place(tx, ty)]; };
        Texture::Seen found{math::mix(math::mix(of(x0, y0), of(x1, y0), share_x), math::mix(of(x0, y1), of(x1, y1), share_x), share_y), 1};
        if (covered) {
            const auto hides = [&](int tx, int ty) { return texture.cover[place(tx, ty)]; };
            found.cover = between(between(hides(x0, y0), hides(x1, y0), share_x), between(hides(x0, y1), hides(x1, y1), share_x), share_y);
        }
        return found;
    };
    const Texture::Seen here = within(size);
    if (!(toward_next > 0)) return here;
    const Texture::Seen next = within(size + 1);
    return {math::mix(here.light, next.light, toward_next), covered ? between(here.cover, next.cover, toward_next) : 1.0f};
}

// The light of what is there: the sum over its cover (a place wholly clear has no light of its own).
Texture::Seen parted(const Texture& texture, Texture::Seen sum) noexcept {
    if (texture.cover.empty()) return sum;
    return {sum.cover > 0 ? sum.light * (1.0f / sum.cover) : sum.light, sum.cover};
}

}  // namespace

Vec3 Texture::at(Vec2 uv, float footprint) const noexcept { return parted(*this, held(*this, uv, footprint)).light; }
Texture::Seen Texture::seen_at(Vec2 uv, float footprint) const noexcept { return parted(*this, held(*this, uv, footprint)); }

float Texture::cover_at(Vec2 uv) const noexcept { return cover.empty() ? 1.0f : held(*this, uv, 1).cover; }

Vec3 Texture::over(Vec2 uv, Vec2 right, Vec2 down) const noexcept { return seen(uv, right, down).light; }

Texture::Seen Texture::seen(Vec2 uv, Vec2 right, Vec2 down) const noexcept {
    const float wide = static_cast<float>(width), high = static_cast<float>(height);
    const float across = std::sqrt(right.x * right.x * wide * wide + right.y * right.y * high * high);
    const float along = std::sqrt(down.x * down.x * wide * wide + down.y * down.y * high * high);
    if (!(across == across) || !(along == along)) return parted(*this, held(*this, uv, 1));
    // The strip a pixel covers: its length the longer of the two ways, its width the shorter.
    const Vec2 lengthwise = across >= along ? right : down;
    const float longer = std::max(across, along), shorter = std::max(std::min(across, along), 1e-6f);
    const float places = std::clamp(std::ceil(longer / shorter), 1.0f, 8.0f);
    const float footprint = longer / places;
    if (places <= 1) return parted(*this, held(*this, uv, footprint));
    Seen sum{{0, 0, 0}, 0};
    for (float place = 0; place < places; place += 1) {
        const float aside = (place + 0.5f) / places - 0.5f;
        const Seen there = held(*this, {uv.x + lengthwise.x * aside, uv.y + lengthwise.y * aside}, footprint);
        sum.light = sum.light + there.light, sum.cover += there.cover;
    }
    return parted(*this, {sum.light * (1.0f / places), sum.cover * (1.0f / places)});
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
        if (tone.filmic || tone.neutral || tone.exposure != 1) shown = tone.of(shown);
        colour[pixel * 4] = encoded(shown.x), colour[pixel * 4 + 1] = encoded(shown.y), colour[pixel * 4 + 2] = encoded(shown.z), colour[pixel * 4 + 3] = 255;
    }
    return told;
}

}  // namespace xrender::mesh
