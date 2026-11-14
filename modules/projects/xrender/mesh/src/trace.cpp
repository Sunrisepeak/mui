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
    const Texture*     clearness = nullptr;  // how much goes through, how thick it is
    const Texture*     thick = nullptr;
};
struct Lamp {
    scene::LightKind kind = scene::LightKind::sun;
    Vec3  colour, at, toward;
    float range = 1, cone_cosine = 0;
    bool  physical = false;
    float inner_cosine = 2;
};
struct Met {
    const Thing* thing = nullptr;
    float        distance = 0;
    Vec3         place, normal;
    math::Vec4   uv{0, 0, 0, 0};  // the mesh's two sets of texture coordinates there
    Vec3         face;  // the normal before the surface's picture of it turned it: the side a ray leaves by
    math::Vec4   colour{1, 1, 1, 1};  // the mesh's own there (`Mesh::colours`), or white
    Vec3         coat_normal{0, 0, 1};  // the clear coat's: the surface's own before its picture turned it
    Vec3         drawn_out{0, 0, 0};    // the way the highlight is drawn out along (`Material::anisotropy`), or none
    // A picture's place there, as `placing` lays it.
    [[nodiscard]] Vec2 at(const Placing& placing) const noexcept { return placing.of(placing.set ? Vec2{uv.z, uv.w} : Vec2{uv.x, uv.y}); }
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
                best = Met{&thing, hit->distance, {}, {}, {}, {}}, triangle = hit->item;
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
        best->place = from + toward * best->distance, best->normal = normal, best->face = normal, best->coat_normal = normal;
        const auto mixed = [&](const auto& values, auto part_of) { return values[corner[0]].*part_of * share[0] + values[corner[1]].*part_of * share[1] + values[corner[2]].*part_of * share[2]; };
        if (!mesh.uvs.empty()) best->uv.x = mixed(mesh.uvs, &Vec2::x), best->uv.y = mixed(mesh.uvs, &Vec2::y);
        if (!mesh.second_uvs.empty()) best->uv.z = mixed(mesh.second_uvs, &Vec2::x), best->uv.w = mixed(mesh.second_uvs, &Vec2::y);
        if (!mesh.colours.empty())
            best->colour = {mixed(mesh.colours, &math::Vec4::x), mixed(mesh.colours, &math::Vec4::y), mixed(mesh.colours, &math::Vec4::z), mixed(mesh.colours, &math::Vec4::w)};
        // The way across the surface and the way up it, in the world: the mesh's tangent, or the way the picture's
        // first and second coordinates grow over this triangle (by `placing`); none where neither is known.
        const auto frame = [&](const Placing& placing) {
            const std::vector<Vec2>& uvs = placing.set ? mesh.second_uvs : mesh.uvs;
            Vec3 right_way{0, 0, 0}, up_way{0, 0, 0};
            if (!mesh.tangents.empty()) {
                const math::Vec4 one = mesh.tangents[corner[0]], two = mesh.tangents[corner[1]], three = mesh.tangents[corner[2]];
                right_way = math::direction(best->thing->world, Vec3{one.x * share[0] + two.x * share[1] + three.x * share[2], one.y * share[0] + two.y * share[1] + three.y * share[2],
                                                                     one.z * share[0] + two.z * share[1] + three.z * share[2]});
                up_way = math::cross(normal, right_way) * one.w;
            } else if (!uvs.empty()) {
                const Vec3 first = math::direction(best->thing->world, b - a), second = math::direction(best->thing->world, c - a);
                const Vec2 uv_first = uvs[corner[0]], uv_second = uvs[corner[1]], uv_third = uvs[corner[2]];
                const Vec2 first_uv = placing.moved({uv_second.x - uv_first.x, uv_second.y - uv_first.y}), second_uv = placing.moved({uv_third.x - uv_first.x, uv_third.y - uv_first.y});
                const float turn = first_uv.x * second_uv.y - second_uv.x * first_uv.y;
                if (turn != 0) right_way = (first * second_uv.y - second * first_uv.y) * (1.0f / turn), up_way = (second * first_uv.x - first * second_uv.x) * (1.0f / turn);
            }
            return std::pair{right_way, up_way};
        };
        // Which way the surface faces by its picture (as the pass reads it): about the mesh's tangent, or the way
        // the surface goes as the picture's first coordinate grows over this triangle.
        if (const Texture* picture = best->thing->normal_picture) {
            const Placing& placing = best->thing->material.placing.normal;
            auto [right_way, up_way] = frame(placing);
            right_way = right_way - normal * math::dot(normal, right_way);
            if (math::dot(right_way, right_way) > 0) {
                right_way = math::normalized(right_way);
                const Vec3 other = math::cross(normal, right_way);
                up_way = math::dot(other, up_way) < 0 ? other * -1.0f : other;
                const Vec3 said = picture->at(best->at(placing), 1);
                const float scale = best->thing->material.normal_scale;
                const Vec3 turned = right_way * ((said.x * 2 - 1) * scale) + up_way * ((said.y * 2 - 1) * scale) + normal * (said.z * 2 - 1);
                // Turned past the eye's ray it would be a side that cannot be seen: the surface's own is kept.
                if (math::dot(turned, turned) > 0 && math::dot(turned, toward) < 0) best->normal = math::normalized(turned);
            }
        }
        // The way the highlight is drawn out along: the tangent turned by the material's rotation, in the surface
        // as its picture turned it (the pass's way; the anisotropy picture is the pass's only).
        if (best->thing->material.anisotropy > 0) {
            auto [right_way, up_way] = frame(best->thing->material.placing.anisotropy);
            const Vec3 facing = best->normal;
            right_way = right_way - facing * math::dot(facing, right_way);
            if (math::dot(right_way, right_way) > 0) {
                right_way = math::normalized(right_way);
                const Vec3 other = math::cross(facing, right_way);
                up_way = math::dot(other, up_way) < 0 ? other * -1.0f : other;
                const double turn = static_cast<double>(best->thing->material.anisotropy_rotation);
                const Vec3 along_way = right_way * static_cast<float>(math::det::cos(turn)) + up_way * static_cast<float>(math::det::sin(turn));
                if (math::dot(along_way, along_way) > 0) best->drawn_out = math::normalized(along_way);
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

    // The way a ray along `toward` goes on through a boundary whose normal `normal` faces it, from an index of
    // refraction to one `bends` times as low (Snell); none where all of it is thrown back.
    [[nodiscard]] static std::optional<Vec3> bent(Vec3 toward, Vec3 normal, float bends) noexcept {
        const float facing = -math::dot(normal, toward), under = 1.0f - bends * bends * (1.0f - facing * facing);
        if (under < 0) return std::nullopt;
        return math::normalized(toward * bends + normal * (bends * facing - std::sqrt(under)));
    }
    // What a way gone `length` through `material`'s inside lets through (Beer's law).
    [[nodiscard]] static Vec3 let_through(const Material& material, float length) noexcept {
        if (!(material.attenuation_distance > 0) || !(material.attenuation_distance < std::numeric_limits<float>::infinity()) || !(length > 0)) return {1, 1, 1};
        const double times = static_cast<double>(length) / static_cast<double>(material.attenuation_distance);
        const auto of = [&](float colour) { return colour >= 1 ? 1.0f : colour <= 0 ? 0.0f : static_cast<float>(math::det::pow(static_cast<double>(colour), times)); };
        return {of(material.attenuation.x), of(material.attenuation.y), of(material.attenuation.z)};
    }

    // The light a ray from `from` along `toward` brings back. `likelihood`: how likely the surface it left was to
    // throw it this way (a way's share of all ways), or 0 where that is not weighed (from the eye). `inside`: the
    // thing whose inside the ray goes through (a solid that lets light through, `Material::thickness` above 0) —
    // where it meets that thing again it leaves it, bent, and what it brings is what the way gone lets through.
    [[nodiscard]] Vec3 along(Vec3 from, Vec3 toward, std::uint32_t bounces_left, math::SplitMix64& random, bool from_eye, float likelihood = 0, const Thing* inside = nullptr) const {
        const std::optional<Met> met = meet(from, toward, std::numeric_limits<float>::max(), from_eye);
        if (!met) {
            ++told.into_open;
            if (likelihood > 0 && picture()) return sky(toward) * (likelihood / (likelihood + picture()->likelihood(toward)));
            return sky(toward);
        }
        if (inside) {
            const Vec3 through = let_through(inside->material, met->distance);
            if (met->thing != inside) return math::scaled(along(from, toward, bounces_left, random, from_eye, likelihood), through);  // something within it
            // Out of it: bent the other way (the normal faces the ray, so inward); all thrown back, on inside.
            const std::optional<Vec3> out = bent(toward, met->face, std::max(inside->material.ior, 1e-3f));
            if (!out) {
                if (bounces_left == 0) return {0, 0, 0};
                const Vec3 back = toward - met->face * (2 * math::dot(met->face, toward));
                return math::scaled(along(met->place + met->face * 2e-3f, back, bounces_left - 1, random, from_eye, 0, inside), through);
            }
            if (bounces_left == 0) return math::scaled(sky(*out), through);
            return math::scaled(along(met->place - met->face * 2e-3f, *out, bounces_left - 1, random, from_eye), through);
        }
        Material material = met->thing->material;
        // A surface seen through: as often as it does not hide what is behind it, the ray goes on through it.
        if (material.see_through) {
            const float hides = material.opacity * met->colour.w * (met->thing->texture ? met->thing->texture->cover_at(met->at(material.placing.base)) : 1.0f);
            if (!(static_cast<float>(math::unit24(random.next())) < hides)) return along(met->place + toward * 2e-3f, toward, bounces_left, random, from_eye, likelihood);
        }
        // Its picture at the place (a pixel a texel: the finest size).
        material.base = math::scaled(material.base, Vec3{met->colour.x, met->colour.y, met->colour.z});
        if (met->thing->texture) material.base = math::scaled(material.base, met->thing->texture->at(met->at(material.placing.base), 1));
        // Its other pictures there: how rough and how much a metal, and what it gives off.
        if (met->thing->surface) {
            const Vec3 said = met->thing->surface->at(met->at(material.placing.surface), 1);
            material.roughness = material.roughness * said.y, material.metallic = material.metallic * said.z;
        }
        if (met->thing->clearness) material.transmission = material.transmission * met->thing->clearness->at(met->at(material.placing.clearness), 1).x;
        if (met->thing->thick) material.thickness = material.thickness * met->thing->thick->at(met->at(material.placing.thick), 1).y;
        if (met->thing->glow) material.emissive = math::scaled(material.emissive, met->thing->glow->at(met->at(material.placing.glow), 1));
        // A ray leaves by the surface's own side, whatever way its picture turned the normal; a way the turned
        // normal throws under the surface brings nothing.
        if (material.unlit) return material.base + material.emissive;  // no light changes it, and the ray goes no further
        const Vec3 lifted = met->place + met->face * 2e-3f, toward_eye = toward * -1.0f;
        Vec3 arriving{0, 0, 0}, sent{0, 0, 0};  // on a matt surface the lights are summed; else each by `reflected`
        const bool matt = material.matt();
        // A highlight drawn out along the surface (`Material::anisotropy`); a sheen (its fibres' own light, what
        // comes from under them less by what they take of an even light); a clear coat (a layer of its own over
        // the surface, by its own normal and roughness, mirroring 4% head on) — each as the pass has it, its light
        // gathered along rays.
        const bool drawn = material.anisotropy > 0 && math::dot(met->drawn_out, met->drawn_out) > 0;
        const bool sheened = material.sheen.x > 0 || material.sheen.y > 0 || material.sheen.z > 0;
        const float coated = std::clamp(material.clearcoat, 0.0f, 1.0f);
        Material layer;
        layer.base = {0, 0, 0}, layer.roughness = material.clearcoat_roughness, layer.metallic = 0;
        Vec3 sheen{0, 0, 0}, coat{0, 0, 0};
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
                colour = colour * (lamp.physical ? physical_reach(lamp.range, apart) * (lamp.kind == scene::LightKind::spot ? soft_edge(lamp.cone_cosine, lamp.inner_cosine, -math::dot(way, lamp.toward)) : 1.0f)
                                                 : near * near);
            }
            const float facing = math::dot(met->normal, way), coat_facing = coated > 0 ? math::dot(met->coat_normal, way) : 0.0f;
            if ((!(facing > 0) && !(coat_facing > 0)) || !(math::dot(met->face, way) > 0) || hidden(lifted, way, reach)) continue;
            if (facing > 0) {
                if (matt) arriving = arriving + colour * facing;
                else sent = sent + math::scaled(drawn ? reflected_drawn_out(material, met->normal, met->drawn_out, toward_eye, way, facing) : reflected(material, met->normal, toward_eye, way, facing), colour);
                if (sheened) sheen = sheen + math::scaled(material.sheen, colour) * sheen_reflected(material.sheen_roughness, met->normal, toward_eye, way, facing);
            }
            if (coat_facing > 0) coat = coat + math::scaled(reflected(layer, met->coat_normal, toward_eye, way, coat_facing), colour);
        }
        // From one more way. A matt surface: about its normal, likelier the nearer it (so each way counts the same).
        // One with a highlight: either that, or — as often as it is a mirror — the way the eye's ray is thrown back
        // by one of its small faces, chosen as the highlight spreads them (GGX), weighed by how much is mirrored
        // there (Schlick) and how much the faces hide of one another (Smith; the measure for light from all about).
        const float rough = std::clamp(material.roughness, 0.04f, 1.0f), metal = std::clamp(material.metallic, 0.0f, 1.0f);
        // What goes on through the surface is not scattered back (`Material::transmission`).
        const float lets = std::clamp(material.transmission, 0.0f, 1.0f);
        const Vec3 colour = (matt ? material.base : material.base * (1.0f - metal)) * (1.0f - lets);
        const Vec3 aside = math::normalized(math::cross(std::abs(met->normal.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, met->normal)), other = math::cross(met->normal, aside);
        const auto about_normal = [&](float out, float turn, float up) {
            return math::normalized(aside * (out * static_cast<float>(math::det::cos(static_cast<double>(turn)))) + other * (out * static_cast<float>(math::det::sin(static_cast<double>(turn)))) + met->normal * up);
        };
        Vec3 around{0, 0, 0}, thrown{0, 0, 0};  // what arrives for the surface's own colour; what it mirrors
        // A highlight's lobe along rays by GGX (Smith's measure for light from all about, as the pass's picture of
        // it): how much of light from `way` is thrown to the eye about `normal`, and how likely the lobe was to
        // throw it so (its share of all ways).
        struct Lobe {
            Vec3  share;
            float likelihood = 0;
        };
        const auto lobe = [&](const Material& of, Vec3 normal, Vec3 way) -> Lobe {
            constexpr float pi = 3.14159265f;
            const float lobe_rough = std::clamp(of.roughness, 0.04f, 1.0f), seen = math::dot(normal, toward_eye), facing = math::dot(normal, way);
            const Vec3 half = math::normalized(toward_eye + way);
            const float up = math::dot(normal, half), eye_half = math::dot(toward_eye, half);
            if (!(seen > 0) || !(facing > 0) || !(up > 0) || !(eye_half > 0)) return {};
            const float spread = lobe_rough * lobe_rough * lobe_rough * lobe_rough, under = up * up * (spread - 1.0f) + 1.0f;
            const float faces = spread / (pi * under * under);  // how many small faces look along `half`
            const float k = lobe_rough * lobe_rough * 0.5f;
            const float unhidden = 1.0f / ((facing * (1.0f - k) + k) * (seen * (1.0f - k) + k));
            const float glance = 1.0f - eye_half, glance5 = glance * glance * glance * glance * glance;
            return {math::scaled(mirrors_at(of, glance5, seen), kept(of, seen)) * (faces * unhidden * facing * 0.25f), faces * up / (4.0f * eye_half)};
        };
        // A highlight drawn out: the pass's own measure (with π out, as along rays), the lobe's likelihood the
        // round one's it is chosen by.
        const auto drawn_lobe = [&](Vec3 way) -> Lobe {
            const float facing = math::dot(met->normal, way);
            if (!(facing > 0)) return {};
            const Lobe round = lobe(material, met->normal, way);
            // The highlight alone: the pass's whole less its matt part (a metal's colour is what it mirrors).
            const Vec3 matt_part = material.base * ((1.0f - std::clamp(material.metallic, 0.0f, 1.0f)) * (1.0f - std::clamp(material.transmission, 0.0f, 1.0f)) * facing);
            return {(reflected_drawn_out(material, met->normal, met->drawn_out, toward_eye, way, facing) - matt_part) * (1.0f / 3.14159265f), round.likelihood};
        };
        if (bounces_left > 0 && picture()) {
            const float first = static_cast<float>(math::unit24(random.next())), second = static_cast<float>(math::unit24(random.next()));
            const Surroundings::Chosen asked = picture()->chosen(first, second);
            const float facing = math::dot(met->normal, asked.way), coat_facing = coated > 0 ? math::dot(met->coat_normal, asked.way) : 0.0f;
            if (asked.likelihood > 0 && (facing > 0 || coat_facing > 0) && math::dot(met->face, asked.way) > 0 && !hidden(lifted, asked.way, std::numeric_limits<float>::max())) {
                constexpr float pi = 3.14159265f;
                const Vec3 light = picture()->toward(asked.way);
                if (facing > 0) {
                    // For the surface's own colour: facing ÷ π of the ways is how likely the surface was to ask this one.
                    sent = sent + math::scaled(colour, light) * (facing / (pi * asked.likelihood + facing));
                    if (sheened) sheen = sheen + math::scaled(material.sheen, light) * (sheen_reflected(material.sheen_roughness, met->normal, toward_eye, asked.way, facing) / (pi * asked.likelihood + facing));
                    if (!matt) {
                        const Lobe thrown_so = drawn ? drawn_lobe(asked.way) : lobe(material, met->normal, asked.way);
                        if (thrown_so.likelihood > 0) sent = sent + math::scaled(thrown_so.share, light) * (1.0f / (asked.likelihood + thrown_so.likelihood));
                    }
                }
                if (coat_facing > 0) {
                    const Lobe on_coat = lobe(layer, met->coat_normal, asked.way);
                    if (on_coat.likelihood > 0) coat = coat + math::scaled(on_coat.share, light) * (1.0f / (asked.likelihood + on_coat.likelihood));
                }
            }
        }
        if (bounces_left > 0) {
            const float mirrors = matt ? 0.0f : metal + (1.0f - metal) * 0.5f;  // how often the mirrored way is taken
            const bool mirror = !matt && static_cast<float>(math::unit24(random.next())) < mirrors;
            const float first = static_cast<float>(math::unit24(random.next())), second = static_cast<float>(math::unit24(random.next()));
            if (!mirror) {
                const float up = std::sqrt(std::max(1.0f - first, 0.0f));
                const Vec3 way = about_normal(std::sqrt(first), second * 6.2831853f, up);
                if (math::dot(met->face, way) > 0) {
                    const Vec3 light = along(lifted, way, bounces_left - 1, random, false, up / 3.14159265f) * (1.0f / (1.0f - mirrors));
                    around = light;
                    // The sheen's share of it: its measure over how likely the way was (facing ÷ π, π out).
                    if (sheened && up > 0) sheen = sheen + math::scaled(material.sheen, light) * (sheen_reflected(material.sheen_roughness, met->normal, toward_eye, way, up) / up);
                }
            } else {
                const float spread = rough * rough * rough * rough;
                const float up = std::sqrt((1.0f - first) / (1.0f + (spread - 1.0f) * first));
                const Vec3 half = about_normal(std::sqrt(std::max(1.0f - up * up, 0.0f)), second * 6.2831853f, up);
                const float eye_half = math::dot(toward_eye, half), seen = math::dot(met->normal, toward_eye);
                const Vec3 way = half * (2 * eye_half) - toward_eye;
                const float facing = math::dot(met->normal, way);
                if (eye_half > 0 && seen > 0 && facing > 0 && up > 0 && math::dot(met->face, way) > 0) {
                    const float under = up * up * (spread - 1.0f) + 1.0f, chosen = spread / (3.14159265f * under * under) * up / (4.0f * eye_half);
                    Vec3 share{0, 0, 0};
                    if (drawn) {
                        // Drawn out: its own measure over how likely the round lobe was to choose this way.
                        share = drawn_lobe(way).share * (1.0f / (chosen * mirrors));
                    } else {
                        const float k = rough * rough * 0.5f;
                        const float unhidden = 1.0f / ((facing * (1.0f - k) + k) * (seen * (1.0f - k) + k));
                        const float glance = 1.0f - eye_half, glance5 = glance * glance * glance * glance * glance;
                        share = math::scaled(mirrors_at(material, glance5, seen), kept(material, seen)) * (unhidden * facing * eye_half / up / mirrors);
                    }
                    thrown = math::scaled(share, along(lifted, way, bounces_left - 1, random, false, chosen));
                }
            }
            // The coat's own way: one of its small faces chosen as its highlight spreads them, about its normal.
            if (coated > 0) {
                const float coat_rough = std::clamp(layer.roughness, 0.04f, 1.0f), spread = coat_rough * coat_rough * coat_rough * coat_rough;
                const float pick = static_cast<float>(math::unit24(random.next())), turn = static_cast<float>(math::unit24(random.next())) * 6.2831853f;
                const float up = std::sqrt((1.0f - pick) / (1.0f + (spread - 1.0f) * pick)), out = std::sqrt(std::max(1.0f - up * up, 0.0f));
                const Vec3 coat_aside = math::normalized(math::cross(std::abs(met->coat_normal.y) < 0.9f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, met->coat_normal)), coat_other = math::cross(met->coat_normal, coat_aside);
                const Vec3 half = math::normalized(coat_aside * (out * static_cast<float>(math::det::cos(static_cast<double>(turn)))) + coat_other * (out * static_cast<float>(math::det::sin(static_cast<double>(turn)))) + met->coat_normal * up);
                const float eye_half = math::dot(toward_eye, half), seen = math::dot(met->coat_normal, toward_eye);
                const Vec3 way = half * (2 * eye_half) - toward_eye;
                const float facing = math::dot(met->coat_normal, way);
                if (eye_half > 0 && seen > 0 && facing > 0 && up > 0 && math::dot(met->face, way) > 0) {
                    const float k = coat_rough * coat_rough * 0.5f;
                    const float unhidden = 1.0f / ((facing * (1.0f - k) + k) * (seen * (1.0f - k) + k));
                    const float glance = 1.0f - eye_half, glance5 = glance * glance * glance * glance * glance;
                    const float under = up * up * (spread - 1.0f) + 1.0f;
                    const Vec3 share = math::scaled(mirrors_at(layer, glance5, seen), kept(layer, seen)) * (unhidden * facing * eye_half / up);
                    coat = coat + math::scaled(share, along(lifted, way, bounces_left - 1, random, false, spread / (3.14159265f * under * under) * up / (4.0f * eye_half)));
                }
            }
        } else {
            const Environment& about = options.environment.on ? options.environment : Environment{true, options.background, options.background, options.background};
            around = options.environment.on ? options.environment.falling(met->normal) : options.background;
            if (!matt) thrown = mirrored(material, about, drawn ? drawn_out_normal(material, met->normal, met->drawn_out, toward_eye) : met->normal, toward_eye);
            if (sheened) sheen = sheen + math::scaled(material.sheen, around) * sheen_share(std::max(math::dot(met->normal, toward_eye), 0.0f), material.sheen_roughness);
            if (coated > 0) coat = coat + mirrored(layer, about, met->coat_normal, toward_eye);
        }
        // What comes through it: the eye's ray goes on through one of its small faces (chosen as the highlight
        // spreads them, as rough as the surface bends: none at an index of 1) — out of a thin wall again at once,
        // the same way a flat one would leave it; into a solid, across its inside to where it leaves.
        Vec3 come_through{0, 0, 0};
        if (lets * (1.0f - metal) > 0) {
            const float bends = 1.0f / std::max(material.ior, 1e-3f);
            const float rough_through = std::clamp(material.roughness, 0.0f, 1.0f) * std::clamp(material.ior * 2.0f - 2.0f, 0.0f, 1.0f);
            Vec3 face = met->normal;
            if (rough_through > 0) {
                const float first = static_cast<float>(math::unit24(random.next())), second = static_cast<float>(math::unit24(random.next()));
                const float spread = rough_through * rough_through * rough_through * rough_through;
                const float up = std::sqrt((1.0f - first) / (1.0f + (spread - 1.0f) * first));
                face = about_normal(std::sqrt(std::max(1.0f - up * up, 0.0f)), second * 6.2831853f, up);
            }
            const std::optional<Vec3> in = bent(toward, face, bends);
            const Vec3 under = met->place - met->face * 2e-3f;
            if (in && material.thickness > 0) {
                come_through = bounces_left > 0 ? along(under, *in, bounces_left - 1, random, from_eye, 0, met->thing) : sky(*in);
            } else if (in) {
                const Vec3 out = bent(*in, met->face, 1.0f / bends).value_or(toward);
                come_through = bounces_left > 0 ? along(under, out, bounces_left - 1, random, from_eye) : sky(out);
            }
            come_through = math::scaled(material.base, come_through) * (lets * (1.0f - metal));
        }
        Vec3 under = math::scaled(colour, arriving + around) + sent + thrown + come_through;
        if (sheened) {
            const float brightest = std::max({material.sheen.x, material.sheen.y, material.sheen.z});
            under = under * (1.0f - brightest * sheen_share(std::max(math::dot(met->normal, toward_eye), 0.0f), material.sheen_roughness)) + sheen;
        }
        if (coated > 0) {
            // How much the coat mirrors of the eye's way (Schlick, 4% head on): that much less comes up through it.
            const float glance = 1.0f - std::clamp(math::dot(met->coat_normal, toward_eye), 0.0f, 1.0f), glance5 = glance * glance * glance * glance * glance;
            under = under * (1.0f - coated * (0.04f + 0.96f * glance5)) + coat * coated;
        }
        return material.emissive + under;
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
        // A picture is asked where the mesh has the texture coordinates it is laid by.
        const auto picture = [&](std::uint32_t which, const Placing& placing) {
            return (which < assets.textures.size() && !(placing.set ? mesh.second_uvs : mesh.uvs).empty()) ? assets.textures[which] : nullptr;
        };
        things.push_back({*back, math::transposed(*back), geom::moved(instance.world, instance.bounds), &mesh, assets.trees[instance.mesh], material,
                          picture(material.texture, material.placing.base), instance.casts, instance.world, picture(material.surface, material.placing.surface),
                          picture(material.normal, material.placing.normal), picture(material.glow, material.placing.glow), picture(material.clearness, material.placing.clearness),
                          picture(material.thick, material.placing.thick)});
    }
    std::vector<Lamp> lamps;
    for (const scene::Light& light : scene.lights())
        if (!light.hidden && (light.layers & view.layers) != 0) {
            const LampShape shape = lamp_shape(light);
            lamps.push_back({light.kind, shape.colour, light.at, math::normalized(light.toward), std::max(light.range, 1e-6f), shape.cone_cosine, shape.physical, shape.inner_cosine});
        }
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
