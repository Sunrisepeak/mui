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
    float uv[2]{0, 0};
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
    Row          picture, picture_sheet;
    Row          open;
    LampUniforms lamps[most_lamps];
};
Row row_of(const Mat4& matrix, int row) { return {matrix.at(0, row), matrix.at(1, row), matrix.at(2, row), matrix.at(3, row)}; }
Row of(Vec3 value, float last = 0) { return {value.x, value.y, value.z, last}; }

// A frame's jitter: every corner moved by so much of a pixel — in clip space, by so much of its own w.
Mat4 aside_of(math::Vec2 jitter, std::uint32_t width, std::uint32_t height) {
    Mat4 aside;
    if (width > 0 && height > 0) aside.at(3, 0) = 2.0f * jitter.x / static_cast<float>(width), aside.at(3, 1) = -2.0f * jitter.y / static_cast<float>(height);
    return aside;
}

constexpr std::array<rhi::VertexAttribute, 3> attributes{rhi::VertexAttribute{0, rhi::VertexFormat::float3, 0}, rhi::VertexAttribute{1, rhi::VertexFormat::float3, 12},
                                                         rhi::VertexAttribute{2, rhi::VertexFormat::float2, 24}};

// A number as a device's half float (rgba16_float): the nearest it has, what is too large its largest.
std::uint16_t half_of(float value) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xffu) - 127 + 15;
    const std::uint32_t mantissa = bits & 0x7fffffu;
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<std::uint16_t>(sign);
        const std::uint32_t whole = (mantissa | 0x800000u) >> (1 - exponent);
        return static_cast<std::uint16_t>(sign | ((whole + 0x1000u) >> 13));
    }
    if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7bffu);
    return static_cast<std::uint16_t>((sign | (static_cast<std::uint32_t>(exponent) << 10) | (mantissa >> 13)) + ((mantissa >> 12) & 1u));
}

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
    auto who = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::ids::ids_vs,
                                                       .fragment = &shaders::ids::who_fs,
                                                       .vertex_buffers = layout,
                                                       .target = rhi::Format::r32_float,
                                                       .blend = rhi::Blend::replace(),
                                                       .depth_target = rhi::Format::d32_float,
                                                       .depth = {.test = true, .write = true, .compare = reversed_depth ? rhi::Compare::greater : rhi::Compare::less},
                                                       .label = "xrender.mesh.who"});
    auto velocity = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::velocity::velocity_vs,
                                                            .fragment = &shaders::velocity::velocity_fs,
                                                            .vertex_buffers = layout,
                                                            .target = rhi::Format::rgba16_float,
                                                            .blend = rhi::Blend::replace(),
                                                            .depth_target = rhi::Format::d32_float,
                                                            .depth = {.test = true, .write = true, .compare = reversed_depth ? rhi::Compare::greater : rhi::Compare::less},
                                                            .label = "xrender.mesh.velocity"});
    // Both of them in one drawing of the scene, where the device has two colour targets a pass.
    const std::array<rhi::MoreTarget, 1> and_who{rhi::MoreTarget{rhi::Format::r32_float, rhi::Blend::replace()}};
    rhi::Result<rhi::GraphicsPipeline> velocity_who = rhi::GraphicsPipeline{};
    if (device.caps().max_color_targets >= 2)
        velocity_who = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::velocity::velocity_vs,
                                                               .fragment = &shaders::velocity::velocity_who_fs,
                                                               .vertex_buffers = layout,
                                                               .target = rhi::Format::rgba16_float,
                                                               .blend = rhi::Blend::replace(),
                                                               .depth_target = rhi::Format::d32_float,
                                                               .depth = {.test = true, .write = true, .compare = reversed_depth ? rhi::Compare::greater : rhi::Compare::less},
                                                               .more_targets = and_who,
                                                               .label = "xrender.mesh.velocity-who"});
    auto surface = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::ids::ids_vs,
                                                           .fragment = &shaders::ids::surface_fs,
                                                           .vertex_buffers = layout,
                                                           .target = rhi::Format::rgba16_float,
                                                           .blend = rhi::Blend::replace(),
                                                           .depth_target = rhi::Format::d32_float,
                                                           .depth = {.test = true, .write = true, .compare = reversed_depth ? rhi::Compare::greater : rhi::Compare::less},
                                                           .label = "xrender.mesh.surface"});
    auto nearest = device.create(rhi::SamplerDesc{rhi::Filter::nearest, rhi::Filter::nearest});
    auto no_shadow = device.create(rhi::TextureDesc{rhi::Format::r32_float, 1, 1, rhi::TextureUsage::color_target | rhi::TextureUsage::sampled, "xrender.mesh.no-shadow"});
    Pass pass;
    pass.device_ = &device, pass.pipeline_ = *pipeline, pass.ids_pipeline_ = *ids, pass.reversed_ = reversed_depth, pass.light_ = light;
    if (sky) pass.sky_pipeline_ = *sky;
    if (cover) pass.cover_ = *cover;
    if (shadow) pass.shadow_pipeline_ = *shadow;
    if (lamp_shadow) pass.lamp_shadow_pipeline_ = *lamp_shadow;
    if (who) pass.who_pipeline_ = *who;
    if (velocity) pass.velocity_pipeline_ = *velocity;
    if (velocity_who) pass.velocity_who_pipeline_ = *velocity_who;
    if (surface) pass.surface_pipeline_ = *surface;
    if (nearest) pass.nearest_ = *nearest;
    if (no_shadow) pass.no_shadow_ = *no_shadow;
    if (!sky) return std::unexpected(sky.error());  // `pass` releases what was made
    if (!cover) return std::unexpected(cover.error());
    if (!shadow) return std::unexpected(shadow.error());
    if (!lamp_shadow) return std::unexpected(lamp_shadow.error());
    if (!who) return std::unexpected(who.error());
    if (!velocity) return std::unexpected(velocity.error());
    if (!velocity_who) return std::unexpected(velocity_who.error());
    if (!surface) return std::unexpected(surface.error());
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
    : device_(std::exchange(other.device_, nullptr)), pipeline_(std::exchange(other.pipeline_, {})), ids_pipeline_(std::exchange(other.ids_pipeline_, {})), sky_pipeline_(std::exchange(other.sky_pipeline_, {})), shadow_pipeline_(std::exchange(other.shadow_pipeline_, {})), lamp_shadow_pipeline_(std::exchange(other.lamp_shadow_pipeline_, {})), who_pipeline_(std::exchange(other.who_pipeline_, {})), velocity_pipeline_(std::exchange(other.velocity_pipeline_, {})), velocity_who_pipeline_(std::exchange(other.velocity_who_pipeline_, {})), surface_pipeline_(std::exchange(other.surface_pipeline_, {})), nearest_(std::exchange(other.nearest_, {})), no_shadow_(std::exchange(other.no_shadow_, {})), cover_(std::exchange(other.cover_, {})), meshes_(std::move(other.meshes_)), textures_(std::move(other.textures_)), reversed_(other.reversed_), light_(other.light_), last_(other.last_) {}
