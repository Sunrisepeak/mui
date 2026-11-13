module xrender.mesh.gpu;

import std;

namespace xrender::mesh::gpu {

namespace rhi = mlibs::rhi;
namespace math = mlibs::math;
using math::Mat4;
using math::Vec3;

namespace {

struct Corner {
    float position[3];
    float normal[3];
};
struct Row {
    float x = 0, y = 0, z = 0, w = 0;
};
struct DrawUniforms {
    Row to_clip[4];
    Row to_world[3];
    Row normals[3];
};
struct LampUniforms {
    Row colour_kind, at_range, toward_cone;
    Row shadow{-1, 0, 0, 0};
};
struct SurfaceUniforms {
    Row          base, emissive, ambient, eye, fog, fog_shape;
    Row          shine;
    Row          above, level, below;
    Row          to_light[3];
    Row          shadow{-1, 0, 0, 0};
    Row          lamp_shadow;
    LampUniforms lamps[most_lamps];
};
Row row_of(const Mat4& matrix, int row) { return {matrix.at(0, row), matrix.at(1, row), matrix.at(2, row), matrix.at(3, row)}; }
Row of(Vec3 value, float last = 0) { return {value.x, value.y, value.z, last}; }

constexpr std::array<rhi::VertexAttribute, 2> attributes{rhi::VertexAttribute{0, rhi::VertexFormat::float3, 0}, rhi::VertexAttribute{1, rhi::VertexFormat::float3, 12}};

}  // namespace

rhi::ShaderFormats Pass::shader_formats() noexcept { return shaders::mesh::mesh_vs.formats(); }

rhi::Result<Pass> Pass::make(rhi::Device& device, bool reversed_depth, bool light) {
    const rhi::Format shown_as = light ? rhi::Format::rgba16_float : rhi::Format::rgba8_unorm;
    const std::array<rhi::VertexBufferLayout, 1> layout{rhi::VertexBufferLayout{sizeof(Corner), false, attributes}};
    auto pipeline = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::mesh::mesh_vs,
                                                            .fragment = &shaders::mesh::mesh_fs,
                                                            .vertex_buffers = layout,
                                                            .target = shown_as,
                                                            .blend = rhi::Blend::replace(),
                                                            .depth_target = rhi::Format::d32_float,
                                                            .depth = {.test = true, .write = true, .compare = reversed_depth ? rhi::Compare::greater : rhi::Compare::less},
                                                            .label = "xrender.mesh"});
    if (!pipeline) return std::unexpected(pipeline.error());
    auto ids = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::ids::ids_vs,
                                                       .fragment = &shaders::ids::ids_fs,
                                                       .vertex_buffers = layout,
                                                       .target = rhi::Format::r32_uint,
                                                       .blend = rhi::Blend::replace(),
                                                       .depth_target = rhi::Format::d32_float,
                                                       .depth = {.test = true, .write = true, .compare = reversed_depth ? rhi::Compare::greater : rhi::Compare::less},
                                                       .label = "xrender.mesh.ids"});
    if (!ids) {
        device.destroy(*pipeline);
        return std::unexpected(ids.error());
    }
    auto sky = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::sky::sky_vs,
                                                       .fragment = &shaders::sky::sky_fs,
                                                       .vertex_buffers = layout,
                                                       .target = shown_as,
                                                       .blend = rhi::Blend::replace(),
                                                       .depth_target = rhi::Format::d32_float,
                                                       .depth = {.test = false, .write = false},
                                                       .label = "xrender.mesh.sky"});
    auto cover = device.create(rhi::BufferDesc{3 * sizeof(Corner), rhi::BufferUsage::vertex, "xrender.mesh.cover"});
    auto shadow = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::shadow::shadow_vs,
                                                          .fragment = &shaders::shadow::shadow_fs,
                                                          .vertex_buffers = layout,
                                                          .target = rhi::Format::r32_float,
                                                          .blend = rhi::Blend::replace(),
                                                          .depth_target = rhi::Format::d32_float,
                                                          .depth = {.test = true, .write = true, .compare = rhi::Compare::less},
                                                          .label = "xrender.mesh.shadow"});
    auto lamp_shadow = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::shadow::lamp_vs,
                                                               .fragment = &shaders::shadow::lamp_fs,
                                                               .vertex_buffers = layout,
                                                               .target = rhi::Format::r32_float,
                                                               .blend = rhi::Blend::replace(),
                                                               .depth_target = rhi::Format::d32_float,
                                                               .depth = {.test = true, .write = true, .compare = rhi::Compare::less},
                                                               .label = "xrender.mesh.shadow.lamp"});
    auto nearest = device.create(rhi::SamplerDesc{rhi::Filter::nearest, rhi::Filter::nearest});
    auto no_shadow = device.create(rhi::TextureDesc{rhi::Format::r32_float, 1, 1, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "xrender.mesh.no-shadow"});
    Pass pass;
    pass.device_ = &device, pass.pipeline_ = *pipeline, pass.ids_pipeline_ = *ids, pass.reversed_ = reversed_depth, pass.light_ = light;
    if (sky) pass.sky_pipeline_ = *sky;
    if (cover) pass.cover_ = *cover;
    if (shadow) pass.shadow_pipeline_ = *shadow;
    if (lamp_shadow) pass.lamp_shadow_pipeline_ = *lamp_shadow;
    if (nearest) pass.nearest_ = *nearest;
    if (no_shadow) pass.no_shadow_ = *no_shadow;
    if (!sky) return std::unexpected(sky.error());  // `pass` releases what was made
    if (!cover) return std::unexpected(cover.error());
    if (!shadow) return std::unexpected(shadow.error());
    if (!lamp_shadow) return std::unexpected(lamp_shadow.error());
    if (!nearest) return std::unexpected(nearest.error());
    if (!no_shadow) return std::unexpected(no_shadow.error());
    // One triangle past the picture's corners covers it.
    const std::array<Corner, 3> corners{Corner{{-1, -1, 0}, {}}, Corner{{3, -1, 0}, {}}, Corner{{-1, 3, 0}, {}}};
    rhi::CommandList list;
    list.upload(*cover, 0, std::span<const Corner>(corners));
    list.begin_render({*no_shadow, rhi::LoadOp::clear, {1, 0, 0, 1}});
    list.end_render();
    auto submitted = device.submit(list);
    if (!submitted) return std::unexpected(submitted.error());
    if (auto waited = device.wait(*submitted); !waited) return std::unexpected(waited.error());
    return pass;
}
Pass::Pass(Pass&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)), pipeline_(std::exchange(other.pipeline_, {})), ids_pipeline_(std::exchange(other.ids_pipeline_, {})), sky_pipeline_(std::exchange(other.sky_pipeline_, {})), shadow_pipeline_(std::exchange(other.shadow_pipeline_, {})), lamp_shadow_pipeline_(std::exchange(other.lamp_shadow_pipeline_, {})), nearest_(std::exchange(other.nearest_, {})), no_shadow_(std::exchange(other.no_shadow_, {})), cover_(std::exchange(other.cover_, {})), meshes_(std::move(other.meshes_)), reversed_(other.reversed_), light_(other.light_), last_(other.last_) {}
Pass& Pass::operator=(Pass&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr), pipeline_ = std::exchange(other.pipeline_, {}), ids_pipeline_ = std::exchange(other.ids_pipeline_, {}), sky_pipeline_ = std::exchange(other.sky_pipeline_, {}), shadow_pipeline_ = std::exchange(other.shadow_pipeline_, {}), lamp_shadow_pipeline_ = std::exchange(other.lamp_shadow_pipeline_, {}), nearest_ = std::exchange(other.nearest_, {}), no_shadow_ = std::exchange(other.no_shadow_, {}), cover_ = std::exchange(other.cover_, {}), meshes_ = std::move(other.meshes_), reversed_ = other.reversed_, light_ = other.light_, last_ = other.last_;
    }
    return *this;
}
Pass::~Pass() { release(); }
void Pass::release() noexcept {
    if (!device_) return;
    static_cast<void>(device_->wait_idle());
    for (const Loaded& mesh : meshes_)
        if (mesh.corners) device_->destroy(mesh.corners);
    meshes_.clear();
    if (pipeline_) device_->destroy(pipeline_);
    if (ids_pipeline_) device_->destroy(ids_pipeline_);
    if (sky_pipeline_) device_->destroy(sky_pipeline_);
    if (cover_) device_->destroy(cover_);
    if (shadow_pipeline_) device_->destroy(shadow_pipeline_);
    if (lamp_shadow_pipeline_) device_->destroy(lamp_shadow_pipeline_);
    if (nearest_) device_->destroy(nearest_);
    if (no_shadow_) device_->destroy(no_shadow_);
    device_ = nullptr;
}

