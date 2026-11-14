module xrender.mesh;

import std;

namespace xrender::mesh {

namespace math = mlibs::math;
namespace geom = mlibs::geom;
using math::Mat4;

namespace {

struct Thing {
    Mat4               back;      // the world to the mesh
    Mat4               normals;   // the mesh's normals to the world
    geom::Box3         bounds;    // in the world
    const geom::Mesh*  mesh = nullptr;
    const geom::Bvh*   tree = nullptr;
    Material           material;
    const Texture*     texture = nullptr;
    bool               casts = true;
    Mat4               world;     // the mesh to the world
    // The surface's other pictures: roughness and metal, which way it faces, what it gives off. Its shade picture
    // is not asked: how much of the light from about reaches a place is what the rays themselves find.
    const Texture*     surface = nullptr;
    const Texture*     normal_picture = nullptr;
    const Texture*     glow = nullptr;
};
struct Lamp {
    scene::LightKind kind = scene::LightKind::sun;
    Vec3  colour, at, toward;
    float range = 1, cone_cosine = 0;
};
struct Met {
    const Thing* thing = nullptr;
    float        distance = 0;
    Vec3         place, normal;
    Vec2         uv{0, 0};
    Vec3         face;  // the normal before the surface's picture of it turned it: the side a ray leaves by
};

struct Tracing {
    std::span<const Thing> things;
    std::span<const Lamp>  lamps;
    const Options&         options;
    Traced&                told;