Pass& Pass::operator=(Pass&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr), pipeline_ = std::exchange(other.pipeline_, {}), ids_pipeline_ = std::exchange(other.ids_pipeline_, {}), sky_pipeline_ = std::exchange(other.sky_pipeline_, {}), shadow_pipeline_ = std::exchange(other.shadow_pipeline_, {}), lamp_shadow_pipeline_ = std::exchange(other.lamp_shadow_pipeline_, {}), who_pipeline_ = std::exchange(other.who_pipeline_, {}), velocity_pipeline_ = std::exchange(other.velocity_pipeline_, {}), velocity_who_pipeline_ = std::exchange(other.velocity_who_pipeline_, {}), surface_pipeline_ = std::exchange(other.surface_pipeline_, {}), nearest_ = std::exchange(other.nearest_, {}), no_shadow_ = std::exchange(other.no_shadow_, {}), cover_ = std::exchange(other.cover_, {}), meshes_ = std::move(other.meshes_), textures_ = std::move(other.textures_), reversed_ = other.reversed_, light_ = other.light_, last_ = other.last_;
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
    for (const Sheet& sheet : textures_)
        if (sheet.texture) device_->destroy(sheet.texture);
    textures_.clear();
    if (pipeline_) device_->destroy(pipeline_);
    if (ids_pipeline_) device_->destroy(ids_pipeline_);
    if (sky_pipeline_) device_->destroy(sky_pipeline_);
    if (cover_) device_->destroy(cover_);
    if (shadow_pipeline_) device_->destroy(shadow_pipeline_);
    if (lamp_shadow_pipeline_) device_->destroy(lamp_shadow_pipeline_);
    if (who_pipeline_) device_->destroy(who_pipeline_);
    if (velocity_pipeline_) device_->destroy(velocity_pipeline_);
    if (velocity_who_pipeline_) device_->destroy(velocity_who_pipeline_);
    if (surface_pipeline_) device_->destroy(surface_pipeline_);
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
                const math::Vec2 uv = vertex < mesh.uvs.size() ? mesh.uvs[vertex] : math::Vec2{0, 0};
                corners.push_back({{at[corner].x, at[corner].y, at[corner].z}, {normal.x, normal.y, normal.z}, {uv.x, uv.y}});
            }
        }
        if (corners.empty()) continue;
        auto buffer = device_->create(rhi::BufferDesc{static_cast<std::uint32_t>(corners.size() * sizeof(Corner)), rhi::BufferUsage::vertex, "xrender.mesh.corners"});
        if (!buffer) return std::unexpected(buffer.error());
        meshes_[which] = {*buffer, static_cast<std::uint32_t>(corners.size()), !mesh.uvs.empty(), meshes[which]->clusters};
        list.upload(*buffer, 0, std::span<const Corner>(corners));
    }
    if (list.empty()) return {};
    auto submitted = device_->submit(list);
    if (!submitted) return std::unexpected(submitted.error());
    return device_->wait(*submitted);
}