rhi::Result<void> Pass::load(std::span<const mlibs::geom::ClusteredMesh* const> meshes) {
    static_cast<void>(device_->wait_idle());
    for (const Loaded& mesh : meshes_)
        if (mesh.corners) device_->destroy(mesh.corners);
    meshes_.assign(meshes.size(), Loaded{});
    rhi::CommandList list;
    std::vector<Corner> corners;
    for (std::size_t which = 0; which < meshes.size(); ++which) {
        if (!meshes[which]) continue;
        const mlibs::geom::Mesh& mesh = meshes[which]->mesh;
        corners.clear();
        for (std::size_t first = 0; first + 2 < mesh.indices.size(); first += 3) {
            std::array<Vec3, 3> at{};
            for (std::size_t corner = 0; corner < 3; ++corner) at[corner] = mesh.indices[first + corner] < mesh.positions.size() ? mesh.positions[mesh.indices[first + corner]] : Vec3{};
            const Vec3 face = math::cross(at[1] - at[0], at[2] - at[0]);
            for (std::size_t corner = 0; corner < 3; ++corner) {
                const std::uint32_t vertex = mesh.indices[first + corner];
                const Vec3 normal = !mesh.normals.empty() && vertex < mesh.normals.size() ? mesh.normals[vertex] : face;
                corners.push_back({{at[corner].x, at[corner].y, at[corner].z}, {normal.x, normal.y, normal.z}});
            }
        }
        if (corners.empty()) continue;
        auto buffer = device_->create(rhi::BufferDesc{static_cast<std::uint32_t>(corners.size() * sizeof(Corner)), rhi::BufferUsage::vertex, "xrender.mesh.corners"});
        if (!buffer) return std::unexpected(buffer.error());
        meshes_[which] = {*buffer, static_cast<std::uint32_t>(corners.size())};
        list.upload(*buffer, 0, std::span<const Corner>(corners));
    }
    if (list.empty()) return {};
    auto submitted = device_->submit(list);
    if (!submitted) return std::unexpected(submitted.error());
    return device_->wait(*submitted);
}