    // What a ray meets first; `seen_by_eye`: things that do not cast count too.
    [[nodiscard]] std::optional<Met> meet(Vec3 from, Vec3 toward, float reach, bool seen_by_eye) const {
        ++told.rays;
        const geom::Ray ray{from, toward};
        std::optional<Met> best;
        std::uint32_t triangle = 0;
        for (const Thing& thing : things) {
            if (!thing.casts && !seen_by_eye) continue;
            const std::optional<float> box = geom::meet(ray, thing.bounds);
            const float farthest = best ? best->distance : reach;
            if (!box || *box > farthest) continue;
            // Taken into the mesh's own space as it is — not made of length 1 again — so a distance along it is the world's.
            const geom::Ray within{math::point(thing.back, from), math::direction(thing.back, toward)};
            if (const auto hit = xrender::mesh::meet(within, *thing.mesh, *thing.tree, &thing.material, thing.texture, farthest); hit && (!best || hit->distance < best->distance))
                best = Met{&thing, hit->distance, {}, {}, {}}, triangle = hit->item;
        }
        if (!best) return best;
        // Where on the triangle: each corner's share is the area across from it.
        const geom::Mesh& mesh = *best->thing->mesh;
        const std::array<std::uint32_t, 3> corner{mesh.indices[triangle * 3], mesh.indices[triangle * 3 + 1], mesh.indices[triangle * 3 + 2]};
        const Vec3 a = mesh.positions[corner[0]], b = mesh.positions[corner[1]], c = mesh.positions[corner[2]];
        const Vec3 within = math::point(best->thing->back, from) + math::direction(best->thing->back, toward) * best->distance;
        const Vec3 face = math::cross(b - a, c - a);
        const float whole = math::dot(face, face);
        std::array<float, 3> share{1, 0, 0};
        if (whole > 0) {
            share[1] = math::dot(math::cross(within - a, c - a), face) / whole, share[2] = math::dot(math::cross(b - a, within - a), face) / whole;
            share[0] = 1 - share[1] - share[2];
        }
        Vec3 normal = !mesh.normals.empty() ? mesh.normals[corner[0]] * share[0] + mesh.normals[corner[1]] * share[1] + mesh.normals[corner[2]] * share[2] : face;
        normal = math::normalized(math::direction(best->thing->normals, normal));
        if (math::dot(normal, toward) > 0) normal = normal * -1.0f;  // met from behind: the side that was met
        best->place = from + toward * best->distance, best->normal = normal, best->face = normal;
        const Placing& placing = best->thing->material.placing;
        if (!mesh.uvs.empty()) best->uv = placing.of({mesh.uvs[corner[0]].x * share[0] + mesh.uvs[corner[1]].x * share[1] + mesh.uvs[corner[2]].x * share[2],
                                                      mesh.uvs[corner[0]].y * share[0] + mesh.uvs[corner[1]].y * share[1] + mesh.uvs[corner[2]].y * share[2]});
        // Which way the surface faces by its picture (as the pass reads it): about the mesh's tangent, or the way
        // the surface goes as the picture's first coordinate grows over this triangle.
        if (const Texture* picture = best->thing->normal_picture; picture && !mesh.uvs.empty()) {
            Vec3 right_way{0, 0, 0}, up_way{0, 0, 0};
            if (!mesh.tangents.empty()) {
                const math::Vec4 one = mesh.tangents[corner[0]], two = mesh.tangents[corner[1]], three = mesh.tangents[corner[2]];
                right_way = math::direction(best->thing->world, Vec3{one.x * share[0] + two.x * share[1] + three.x * share[2], one.y * share[0] + two.y * share[1] + three.y * share[2],
                                                                     one.z * share[0] + two.z * share[1] + three.z * share[2]});
                up_way = math::cross(normal, right_way) * one.w;
            } else {
                const Vec3 first = math::direction(best->thing->world, b - a), second = math::direction(best->thing->world, c - a);
                const Vec2 uv_first = placing.of(mesh.uvs[corner[0]]), uv_second = placing.of(mesh.uvs[corner[1]]), uv_third = placing.of(mesh.uvs[corner[2]]);
                const Vec2 first_uv{uv_second.x - uv_first.x, uv_second.y - uv_first.y}, second_uv{uv_third.x - uv_first.x, uv_third.y - uv_first.y};
                const float turn = first_uv.x * second_uv.y - second_uv.x * first_uv.y;
                if (turn != 0) right_way = (first * second_uv.y - second * first_uv.y) * (1.0f / turn), up_way = (second * first_uv.x - first * second_uv.x) * (1.0f / turn);
            }
            right_way = right_way - normal * math::dot(normal, right_way);
            if (math::dot(right_way, right_way) > 0) {
                right_way = math::normalized(right_way);
                const Vec3 other = math::cross(normal, right_way);
                up_way = math::dot(other, up_way) < 0 ? other * -1.0f : other;
                const Vec3 said = picture->at(best->uv, 1);
                const float scale = best->thing->material.normal_scale;
                const Vec3 turned = right_way * ((said.x * 2 - 1) * scale) + up_way * ((said.y * 2 - 1) * scale) + normal * (said.z * 2 - 1);
                // Turned past the eye's ray it would be a side that cannot be seen: the surface's own is kept.
                if (math::dot(turned, turned) > 0 && math::dot(turned, toward) < 0) best->normal = math::normalized(turned);
            }
        }
        return best;
    }
    [[nodiscard]] bool hidden(Vec3 from, Vec3 toward, float reach) const {
        ++told.rays;
        const geom::Ray ray{from, toward};
        for (const Thing& thing : things) {
            if (!thing.casts || thing.material.see_through) continue;  // what is seen through hides nothing from a light
            const std::optional<float> box = geom::meet(ray, thing.bounds);
            if (!box || *box > reach) continue;
            const geom::Ray within{math::point(thing.back, from), math::direction(thing.back, toward)};
            if (xrender::mesh::meet(within, *thing.mesh, *thing.tree, &thing.material, thing.texture, reach)) return true;
        }
        return false;
    }
    [[nodiscard]] Vec3 sky(Vec3 toward) const { return options.environment.on ? options.environment.toward(toward) : options.background; }

    // The light a ray from `from` along `toward` brings back.
    // The surroundings as a picture, or none: with one, a surface also asks the picture for a way — chosen as
    // often as there is light along it — besides the way its own surface throws, and each is counted by how
    // likely it was against the other (Veach's weights, a lobe at a time). A small bright sun is then found from
    // every surface, not waited for.
    [[nodiscard]] const Surroundings* picture() const noexcept { return options.environment.on ? options.environment.picture : nullptr; }