rhi::Result<void> Pass::load_textures(std::span<const Texture* const> textures) {
    static_cast<void>(device_->wait_idle());
    for (const Sheet& sheet : textures_)
        if (sheet.texture) device_->destroy(sheet.texture);
    textures_.assign(textures.size(), {});
    rhi::CommandList list;
    std::vector<std::uint16_t> texels;
    for (std::size_t which = 0; which < textures.size(); ++which) {
        if (!textures[which] || textures[which]->sizes.empty()) continue;
        const Texture& picture = *textures[which];
        // The finest size at the sheet's corner, the others in a column beside it.
        const std::uint32_t wide = static_cast<std::uint32_t>(picture.width), high = static_cast<std::uint32_t>(picture.height);
        std::uint32_t column_high = 0;
        for (std::size_t size = 1; size < picture.sizes.size(); ++size) column_high += std::max(high >> size, 1u);
        const std::uint32_t sheet_wide = wide + (picture.sizes.size() > 1 ? std::max(wide >> 1, 1u) : 0u), sheet_high = std::max(high, column_high);
        texels.assign(static_cast<std::size_t>(sheet_wide) * sheet_high * 4, 0);
        std::uint32_t left = 0, top = 0;
        for (std::size_t size = 0; size < picture.sizes.size(); ++size) {
            const std::uint32_t size_wide = std::max(wide >> size, 1u), size_high = std::max(high >> size, 1u);
            for (std::uint32_t y = 0; y < size_high; ++y)
                for (std::uint32_t x = 0; x < size_wide; ++x) {
                    const Vec3 light = picture.light[picture.sizes[size] + static_cast<std::size_t>(y) * size_wide + x];
                    const std::size_t at = (static_cast<std::size_t>(top + y) * sheet_wide + left + x) * 4;
                    texels[at] = half_of(light.x), texels[at + 1] = half_of(light.y), texels[at + 2] = half_of(light.z), texels[at + 3] = half_of(1.0f);
                }
            if (size == 0) left = wide;
            else top += size_high;
        }
        auto made = device_->create(rhi::TextureDesc{rhi::Format::rgba16_float, sheet_wide, sheet_high, rhi::TextureUsage::sampled, "xrender.mesh.picture"});
        if (!made) return std::unexpected(made.error());
        textures_[which] = {*made, static_cast<float>(wide), static_cast<float>(high), static_cast<float>(picture.sizes.size()), static_cast<float>(sheet_wide), static_cast<float>(sheet_high)};
        list.upload(*made, rhi::Region{0, 0, sheet_wide, sheet_high}, std::as_bytes(std::span<const std::uint16_t>(texels)));
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
            const mlibs::geom::Frustum sees = mlibs::geom::Frustum::of(to_light);
            for (const scene::Instance& instance : instances) {
                if (instance.hidden || !instance.casts || instance.mesh >= meshes_.size() || !meshes_[instance.mesh].corners) continue;
                const Mat4 through = to_light * instance.world;
                DrawUniforms uniforms{};
                for (int which = 0; which < 4; ++which) uniforms.to_clip[which] = row_of(through, which);
                // The clusters the map can hold: what the sun's box does not reach casts nothing into it.
                if (shadow_runs_for(meshes_[instance.mesh], instance.world, sees, options.cull_clusters)) {
                    list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
                    list.bind_vertex_buffer(0, meshes_[instance.mesh].corners);
                    for (const Run& run : runs_) list.draw(run.count, 1, run.first);
                }
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
                const mlibs::geom::Frustum sees = mlibs::geom::Frustum::of(to_face);
                for (const scene::Instance& instance : instances) {
                    if (instance.hidden || !instance.casts || instance.mesh >= meshes_.size() || !meshes_[instance.mesh].corners) continue;
                    const Mat4 through = to_face * instance.world;
                    DrawUniforms uniforms{};
                    for (int into = 0; into < 4; ++into) uniforms.to_clip[into] = row_of(through, into);
                    for (int into = 0; into < 3; ++into) uniforms.to_world[into] = row_of(instance.world, into);
                    // One face of the six sees a sixth of what is around the lamp, and no further than it reaches.
                    if (!shadow_runs_for(meshes_[instance.mesh], instance.world, sees, options.cull_clusters)) continue;
                    list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
                    list.bind_vertex_buffer(0, meshes_[instance.mesh].corners);
                    for (const Run& run : runs_) list.draw(run.count, 1, run.first);
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
    const bool opened = shadows && shadows->open && width > 0 && height > 0;
    list.bind_texture(rhi::Stage::fragment, 3, opened ? shadows->open : no_shadow_, nearest_);
    if (opened) surface.open = {1, 0, 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)};
    // What the frame's view holds, aside by its jitter as the corners are drawn.
    const mlibs::geom::Frustum frustum = mlibs::geom::Frustum::of(aside_of(options.jitter, width, height) * clip);
    for (const scene::Draw& draw : extracted.draws) {
        if (draw.instance >= instances.size() || draw.mesh >= meshes_.size() || !meshes_[draw.mesh].corners || draw.material >= materials.size()) {
            ++last_.missing;
            continue;
        }
        const scene::Instance& instance = instances[draw.instance];
        const Mat4 to_clip = aside_of(options.jitter, width, height) * clip * instance.world;
        const std::optional<Mat4> back = math::inverse(instance.world);
        const Mat4 for_normals = back ? math::transposed(*back) : instance.world;
        DrawUniforms uniforms{};
        for (int row = 0; row < 4; ++row) uniforms.to_clip[row] = row_of(to_clip, row);
        for (int row = 0; row < 3; ++row) uniforms.to_world[row] = row_of(instance.world, row), uniforms.normals[row] = row_of(for_normals, row);
        const Material& material = materials[draw.material];
        surface.base = of(material.base), surface.emissive = of(material.emissive);
        surface.shine = {std::clamp(material.roughness, 0.04f, 1.0f), std::clamp(material.metallic, 0.0f, 1.0f), material.matt() ? 1.0f : 0.0f, light_ ? 1.0f : 0.0f};
        // Its picture, where the material names one that is loaded and the mesh says where its corners are in it.
        const Sheet* sheet = material.texture < textures_.size() && textures_[material.texture].texture && meshes_[draw.mesh].placed ? &textures_[material.texture] : nullptr;
        surface.picture = sheet ? Row{sheet->width, sheet->height, sheet->sizes, 0} : Row{};
        surface.picture_sheet = sheet ? Row{1.0f / sheet->sheet_width, 1.0f / sheet->sheet_height, 0, 0} : Row{};
        // Its clusters that may show: one outside the view or wholly facing away is left out (as the reference
        // leaves it out), and those that follow one another in the corners are one draw call.
        const Loaded& loaded = meshes_[draw.mesh];
        runs_for(loaded, instance.world, back, frustum, eye, options.cull_clusters, true);
        if (runs_.empty()) continue;  // nothing of it may show: no draw, nothing bound
        list.bind_texture(rhi::Stage::fragment, 2, sheet ? sheet->texture : no_shadow_, nearest_);
        if (sheet) ++last_.textured;
        list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
        list.push_uniforms(rhi::Stage::fragment, 0, surface);
        list.bind_vertex_buffer(0, loaded.corners);
        for (const Run& run : runs_) {
            list.draw(run.count, 1, run.first);
            last_.triangles += run.count / 3;
        }
        ++last_.draws, last_.runs += static_cast<std::uint32_t>(runs_.size());
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

void Glow::record(rhi::CommandList& list, rhi::Texture light, rhi::Texture one, rhi::Texture other, rhi::Texture colour, std::uint32_t width, std::uint32_t height, const Bloom& bloom, const Tone& tone) {
    struct Uniforms {
        Row size, along, amount;
    } uniforms{};
    const int radius = static_cast<int>(bloom.radius);
    const bool spreads = radius >= 1 && bloom.strength > 0;
    uniforms.size = {static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)};
    uniforms.amount = {bloom.threshold, spreads ? bloom.strength : 0.0f, tone.exposure, tone.filmic ? 1.0f : 0.0f};
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

// A mesh's runs of corners that may show in a view: every cluster but those outside it or wholly facing away
// from the eye (the reference's rule), the clusters that follow one another joined.
void Pass::runs_for(const Loaded& loaded, const Mat4& world, const std::optional<Mat4>& back, const mlibs::geom::Frustum& frustum, Vec3 eye, bool cull, bool counted) {
    runs_.clear();
    if (!cull || loaded.clusters.empty()) {
        runs_.push_back({0, loaded.count});
        if (counted) last_.clusters += static_cast<std::uint32_t>(loaded.clusters.size());
        return;
    }
    const std::array<float, 3> stretch{math::length(math::direction(world, {1, 0, 0})), math::length(math::direction(world, {0, 1, 0})), math::length(math::direction(world, {0, 0, 1}))};
    const float largest = std::max({stretch[0], stretch[1], stretch[2]}), smallest = std::min({stretch[0], stretch[1], stretch[2]});
    // A cluster's cone is a cone still only where the instance is stretched the same every way.
    const bool even = back && largest - smallest <= largest * 1e-4f;
    const Vec3 eye_in_mesh = even ? math::point(*back, eye) : Vec3{};
    for (const mlibs::geom::Cluster& cluster : loaded.clusters) {
        if (counted) ++last_.clusters;
        if (!frustum.holds(mlibs::geom::Sphere{math::point(world, cluster.bounds.centre), cluster.bounds.radius * largest})) {
            if (counted) ++last_.clusters_outside;
            continue;
        }
        if (even && mlibs::geom::faces_away(cluster, eye_in_mesh)) {
            if (counted) ++last_.clusters_away;
            continue;
        }
        const std::uint32_t first = std::min(cluster.first_index, loaded.count), count = std::min(cluster.triangle_count * 3, loaded.count - first);
        if (count == 0) continue;
        if (!runs_.empty() && runs_.back().first + runs_.back().count == first) runs_.back().count += count;
        else runs_.push_back({first, count});
    }
}

// A mesh's runs of corners a light's view can hold (false: none). Both sides of what casts are in a map, so
// nothing is left out for facing away.
bool Pass::shadow_runs_for(const Loaded& loaded, const Mat4& world, const mlibs::geom::Frustum& sees, bool cull) {
    const std::uint32_t before = last_.clusters, outside = last_.clusters_outside;
    runs_for(loaded, world, std::nullopt, sees, {}, cull, true);
    // Counted as the shadow views', not as the eye's.
    last_.shadow_clusters += last_.clusters - before, last_.shadow_clusters_outside += last_.clusters_outside - outside;
    last_.clusters = before, last_.clusters_outside = outside;
    last_.shadow_runs += static_cast<std::uint32_t>(runs_.size());
    for (const Run& run : runs_) last_.shadow_triangles += run.count / 3;
    return !runs_.empty();
}

void Pass::record_ids(rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, rhi::Texture ids, rhi::Texture depth, std::uint32_t width,
                      std::uint32_t height) {
    who_pass(list, scene, extracted, view, ids, depth, width, height, ids_pipeline_, false, {0, 0});
}
void Pass::record_who(rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, rhi::Texture who, rhi::Texture depth, std::uint32_t width,
                      std::uint32_t height, const Options& options) {
    who_pass(list, scene, extracted, view, who, depth, width, height, who_pipeline_, true, options.jitter);
}
void Pass::record_surface(rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, rhi::Texture surface, rhi::Texture depth,
                          std::uint32_t width, std::uint32_t height, const Options& options) {
    who_pass(list, scene, extracted, view, surface, depth, width, height, surface_pipeline_, false, options.jitter);
}
void Pass::who_pass(rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, rhi::Texture ids, rhi::Texture depth, std::uint32_t width,
                    std::uint32_t height, rhi::GraphicsPipeline pipeline, bool by_instance, math::Vec2 jitter) {
    struct Who {
        Row           eye;
        std::uint32_t number = 0, spare[3]{};
    };
    const Mat4 clip = aside_of(jitter, width, height) * view.projection * view.view;
    const std::optional<Mat4> camera = math::inverse(view.view);
    const Vec3 eye = camera ? math::point(*camera, {0, 0, 0}) : Vec3{};
    const std::span<const scene::Instance> instances = scene.instances();
    const mlibs::geom::Frustum frustum = mlibs::geom::Frustum::of(clip);
    list.begin_render({ids, rhi::LoadOp::clear, {0, 0, 0, 0}}, {depth, rhi::LoadOp::clear, reversed_ ? 0.0f : 1.0f});
    list.set_viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
    list.set_pipeline(pipeline);
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
        runs_for(meshes_[draw.mesh], instance.world, back, frustum, eye, camera.has_value(), false);
        if (runs_.empty()) continue;
        list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
        list.push_uniforms(rhi::Stage::fragment, 0, Who{of(eye), by_instance ? static_cast<std::uint32_t>(instance.id & 0xffffffu) + 1 : number + 1, {}});
        list.bind_vertex_buffer(0, meshes_[draw.mesh].corners);
        for (const Run& run : runs_) list.draw(run.count, 1, run.first);
    }
    list.end_render();
}

void Pass::record_velocity(rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, rhi::Texture velocity, rhi::Texture depth,
                           std::uint32_t width, std::uint32_t height, const Options& options) {
    velocity_pass(list, scene, extracted, view, velocity, {}, depth, width, height, options);
}
void Pass::record_velocity_and_who(rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, rhi::Texture velocity, rhi::Texture who,
                                   rhi::Texture depth, std::uint32_t width, std::uint32_t height, const Options& options) {
    if (velocity_who_pipeline_) {
        velocity_pass(list, scene, extracted, view, velocity, who, depth, width, height, options);
        return;
    }
    record_who(list, scene, extracted, view, who, depth, width, height, options);
    record_velocity(list, scene, extracted, view, velocity, depth, width, height, options);
}
void Pass::velocity_pass(rhi::CommandList& list, const scene::Scene& scene, const scene::Extracted& extracted, const scene::View& view, rhi::Texture velocity, rhi::Texture who,
                         rhi::Texture depth, std::uint32_t width, std::uint32_t height, const Options& options) {
    struct VelocityDraw {
        Row to_clip[4];
        Row to_world[3];
        Row normals[3];
        Row before[4];
    };
    struct Moved {
        Row eye, size_jitter, still;
    };
    const Mat4 clip = view.projection * view.view, drawn_clip = aside_of(options.jitter, width, height) * clip;
    const std::optional<Mat4> camera = math::inverse(view.view);
    const Vec3 eye = camera ? math::point(*camera, {0, 0, 0}) : Vec3{};
    const std::span<const scene::Instance> instances = scene.instances();
    const mlibs::geom::Frustum frustum = mlibs::geom::Frustum::of(drawn_clip);
    const rhi::DepthTarget cleared{depth, rhi::LoadOp::clear, reversed_ ? 0.0f : 1.0f};
    if (who) list.begin_render({velocity, rhi::LoadOp::clear, {0, 0, 0, 1}}, cleared, std::array<rhi::ColorTarget, 1>{rhi::ColorTarget{who, rhi::LoadOp::clear, {0, 0, 0, 0}}});
    else list.begin_render({velocity, rhi::LoadOp::clear, {0, 0, 0, 1}}, cleared);
    list.set_viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
    list.set_pipeline(who ? velocity_who_pipeline_ : velocity_pipeline_);
    for (const scene::Draw& draw : extracted.draws) {
        if (draw.instance >= instances.size() || draw.mesh >= meshes_.size() || !meshes_[draw.mesh].corners) continue;
        const scene::Instance& instance = instances[draw.instance];
        const Mat4 to_clip = clip * instance.world, jittered = drawn_clip * instance.world;
        // Where it was a frame ago: its own place then, through the view then. New this frame: where it is.
        Mat4 before = to_clip;
        if (remembered_) {
            const auto found = std::ranges::lower_bound(before_ids_, instance.id);
            before = before_clip_ * (found != before_ids_.end() && *found == instance.id ? before_world_[static_cast<std::size_t>(found - before_ids_.begin())] : instance.world);
        }
        const std::optional<Mat4> back = math::inverse(instance.world);
        const Mat4 for_normals = back ? math::transposed(*back) : instance.world;
        runs_for(meshes_[draw.mesh], instance.world, back, frustum, eye, options.cull_clusters && camera.has_value(), false);
        if (runs_.empty()) continue;
        VelocityDraw uniforms{};
        for (int row = 0; row < 4; ++row) uniforms.to_clip[row] = row_of(jittered, row), uniforms.before[row] = row_of(before, row);
        for (int row = 0; row < 3; ++row) uniforms.to_world[row] = row_of(instance.world, row), uniforms.normals[row] = row_of(for_normals, row);
        list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
        list.push_uniforms(rhi::Stage::fragment, 0,
                           Moved{of(eye),
                                 {static_cast<float>(width), static_cast<float>(height), options.jitter.x, options.jitter.y},
                                 {before.values == to_clip.values ? 1.0f : 0.0f, static_cast<float>(static_cast<std::uint32_t>(instance.id & 0xffffffu) + 1), 0, 0}});
        list.bind_vertex_buffer(0, meshes_[draw.mesh].corners);
        for (const Run& run : runs_) list.draw(run.count, 1, run.first);
    }
    list.end_render();
    // The scene's instances are in id order: what the next frame looks its things up in.
    before_ids_.clear(), before_world_.clear();
    for (const scene::Instance& instance : instances) before_ids_.push_back(instance.id), before_world_.push_back(instance.world);
    before_clip_ = clip;
    remembered_ = true;
}

rhi::Result<Temporal> Temporal::make(rhi::Device& device) {
    const std::array<rhi::VertexBufferLayout, 1> layout{rhi::VertexBufferLayout{sizeof(Corner), false, attributes}};
    auto pipeline = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::resolve::resolve_vs,
                                                            .fragment = &shaders::resolve::resolve_fs,
                                                            .vertex_buffers = layout,
                                                            .target = rhi::Format::rgba16_float,
                                                            .blend = rhi::Blend::replace(),
                                                            .label = "xrender.mesh.resolve"});
    auto nearest = device.create(rhi::SamplerDesc{rhi::Filter::nearest, rhi::Filter::nearest});
    auto cover = device.create(rhi::BufferDesc{3 * sizeof(Corner), rhi::BufferUsage::vertex, "xrender.mesh.resolve.cover"});
    Temporal made;
    made.device_ = &device;
    if (pipeline) made.pipeline_ = *pipeline;
    if (nearest) made.nearest_ = *nearest;
    if (cover) made.cover_ = *cover;
    if (!pipeline) return std::unexpected(pipeline.error());  // `made` releases what was made
    if (!nearest) return std::unexpected(nearest.error());
    if (!cover) return std::unexpected(cover.error());
    const std::array<Corner, 3> corners{Corner{{-1, -1, 0}, {}}, Corner{{3, -1, 0}, {}}, Corner{{-1, 3, 0}, {}}};
    rhi::CommandList list;
    list.upload(*cover, 0, std::span<const Corner>(corners));
    auto submitted = device.submit(list);
    if (!submitted) return std::unexpected(submitted.error());
    if (auto waited = device.wait(*submitted); !waited) return std::unexpected(waited.error());
    return made;
}
Temporal::Temporal(Temporal&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)), pipeline_(std::exchange(other.pipeline_, {})), nearest_(std::exchange(other.nearest_, {})), cover_(std::exchange(other.cover_, {})) {}
Temporal& Temporal::operator=(Temporal&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr), pipeline_ = std::exchange(other.pipeline_, {}), nearest_ = std::exchange(other.nearest_, {}), cover_ = std::exchange(other.cover_, {});
    }
    return *this;
}
Temporal::~Temporal() { release(); }
void Temporal::release() noexcept {
    if (!device_) return;
    static_cast<void>(device_->wait_idle());
    if (pipeline_) device_->destroy(pipeline_);
    if (nearest_) device_->destroy(nearest_);
    if (cover_) device_->destroy(cover_);
    device_ = nullptr;
}