void Pass::record(rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, std::span<const Material> materials, rhi::Texture colour,
                  rhi::Texture depth, std::uint32_t width, std::uint32_t height, const Options& options, const Shadows* shadows) {
    last_ = {};
    const Mat4 clip = view.projection * view.view;
    const std::optional<Mat4> camera = math::inverse(view.view);
    const Vec3 eye = camera ? math::point(*camera, {0, 0, 0}) : Vec3{};
    const std::span<const scene::Instance> instances = scene.instances();

    SurfaceUniforms surface{};
    std::array<float, most_lamps> cones{};  // each lamp's half angle, as the scene says it
    for (const std::uint32_t index : extracted.lights) {
        if (index >= scene.lights().size()) continue;
        if (last_.lamps == most_lamps) {
            ++last_.lamps_left_out;
            continue;
        }
        const scene::Light& light = scene.lights()[index];
        const float kind = light.kind == scene::LightKind::sun ? 0.0f : light.kind == scene::LightKind::spot ? 2.0f : 1.0f;
        cones[last_.lamps] = light.cone;
        surface.lamps[last_.lamps++] = {of(light.colour * light.intensity, kind), of(light.at, std::max(light.range, 1e-6f)),
                                        of(math::normalized(light.toward), static_cast<float>(math::det::cos(static_cast<double>(light.cone))))};
    }
    surface.ambient = of(options.ambient);
    surface.above = of(options.environment.above, options.environment.on ? 1.0f : 0.0f), surface.level = of(options.environment.level), surface.below = of(options.environment.below);
    surface.eye = of(eye, static_cast<float>(last_.lamps));
    surface.fog = of(options.fog.colour, options.fog.density);
    surface.fog_shape = {options.fog.base, options.fog.falloff, 0, 0};

    // The sun's map, before the frame: what the first sun sees of everything that casts, over the part of the
    // view within `distance` of the eye — a ball about that part, looked at along the sun's way.
    rhi::Texture map = no_shadow_;
    if (shadows && options.shadows && shadows->map && shadows->depth && camera) {
        std::optional<std::uint32_t> sun;
        for (std::uint32_t index = 0; index < last_.lamps && !sun; ++index)
            if (surface.lamps[index].colour_kind.w < 0.5f) sun = index;
        const std::optional<Mat4> from_clip = math::inverse(clip);
        if (sun && from_clip) {
            const Vec3 forward{surface.lamps[*sun].toward_cone.x, surface.lamps[*sun].toward_cone.y, surface.lamps[*sun].toward_cone.z};
            std::array<Vec3, 8> corners{};
            Vec3 centre{};
            for (int corner = 0; corner < 4; ++corner) {
                const Vec3 near = math::point(*from_clip, {corner & 1 ? 1.0f : -1.0f, corner & 2 ? 1.0f : -1.0f, reversed_ ? 1.0f : 0.0f});
                const Vec3 along = math::normalized(near - eye);
                corners[static_cast<std::size_t>(corner)] = near, corners[static_cast<std::size_t>(corner) + 4] = eye + along * shadows->distance;
            }
            for (const Vec3& corner : corners) centre = centre + corner * 0.125f;
            float radius = 0;
            for (const Vec3& corner : corners) radius = std::max(radius, math::length(corner - centre));
            const Vec3 aside = math::normalized(math::cross(std::abs(forward.y) < 0.99f ? Vec3{0, 1, 0} : Vec3{1, 0, 0}, forward)), above = math::cross(forward, aside);
            const float deep = shadows->behind + 2 * radius;
            Mat4 to_light;  // rows: aside, above, forward — each a share of the map
            const auto row = [&](int which, Vec3 axis, float scale, float shift) {
                to_light.at(0, which) = axis.x * scale, to_light.at(1, which) = axis.y * scale, to_light.at(2, which) = axis.z * scale;
                to_light.at(3, which) = -math::dot(axis, centre) * scale + shift;
            };
            row(0, aside, 1.0f / radius, 0), row(1, above, 1.0f / radius, 0), row(2, forward, 1.0f / deep, (shadows->behind + radius) / deep);
            const float texel = 2 * radius / static_cast<float>(shadows->size);
            for (int which = 0; which < 3; ++which) surface.to_light[which] = row_of(to_light, which);
            surface.shadow = {static_cast<float>(*sun), texel * 2.0f, texel * 1.5f / deep, 0};

            list.begin_render({shadows->map, rhi::LoadOp::clear, {1, 0, 0, 1}}, {shadows->depth, rhi::LoadOp::clear, 1.0f});
            list.set_viewport(0, 0, static_cast<float>(shadows->size), static_cast<float>(shadows->size));
            list.set_pipeline(shadow_pipeline_);
            for (const scene::Instance& instance : instances) {
                if (instance.hidden || !instance.casts || instance.mesh >= meshes_.size() || !meshes_[instance.mesh].corners) continue;
                const Mat4 through = to_light * instance.world;
                DrawUniforms uniforms{};
                for (int which = 0; which < 4; ++which) uniforms.to_clip[which] = row_of(through, which);
                list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
                list.bind_vertex_buffer(0, meshes_[instance.mesh].corners);
                list.draw(meshes_[instance.mesh].count);
                ++last_.casters;
            }
            list.end_render();
            map = shadows->map;
        }
    }
    // The lamps' map: six views about each of the first lamps and spots, a row a lamp and a tile a view.
    rhi::Texture lamp_map = no_shadow_;
    if (shadows && options.shadows && shadows->lamps_map && shadows->lamps_depth && shadows->lamps > 0 && shadows->lamp_size > 0) {
        const float side = static_cast<float>(shadows->lamp_size);
        surface.lamp_shadow = {side, 0, 1.0f / (side * 6), 1.0f / (side * static_cast<float>(shadows->lamps))};
        list.begin_render({shadows->lamps_map, rhi::LoadOp::clear, {1, 0, 0, 1}}, {shadows->lamps_depth, rhi::LoadOp::clear, 1.0f});
        list.set_pipeline(lamp_shadow_pipeline_);
        struct Face {
            Vec3 axis, right, up;
        };
        // The same six, in the same order, as the shader's lookup.
        constexpr std::array<Face, 6> faces{Face{{1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, Face{{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}}, Face{{0, 1, 0}, {1, 0, 0}, {0, 0, 1}},
                                            Face{{0, -1, 0}, {1, 0, 0}, {0, 0, 1}}, Face{{0, 0, 1}, {1, 0, 0}, {0, 1, 0}},  Face{{0, 0, -1}, {1, 0, 0}, {0, 1, 0}}};
        for (std::uint32_t which = 0; which < last_.lamps && last_.lamps_shadowed < shadows->lamps; ++which) {
            LampUniforms& lamp = surface.lamps[which];
            if (lamp.colour_kind.w < 0.5f) continue;  // a sun
            const Vec3 at{lamp.at_range.x, lamp.at_range.y, lamp.at_range.z}, toward{lamp.toward_cone.x, lamp.toward_cone.y, lamp.toward_cone.z};
            const float reach = lamp.at_range.w, near = std::min(0.02f, reach * 0.5f);
            const std::uint32_t row = last_.lamps_shadowed++;
            lamp.shadow = {static_cast<float>(row), 0, 0, 0};
            // A spot looks one way: a view its cone does not reach (a view's corner is 54.7° off its middle) is left clear.
            const float spot_reach = lamp.colour_kind.w > 1.5f ? static_cast<float>(math::det::cos(std::min(static_cast<double>(cones[which]) + 0.9554, 3.14159265))) : -2.0f;
            list.push_uniforms(rhi::Stage::fragment, 0, lamp.at_range);
            for (std::uint32_t number = 0; number < faces.size(); ++number) {
                const Face& face = faces[number];
                if (math::dot(face.axis, toward) < spot_reach) continue;
                Mat4 to_face;
                const auto set = [&](int into, Vec3 axis, float scale, float shift) {
                    to_face.at(0, into) = axis.x * scale, to_face.at(1, into) = axis.y * scale, to_face.at(2, into) = axis.z * scale;
                    to_face.at(3, into) = -math::dot(axis, at) * scale + shift;
                };
                const float deepens = reach / (reach - near);
                set(0, face.right, 1, 0), set(1, face.up, 1, 0), set(2, face.axis, deepens, -near * deepens), set(3, face.axis, 1, 0);
                list.set_viewport(static_cast<float>(number) * side, static_cast<float>(row) * side, side, side);
                for (const scene::Instance& instance : instances) {
                    if (instance.hidden || !instance.casts || instance.mesh >= meshes_.size() || !meshes_[instance.mesh].corners) continue;
                    const Mat4 through = to_face * instance.world;
                    DrawUniforms uniforms{};
                    for (int into = 0; into < 4; ++into) uniforms.to_clip[into] = row_of(through, into);
                    for (int into = 0; into < 3; ++into) uniforms.to_world[into] = row_of(instance.world, into);
                    list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
                    list.bind_vertex_buffer(0, meshes_[instance.mesh].corners);
                    list.draw(meshes_[instance.mesh].count);
                }
                ++last_.lamp_views;
            }
        }
        list.end_render();
        lamp_map = shadows->lamps_map;
    }
    const auto shown = [&](float linear) {
        if (light_) return linear;
        const float held = std::clamp(linear, 0.0f, 1.0f);
        return held <= 0.0031308f ? held * 12.92f : 1.055f * static_cast<float>(math::det::pow(static_cast<double>(held), 1.0 / 2.4)) - 0.055f;
    };
    list.begin_render({colour, rhi::LoadOp::clear, {shown(options.background.x), shown(options.background.y), shown(options.background.z), 1.0f}},
                      {depth, rhi::LoadOp::clear, reversed_ ? 0.0f : 1.0f});
    list.set_viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
    // The background through the fog, before anything: each pixel along its own ray.
    if (const std::optional<Mat4> from_clip = options.fog.density > 0 ? math::inverse(clip) : std::nullopt) {
        struct SkyUniforms {
            Row from_clip[4];
            Row eye, background, fog, fog_shape;
        } sky{};
        for (int row = 0; row < 4; ++row) sky.from_clip[row] = row_of(*from_clip, row);
        sky.eye = of(eye), sky.background = of(options.background, light_ ? 1.0f : 0.0f), sky.fog = surface.fog, sky.fog_shape = surface.fog_shape;
        list.set_pipeline(sky_pipeline_);
        list.push_uniforms(rhi::Stage::fragment, 0, sky);
        list.bind_vertex_buffer(0, cover_);
        list.draw(3);
    }
    list.set_pipeline(pipeline_);
    list.bind_texture(rhi::Stage::fragment, 0, map, nearest_);
    list.bind_texture(rhi::Stage::fragment, 1, lamp_map, nearest_);
    for (const scene::Draw& draw : extracted.draws) {
        if (draw.instance >= instances.size() || draw.mesh >= meshes_.size() || !meshes_[draw.mesh].corners || draw.material >= materials.size()) {
            ++last_.missing;
            continue;
        }
        const scene::Instance& instance = instances[draw.instance];
        const Mat4 to_clip = clip * instance.world;
        const std::optional<Mat4> back = math::inverse(instance.world);
        const Mat4 for_normals = back ? math::transposed(*back) : instance.world;
        DrawUniforms uniforms{};
        for (int row = 0; row < 4; ++row) uniforms.to_clip[row] = row_of(to_clip, row);
        for (int row = 0; row < 3; ++row) uniforms.to_world[row] = row_of(instance.world, row), uniforms.normals[row] = row_of(for_normals, row);
        const Material& material = materials[draw.material];
        surface.base = of(material.base), surface.emissive = of(material.emissive);
        surface.shine = {std::clamp(material.roughness, 0.04f, 1.0f), std::clamp(material.metallic, 0.0f, 1.0f), material.matt() ? 1.0f : 0.0f, light_ ? 1.0f : 0.0f};
        list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
        list.push_uniforms(rhi::Stage::fragment, 0, surface);
        list.bind_vertex_buffer(0, meshes_[draw.mesh].corners);
        list.draw(meshes_[draw.mesh].count);
        ++last_.draws, last_.triangles += meshes_[draw.mesh].count / 3;
    }
    list.end_render();
}

rhi::Result<Glow> Glow::make(rhi::Device& device) {
    const std::array<rhi::VertexBufferLayout, 1> layout{rhi::VertexBufferLayout{sizeof(Corner), false, attributes}};
    auto spread = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::glow::glow_vs,
                                                          .fragment = &shaders::glow::spread_fs,
                                                          .vertex_buffers = layout,
                                                          .target = rhi::Format::rgba16_float,
                                                          .blend = rhi::Blend::replace(),
                                                          .label = "xrender.mesh.glow.spread"});
    auto shown = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::glow::glow_vs,
                                                         .fragment = &shaders::glow::shown_fs,
                                                         .vertex_buffers = layout,
                                                         .blend = rhi::Blend::replace(),
                                                         .label = "xrender.mesh.glow.shown"});
    auto nearest = device.create(rhi::SamplerDesc{rhi::Filter::nearest, rhi::Filter::nearest});
    auto cover = device.create(rhi::BufferDesc{3 * sizeof(Corner), rhi::BufferUsage::vertex, "xrender.mesh.glow.cover"});
    Glow glow;
    glow.device_ = &device;
    if (spread) glow.spread_ = *spread;
    if (shown) glow.shown_ = *shown;
    if (nearest) glow.nearest_ = *nearest;
    if (cover) glow.cover_ = *cover;
    if (!spread) return std::unexpected(spread.error());  // `glow` releases what was made
    if (!shown) return std::unexpected(shown.error());
    if (!nearest) return std::unexpected(nearest.error());
    if (!cover) return std::unexpected(cover.error());
    const std::array<Corner, 3> corners{Corner{{-1, -1, 0}, {}}, Corner{{3, -1, 0}, {}}, Corner{{-1, 3, 0}, {}}};
    rhi::CommandList list;
    list.upload(*cover, 0, std::span<const Corner>(corners));
    auto submitted = device.submit(list);
    if (!submitted) return std::unexpected(submitted.error());
    if (auto waited = device.wait(*submitted); !waited) return std::unexpected(waited.error());
    return glow;
}
Glow::Glow(Glow&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)), spread_(std::exchange(other.spread_, {})), shown_(std::exchange(other.shown_, {})), nearest_(std::exchange(other.nearest_, {})), cover_(std::exchange(other.cover_, {})) {}
Glow& Glow::operator=(Glow&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr), spread_ = std::exchange(other.spread_, {}), shown_ = std::exchange(other.shown_, {}), nearest_ = std::exchange(other.nearest_, {}), cover_ = std::exchange(other.cover_, {});
    }
    return *this;
}
Glow::~Glow() { release(); }
void Glow::release() noexcept {
    if (!device_) return;
    static_cast<void>(device_->wait_idle());
    if (spread_) device_->destroy(spread_);
    if (shown_) device_->destroy(shown_);
    if (nearest_) device_->destroy(nearest_);
    if (cover_) device_->destroy(cover_);
    device_ = nullptr;
}

