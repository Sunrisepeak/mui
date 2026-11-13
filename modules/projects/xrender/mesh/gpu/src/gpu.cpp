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
};
struct SurfaceUniforms {
    Row          base, emissive, ambient, eye, fog, fog_shape;
    LampUniforms lamps[most_lamps];
};
Row row_of(const Mat4& matrix, int row) { return {matrix.at(0, row), matrix.at(1, row), matrix.at(2, row), matrix.at(3, row)}; }
Row of(Vec3 value, float last = 0) { return {value.x, value.y, value.z, last}; }

constexpr std::array<rhi::VertexAttribute, 2> attributes{rhi::VertexAttribute{0, rhi::VertexFormat::float3, 0}, rhi::VertexAttribute{1, rhi::VertexFormat::float3, 12}};

}  // namespace

rhi::ShaderFormats Pass::shader_formats() noexcept { return shaders::mesh::mesh_vs.formats(); }

rhi::Result<Pass> Pass::make(rhi::Device& device, bool reversed_depth) {
    const std::array<rhi::VertexBufferLayout, 1> layout{rhi::VertexBufferLayout{sizeof(Corner), false, attributes}};
    auto pipeline = device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::mesh::mesh_vs,
                                                            .fragment = &shaders::mesh::mesh_fs,
                                                            .vertex_buffers = layout,
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
                                                       .blend = rhi::Blend::replace(),
                                                       .depth_target = rhi::Format::d32_float,
                                                       .depth = {.test = false, .write = false},
                                                       .label = "xrender.mesh.sky"});
    auto cover = device.create(rhi::BufferDesc{3 * sizeof(Corner), rhi::BufferUsage::vertex, "xrender.mesh.cover"});
    Pass pass;
    pass.device_ = &device, pass.pipeline_ = *pipeline, pass.ids_pipeline_ = *ids, pass.reversed_ = reversed_depth;
    if (sky) pass.sky_pipeline_ = *sky;
    if (cover) pass.cover_ = *cover;
    if (!sky) return std::unexpected(sky.error());  // `pass` releases what was made
    if (!cover) return std::unexpected(cover.error());
    // One triangle past the picture's corners covers it.
    const std::array<Corner, 3> corners{Corner{{-1, -1, 0}, {}}, Corner{{3, -1, 0}, {}}, Corner{{-1, 3, 0}, {}}};
    rhi::CommandList list;
    list.upload(*cover, 0, std::span<const Corner>(corners));
    auto submitted = device.submit(list);
    if (!submitted) return std::unexpected(submitted.error());
    if (auto waited = device.wait(*submitted); !waited) return std::unexpected(waited.error());
    return pass;
}
Pass::Pass(Pass&& other) noexcept
    : device_(std::exchange(other.device_, nullptr)), pipeline_(std::exchange(other.pipeline_, {})), ids_pipeline_(std::exchange(other.ids_pipeline_, {})), sky_pipeline_(std::exchange(other.sky_pipeline_, {})), cover_(std::exchange(other.cover_, {})), meshes_(std::move(other.meshes_)), reversed_(other.reversed_), last_(other.last_) {}
Pass& Pass::operator=(Pass&& other) noexcept {
    if (this != &other) {
        release();
        device_ = std::exchange(other.device_, nullptr), pipeline_ = std::exchange(other.pipeline_, {}), ids_pipeline_ = std::exchange(other.ids_pipeline_, {}), sky_pipeline_ = std::exchange(other.sky_pipeline_, {}), cover_ = std::exchange(other.cover_, {}), meshes_ = std::move(other.meshes_), reversed_ = other.reversed_, last_ = other.last_;
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
                  rhi::Texture depth, std::uint32_t width, std::uint32_t height, const Options& options) {
    last_ = {};
    const Mat4 clip = view.projection * view.view;
    const std::optional<Mat4> camera = math::inverse(view.view);
    const Vec3 eye = camera ? math::point(*camera, {0, 0, 0}) : Vec3{};
    const std::span<const scene::Instance> instances = scene.instances();

    SurfaceUniforms surface{};
    for (const std::uint32_t index : extracted.lights) {
        if (index >= scene.lights().size()) continue;
        if (last_.lamps == most_lamps) {
            ++last_.lamps_left_out;
            continue;
        }
        const scene::Light& light = scene.lights()[index];
        const float kind = light.kind == scene::LightKind::sun ? 0.0f : light.kind == scene::LightKind::spot ? 2.0f : 1.0f;
        surface.lamps[last_.lamps++] = {of(light.colour * light.intensity, kind), of(light.at, std::max(light.range, 1e-6f)),
                                        of(math::normalized(light.toward), static_cast<float>(math::det::cos(static_cast<double>(light.cone))))};
    }
    surface.ambient = of(options.ambient);
    surface.eye = of(eye, static_cast<float>(last_.lamps));
    surface.fog = of(options.fog.colour, options.fog.density);
    surface.fog_shape = {options.fog.base, options.fog.falloff, 0, 0};

    const auto shown = [](float linear) {
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
        sky.eye = of(eye), sky.background = of(options.background), sky.fog = surface.fog, sky.fog_shape = surface.fog_shape;
        list.set_pipeline(sky_pipeline_);
        list.push_uniforms(rhi::Stage::fragment, 0, sky);
        list.bind_vertex_buffer(0, cover_);
        list.draw(3);
    }
    list.set_pipeline(pipeline_);
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
        surface.base = of(materials[draw.material].base), surface.emissive = of(materials[draw.material].emissive);
        list.push_uniforms(rhi::Stage::vertex, 0, uniforms);
        list.push_uniforms(rhi::Stage::fragment, 0, surface);
        list.bind_vertex_buffer(0, meshes_[draw.mesh].corners);
        list.draw(meshes_[draw.mesh].count);
        ++last_.draws, last_.triangles += meshes_[draw.mesh].count / 3;
    }
    list.end_render();
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