void Temporal::record(rhi::CommandList& list, rhi::Texture light, rhi::Texture velocity, rhi::Texture who, rhi::Texture who_before, rhi::Texture gathered_before, rhi::Texture gathered,
                      std::uint32_t width, std::uint32_t height, bool begun, float least) {
    struct Uniforms {
        Row size, how;
    } uniforms{{static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)}, {begun ? 1.0f : 0.0f, least, 0, 0}};
    list.begin_render({gathered, rhi::LoadOp::clear, {0, 0, 0, 0}});
    list.set_viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
    list.set_pipeline(pipeline_);
    list.push_uniforms(rhi::Stage::fragment, 0, uniforms);
    list.bind_texture(rhi::Stage::fragment, 0, light, nearest_);
    list.bind_texture(rhi::Stage::fragment, 1, velocity, nearest_);
    list.bind_texture(rhi::Stage::fragment, 2, who, nearest_);
    // With no last result the two are not read: something of the right kind stands in their places.
    list.bind_texture(rhi::Stage::fragment, 3, begun ? who_before : who, nearest_);
    list.bind_texture(rhi::Stage::fragment, 4, begun ? gathered_before : light, nearest_);
    list.bind_vertex_buffer(0, cover_);
    list.draw(3);
    list.end_render();
}