void Glow::record(rhi::CommandList& list, rhi::Texture light, rhi::Texture one, rhi::Texture other, rhi::Texture colour, std::uint32_t width, std::uint32_t height, const Bloom& bloom) {
    struct Uniforms {
        Row size, along, amount;
    } uniforms{};
    const int radius = static_cast<int>(bloom.radius);
    const bool spreads = radius >= 1 && bloom.strength > 0;
    uniforms.size = {static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)};
    uniforms.amount = {bloom.threshold, spreads ? bloom.strength : 0.0f, 0, 0};
    const auto pass = [&](rhi::GraphicsPipeline pipeline, rhi::Texture onto, rhi::Texture from, rhi::Texture beside) {
        list.begin_render({onto, rhi::LoadOp::clear, {0, 0, 0, 1}});
        list.set_viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
        list.set_pipeline(pipeline);
        list.push_uniforms(rhi::Stage::fragment, 0, uniforms);
        list.bind_texture(rhi::Stage::fragment, 0, from, nearest_);
        list.bind_texture(rhi::Stage::fragment, 1, beside, nearest_);
        list.bind_vertex_buffer(0, cover_);
        list.draw(3);
        list.end_render();
    };
    if (spreads)
        for (int number = 0; number < 3; ++number) {
            // Along the rows (the first time from the light, taking what is above the threshold), then down the columns.
            uniforms.along = {1, 0, static_cast<float>(radius), number == 0 ? 1.0f : 0.0f};
            pass(spread_, one, number == 0 ? light : other, light);
            uniforms.along = {0, 1, static_cast<float>(radius), 0};
            pass(spread_, other, one, light);
        }
    // With nothing spread the strength is 0 and what is beside the light is the light itself: not read into the sum.
    pass(shown_, colour, light, spreads ? other : light);
}