    // The light a ray from `from` along `toward` brings back. `likelihood`: how likely the surface it left was to
    // throw it this way (a way's share of all ways), or 0 where that is not weighed (from the eye).
    [[nodiscard]] Vec3 along(Vec3 from, Vec3 toward, std::uint32_t bounces_left, math::SplitMix64& random, bool from_eye, float likelihood = 0) const {
        const std::optional<Met> met = meet(from, toward, std::numeric_limits<float>::max(), from_eye);
        if (!met) {
            ++told.into_open;
            if (likelihood > 0 && picture()) return sky(toward) * (likelihood / (likelihood + picture()->likelihood(toward)));
            return sky(toward);
        }
        Material material = met->thing->material;
        // A surface seen through: as often as it does not hide what is behind it, the ray goes on through it.
        if (material.see_through) {
            const float hides = material.opacity * (met->thing->texture ? met->thing->texture->cover_at(met->uv) : 1.0f);
            if (!(static_cast<float>(math::unit24(random.next())) < hides)) return along(met->place + toward * 2e-3f, toward, bounces_left, random, from_eye, likelihood);
        }
        // Its picture at the place (a pixel a texel: the finest size).
        if (met->thing->texture) material.base = math::scaled(material.base, met->thing->texture->at(met->uv, 1));
        // Its other pictures there: how rough and how much a metal, and what it gives off.
        if (met->thing->surface) {
            const Vec3 said = met->thing->surface->at(met->uv, 1);
            material.roughness = material.roughness * said.y, material.metallic = material.metallic * said.z;
        }
        if (met->thing->glow) material.emissive = math::scaled(material.emissive, met->thing->glow->at(met->uv, 1));
        // A ray leaves by the surface's own side, whatever way its picture turned the normal; a way the turned
        // normal throws under the surface brings nothing.
        if (material.unlit) return material.base + material.emissive;  // no light changes it, and the ray goes no further
        const Vec3 lifted = met->place + met->face * 2e-3f, toward_eye = toward * -1.0f;
        Vec3 arriving{0, 0, 0}, sent{0, 0, 0};  // on a matt surface the lights are summed; else each by `reflected`
        const bool matt = material.matt();
        for (const Lamp& lamp : lamps) {
            Vec3 way = lamp.toward * -1.0f, colour = lamp.colour;
            float reach = std::numeric_limits<float>::max();
            if (lamp.kind != scene::LightKind::sun) {
                const Vec3 to = lamp.at - met->place;
                const float apart = math::length(to);
                if (!(apart < lamp.range) || !(apart > 0)) continue;
                way = to * (1.0f / apart), reach = apart;
                if (lamp.kind == scene::LightKind::spot && -math::dot(way, lamp.toward) < lamp.cone_cosine) continue;
                const float near = 1.0f - (apart / lamp.range) * (apart / lamp.range);
                colour = colour * (near * near);
            }
            const float facing = math::dot(met->normal, way);
            if (!(facing > 0) || !(math::dot(met->face, way) > 0) || hidden(lifted, way, reach)) continue;
            if (matt) arriving = arriving + colour * facing;
            else sent = sent + math::scaled(reflected(material, met->normal, toward_eye, way, facing), colour);
        }
        // From one more way. A matt surface: about its normal, likelier the nearer it (so each way counts the same).
        // One with a highlight: either that, or — as often as it is a mirror — the way the eye's ray is thrown back
        // by one of its small faces, chosen as the highlight spreads them (GGX), weighed by how much is mirrored
        // there (Schlick) and how much the faces hide of one another (Smith; the measure for light from all about).
        const float rough = std::clamp(material.roughness, 0.04f, 1.0f), metal = std::clamp(material.metallic, 0.0f, 1.0f);
        const Vec3 colour = matt ? material.base : material.base * (1.0f - metal);
        const Vec3 aside = math::normalized(math::cross(std::abs(met->normal.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, met->normal)), other = math::cross(met->normal, aside);
        const auto about_normal = [&](float out, float turn, float up) {
            return math::normalized(aside * (out * static_cast<float>(math::det::cos(static_cast<double>(turn)))) + other * (out * static_cast<float>(math::det::sin(static_cast<double>(turn)))) + met->normal * up);
        };
        Vec3 around{0, 0, 0}, thrown{0, 0, 0};  // what arrives for the surface's own colour; what it mirrors
        if (bounces_left > 0 && picture()) {
            const float first = static_cast<float>(math::unit24(random.next())), second = static_cast<float>(math::unit24(random.next()));
            const Surroundings::Chosen asked = picture()->chosen(first, second);
            const float facing = math::dot(met->normal, asked.way);
            if (asked.likelihood > 0 && facing > 0 && math::dot(met->face, asked.way) > 0 && !hidden(lifted, asked.way, std::numeric_limits<float>::max())) {
                constexpr float pi = 3.14159265f;
                const Vec3 light = picture()->toward(asked.way);
                // For the surface's own colour: facing ÷ π of the ways is how likely the surface was to ask this one.
                sent = sent + math::scaled(colour, light) * (facing / (pi * asked.likelihood + facing));
                const float seen = math::dot(met->normal, toward_eye);
                if (!matt && seen > 0) {
                    const Vec3 half = math::normalized(toward_eye + asked.way);
                    const float up = math::dot(met->normal, half), eye_half = math::dot(toward_eye, half);
                    if (up > 0 && eye_half > 0) {
                        const float spread = rough * rough * rough * rough, under = up * up * (spread - 1.0f) + 1.0f;
                        const float faces = spread / (pi * under * under);  // how many small faces look along `half`
                        const float k = rough * rough * 0.5f;
                        const float unhidden = 1.0f / ((facing * (1.0f - k) + k) * (seen * (1.0f - k) + k));
                        const float glance = 1.0f - eye_half, glance5 = glance * glance * glance * glance * glance;
                        const Vec3 head_on = Vec3{0.04f, 0.04f, 0.04f} * (1.0f - metal) + material.base * metal;
                        const float thrown_so = faces * up / (4.0f * eye_half);  // how likely the highlight was to throw the eye's way there
                        sent = sent + math::scaled(math::scaled(head_on + (Vec3{1, 1, 1} - head_on) * glance5, kept(material, seen)), light) * (faces * unhidden * facing * 0.25f / (asked.likelihood + thrown_so));
                    }
                }
            }
        }
        if (bounces_left > 0) {
            const float mirrors = matt ? 0.0f : metal + (1.0f - metal) * 0.5f;  // how often the mirrored way is taken
            const bool mirror = !matt && static_cast<float>(math::unit24(random.next())) < mirrors;
            const float first = static_cast<float>(math::unit24(random.next())), second = static_cast<float>(math::unit24(random.next()));
            if (!mirror) {
                const Vec3 way = about_normal(std::sqrt(first), second * 6.2831853f, std::sqrt(std::max(1.0f - first, 0.0f)));
                if (math::dot(met->face, way) > 0) around = along(lifted, way, bounces_left - 1, random, false, std::sqrt(std::max(1.0f - first, 0.0f)) / 3.14159265f) * (1.0f / (1.0f - mirrors));
            } else {
                const float spread = rough * rough * rough * rough;
                const float up = std::sqrt((1.0f - first) / (1.0f + (spread - 1.0f) * first));
                const Vec3 half = about_normal(std::sqrt(std::max(1.0f - up * up, 0.0f)), second * 6.2831853f, up);
                const float eye_half = math::dot(toward_eye, half), seen = math::dot(met->normal, toward_eye);
                const Vec3 way = half * (2 * eye_half) - toward_eye;
                const float facing = math::dot(met->normal, way);
                if (eye_half > 0 && seen > 0 && facing > 0 && up > 0 && math::dot(met->face, way) > 0) {
                    const float k = rough * rough * 0.5f;
                    const float unhidden = 1.0f / ((facing * (1.0f - k) + k) * (seen * (1.0f - k) + k));
                    const float glance = 1.0f - eye_half, glance5 = glance * glance * glance * glance * glance;
                    const Vec3 head_on = Vec3{0.04f, 0.04f, 0.04f} * (1.0f - metal) + material.base * metal;
                    const Vec3 share = math::scaled(head_on + (Vec3{1, 1, 1} - head_on) * glance5, kept(material, seen)) * (unhidden * facing * eye_half / up / mirrors);
                    const float under = up * up * (spread - 1.0f) + 1.0f;
                    thrown = math::scaled(share, along(lifted, way, bounces_left - 1, random, false, spread / (3.14159265f * under * under) * up / (4.0f * eye_half)));
                }
            }
        } else {
            around = options.environment.on ? options.environment.falling(met->normal) : options.background;
            if (!matt) thrown = mirrored(material, options.environment.on ? options.environment : Environment{true, options.background, options.background, options.background}, met->normal, toward_eye);
        }
        return material.emissive + math::scaled(colour, arriving + around) + sent + thrown;
    }
};

}  // namespace

Traced traced(const scene::Scene& scene, const scene::View& view, const Assets& assets, Targets& out, const Options& options, const Trace& how) {
    Traced told;
    const std::size_t pixels = static_cast<std::size_t>(std::max(out.width, 0)) * static_cast<std::size_t>(std::max(out.height, 0));
    out.light.assign(pixels, options.background);
    const Mat4 clip = view.projection * view.view;
    const std::optional<Mat4> from_clip = math::inverse(clip), camera = math::inverse(view.view);
    if (!pixels || !from_clip || !camera || how.samples == 0) {
        Glow shown;
        (void)shown.show(out.light, out.width, out.height, {}, out.colour);
        return told;
    }
    std::vector<Thing> things;
    for (const scene::Instance& instance : scene.instances()) {
        if (instance.hidden || (instance.layers & view.layers) == 0 || instance.mesh >= assets.meshes.size() || instance.mesh >= assets.trees.size() || !assets.meshes[instance.mesh] ||
            !assets.trees[instance.mesh] || instance.material >= assets.materials.size())
            continue;
        const std::optional<Mat4> back = math::inverse(instance.world);
        if (!back) continue;
        const geom::Mesh& mesh = assets.meshes[instance.mesh]->mesh;
        const Material& material = assets.materials[instance.material];
        things.push_back({*back, math::transposed(*back), geom::moved(instance.world, instance.bounds), &mesh, assets.trees[instance.mesh], material,
                          (material.texture < assets.textures.size() && !mesh.uvs.empty()) ? assets.textures[material.texture] : nullptr, instance.casts, instance.world,
                          (material.surface < assets.textures.size() && !mesh.uvs.empty()) ? assets.textures[material.surface] : nullptr,
                          (material.normal < assets.textures.size() && !mesh.uvs.empty()) ? assets.textures[material.normal] : nullptr,
                          (material.glow < assets.textures.size() && !mesh.uvs.empty()) ? assets.textures[material.glow] : nullptr});
    }
    std::vector<Lamp> lamps;
    for (const scene::Light& light : scene.lights())
        if (!light.hidden && (light.layers & view.layers) != 0)
            lamps.push_back({light.kind, light.colour * light.intensity, light.at, math::normalized(light.toward), std::max(light.range, 1e-6f), static_cast<float>(math::det::cos(static_cast<double>(light.cone)))});
    const Tracing tracing{things, lamps, options, told};
    const Vec3 eye = math::point(*camera, {0, 0, 0});
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const std::size_t pixel = static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width) + static_cast<std::size_t>(x);
            Vec3 sum{0, 0, 0};
            for (std::uint32_t sample = 0; sample < how.samples; ++sample) {
                // A pixel's rays from its number, the sample's and the seed: the same whatever was drawn before.
                math::SplitMix64 random{math::mix64(math::mix64(how.seed ^ (static_cast<std::uint64_t>(pixel) << 20)) + sample)};
                const float at_x = static_cast<float>(x) + static_cast<float>(math::unit24(random.next())), at_y = static_cast<float>(y) + static_cast<float>(math::unit24(random.next()));
                const Vec3 through = math::point(*from_clip, {at_x / static_cast<float>(out.width) * 2 - 1, 1 - at_y / static_cast<float>(out.height) * 2, options.reversed_depth ? 1.0f : 0.0f});
                sum = sum + tracing.along(eye, math::normalized(through - eye), how.bounces, random, true);
            }
            out.light[pixel] = sum * (1.0f / static_cast<float>(how.samples));
        }
    Glow shown;
    (void)shown.show(out.light, out.width, out.height, {}, out.colour);
    return told;
}

}  // namespace xrender::mesh