rhi::Result<Open> Open::make(rhi::Device& device) {
    const std::array<rhi::VertexBufferLayout, 1> layout{rhi::VertexBufferLayout{sizeof(Corner), false, attributes}};
    auto pipeline = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::open::open_vs,
                                                            .fragment = &shaders::open::open_fs,
                                                            .vertex_buffers = layout,
                                                            .target = rhi::Format::r32_float,
                                                            .blend = rhi::Blend::replace(),
                                                            .label = "xrender.mesh.open"});
    auto nearest = device.create(rhi::SamplerDesc{rhi::Filter::nearest, rhi::Filter::nearest});
    auto cover = device.create(rhi::BufferDesc{3 * sizeof(Corner), rhi::BufferUsage::vertex, "xrender.mesh.open.cover"});
    Open made;
    made.device_ = &device;
    if (pipeline) made.pipeline_ = *pipeline;
    if (nearest) made.nearest_ = *nearest;
    if (cover) made.cover_ = *cover;
    if (!pipeline) return std::unexpected(pipeline.error());  // `made` releases what was made
    if (!nearest) return std::unexpected(nearest.error());
    if (!cover) return std::unexpected(cover.error());
    const std::array<Corner, 3> corners{Corner{{-1, -1, 0}, {}}, Corner{{3, -1, 0}, {}}, Corner{{-1, 3, 0}, {}}};
    rhi::CommandList list;
    list.upload(*cover, 0, std::span<const Corner>(corners));
    auto submitted = device.submit(list);
    if (!submitted) return std::unexpected(submitted.error());
    if (auto waited = device.wait(*submitted); !waited) return std::unexpected(waited.error());
    return made;
}
Open::Open(Open&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)), pipeline_(std::exchange(other.pipeline_, {})), nearest_(std::exchange(other.nearest_, {})), cover_(std::exchange(other.cover_, {})) {}
Open& Open::operator=(Open&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr), pipeline_ = std::exchange(other.pipeline_, {}), nearest_ = std::exchange(other.nearest_, {}), cover_ = std::exchange(other.cover_, {});
    }
    return *this;
}
Open::~Open() { release(); }
void Open::release() noexcept {
    if (!device_) return;
    static_cast<void>(device_->wait_idle());
    if (pipeline_) device_->destroy(pipeline_);
    if (nearest_) device_->destroy(nearest_);
    if (cover_) device_->destroy(cover_);
    device_ = nullptr;
}