void Pass::record_ids(rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, rhi::Texture ids, rhi::Texture depth, std::uint32_t width,
                      std::uint32_t height) {
    struct Who {
        Row           eye;
        std::uint32_t number = 0, spare[3]{};
    };
    const Mat4 clip = view.projection * view.view;
    const std::optional<Mat4> camera = math::inverse(view.view);
    const Vec3 eye = camera ? math::point(*camera, {0, 0, 0}) : Vec3{};
    const std::span<const scene::Instance> instances = scene.instances();
    list.begin_render({ids, rhi::LoadOp::clear, {0, 0, 0, 0}}, {depth, rhi::LoadOp::clear, reversed_ ? 0.0f : 1.0f});
    list.set_viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
    list.set_pipeline(ids_pipeline_);
    for (std::uint32_t number = 0; number < extracted.draws.size(); ++number) {
        const scene::Draw& draw = extracted.draws[number];
        if (draw.instance >= instances.size() || draw.mesh >= meshes_.size() || !meshes_[draw.mesh].corners) continue;
        const scene::Instance& instance = instances[draw.instance];
        const Mat4 to_clip = clip * instance.world;
        const std::optional<Mat4> back = math::inverse(instance.world);
        const Mat4 for_normals = back ? math::transposed(*back) : instance.world;
        DrawUniforms uniforms{};
        for (int row = 0; row < 4; ++row) uniforms.to_clip[row] = row_of(to_clip, row);
        for (int row = 0; row < 3; ++row) uniforms.to_world[row] = row_of(instance.world, row), uniforms.normals[row] = row_of(for_normals, row);
        list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
        list.push_uniforms(rhi::Stage::fragment, 0, Who{of(eye), number + 1, {}});
        list.bind_vertex_buffer(0, meshes_[draw.mesh].corners);
        list.draw(meshes_[draw.mesh].count);
    }
    list.end_render();
}

std::uint32_t who(std::span<const std::byte> pixels, std::uint32_t width, std::uint32_t x, std::uint32_t y) noexcept {
    const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4;
    if (at + 3 >= pixels.size()) return none;
    const std::uint32_t number = std::to_integer<std::uint32_t>(pixels[at]) | std::to_integer<std::uint32_t>(pixels[at + 1]) << 8 | std::to_integer<std::uint32_t>(pixels[at + 2]) << 16 |
                                 std::to_integer<std::uint32_t>(pixels[at + 3]) << 24;
    return number == 0 ? none : number - 1;
}

}  // namespace xrender::mesh::gpu