void Open::record(rhi::CommandList& list, const scene::View& view, rhi::Texture surface, rhi::Texture open, std::uint32_t width, std::uint32_t height, const Options& options,
                  std::uint32_t steps) {
    struct Uniforms {
        Row to_clip[4], from_clip[4];
        Row eye, size, how;
    } uniforms{};
    const Mat4 clip = aside_of(options.jitter, width, height) * view.projection * view.view;
    const std::optional<Mat4> back = math::inverse(clip);
    const std::optional<Mat4> camera = math::inverse(view.view);
    for (int row = 0; row < 4; ++row) uniforms.to_clip[row] = row_of(clip, row), uniforms.from_clip[row] = row_of(back ? *back : Mat4{}, row);
    uniforms.eye = of(camera ? math::point(*camera, {0, 0, 0}) : Vec3{});
    uniforms.size = {static_cast<float>(width), static_cast<float>(height), 1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height)};
    // With no way back from clip space there is nothing to step through: every place is open.
    uniforms.how = {back ? static_cast<float>(std::min(options.occlusion_rays, 32u)) : 0.0f, options.occlusion_reach, static_cast<float>(options.occlusion_seed & 0xffffffu),
                    static_cast<float>(std::clamp(steps, 1u, 64u))};
    list.begin_render({open, rhi::LoadOp::clear, {1, 0, 0, 1}});
    list.set_viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
    list.set_pipeline(pipeline_);
    list.push_uniforms(rhi::Stage::fragment, 0, uniforms);
    list.bind_texture(rhi::Stage::fragment, 0, surface, nearest_);
    list.bind_vertex_buffer(0, cover_);
    list.draw(3);
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
