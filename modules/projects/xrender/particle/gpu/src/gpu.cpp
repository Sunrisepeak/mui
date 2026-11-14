module xrender.particle.gpu;

import std;
import mlibs.math;

namespace xrender::particle::gpu {

namespace rhi = mlibs::rhi;
namespace math = mlibs::math;
using math::Mat4;

struct Pass::Corner {
    float eye[3];
    float square[2];
    float colour[4];
};

namespace {

constexpr std::array<rhi::VertexAttribute, 3> attributes{rhi::VertexAttribute{0, rhi::VertexFormat::float3, 0}, rhi::VertexAttribute{1, rhi::VertexFormat::float2, 12},
                                                         rhi::VertexAttribute{2, rhi::VertexFormat::float4, 20}};
struct Row {
    float x = 0, y = 0, z = 0, w = 0;
};
Row row_of(const Mat4& matrix, int row) noexcept {
    return {matrix.at(0, static_cast<std::size_t>(row)), matrix.at(1, static_cast<std::size_t>(row)), matrix.at(2, static_cast<std::size_t>(row)), matrix.at(3, static_cast<std::size_t>(row))};
}

}  // namespace

rhi::ShaderFormats Pass::shader_formats() noexcept { return shaders::particle::particle_vs.formats(); }

rhi::Result<Pass> Pass::make(rhi::Device& device) {
    const std::array<rhi::VertexBufferLayout, 1> layout{rhi::VertexBufferLayout{sizeof(Corner), false, attributes}};
    const auto made = [&](rhi::Blend blend, const char* label) {
        return device.create(rhi::GraphicsPipelineDesc{.vertex = &shaders::particle::particle_vs,
                                                       .fragment = &shaders::particle::particle_fs,
                                                       .vertex_buffers = layout,
                                                       .target = rhi::Format::rgba16_float,
                                                       .blend = blend,
                                                       .label = label});
    };
    auto added = made(rhi::Blend::additive(), "xrender.particle.added");
    if (!added) return std::unexpected(added.error());
    auto laid = made(rhi::Blend::premultiplied(), "xrender.particle.laid");
    if (!laid) {
        device.destroy(*added);
        return std::unexpected(laid.error());
    }
    auto nearest = device.create(rhi::SamplerDesc{rhi::Filter::nearest, rhi::Filter::nearest});
    if (!nearest) {
        device.destroy(*added), device.destroy(*laid);
        return std::unexpected(nearest.error());
    }
    Pass pass;
    pass.device_ = &device, pass.added_ = *added, pass.laid_ = *laid, pass.nearest_ = *nearest;
    return pass;
}

Pass::Pass(Pass&& other) noexcept = default;
Pass& Pass::operator=(Pass&& other) noexcept {
    if (this != &other) {
        std::destroy_at(this);
        std::construct_at(this, std::move(other));
    }
    return *this;
}
Pass::~Pass() { release(); }
void Pass::release() noexcept {
    if (!device_) return;
    static_cast<void>(device_->wait_idle());
    if (added_) device_->destroy(added_);
    if (laid_) device_->destroy(laid_);
    if (nearest_) device_->destroy(nearest_);
    if (corners_) device_->destroy(corners_);
    device_ = nullptr;
}

rhi::Result<void> Pass::record(rhi::CommandList& list, std::span<const Batch> batches, const scene::View& view, rhi::Texture light, rhi::Texture surface, std::uint32_t width,
                               std::uint32_t height) {
    last_ = {};
    // The reference's order: every particle in front of the eye, the farthest first; at one distance, in the
    // batches' order.
    struct Placed {
        math::Vec3    middle;
        float         distance = 0;
        std::uint32_t batch = 0, index = 0;
    };
    std::vector<Placed> placed;
    for (std::uint32_t batch = 0; batch < batches.size(); ++batch)
        for (std::uint32_t index = 0; index < batches[batch].particles.size(); ++index) {
            ++last_.particles;
            const Particle& one = batches[batch].particles[index];
            const math::Vec4 middle = view.view * math::Vec4{one.at, 1};
            if (!(middle.z < 0) || !(one.size > 0)) {
                ++last_.outside;
                continue;
            }
            if (batches[batch].sprite) {
                ++last_.pictured;
                continue;
            }
            placed.push_back({{middle.x, middle.y, middle.z}, -middle.z, batch, index});
        }
    std::ranges::stable_sort(placed, [](const Placed& one, const Placed& other) { return one.distance > other.distance; });
    laid_out_.clear(), runs_.clear();
    for (const Placed& one : placed) {
        const Particle& particle = batches[one.batch].particles[one.index];
        const float half = particle.size * 0.5f, cos_turn = math::det::cos(particle.turn), sin_turn = math::det::sin(particle.turn);
        const auto corner = [&](float across, float up) {
            const float right = (across * cos_turn - up * sin_turn) * half, above = (across * sin_turn + up * cos_turn) * half;
            return Corner{{one.middle.x + right, one.middle.y + above, one.middle.z}, {across, up}, {particle.colour.x, particle.colour.y, particle.colour.z, particle.colour.w}};
        };
        const Corner low_left = corner(-1, -1), low_right = corner(1, -1), high_right = corner(1, 1), high_left = corner(-1, 1);
        if (runs_.empty() || runs_.back().batch != one.batch) runs_.push_back({one.batch, static_cast<std::uint32_t>(laid_out_.size()), 0});
        for (const Corner& made : {low_left, low_right, high_right, low_left, high_right, high_left}) laid_out_.push_back(made);
        runs_.back().count += 6;
    }
    last_.runs = static_cast<std::uint32_t>(runs_.size());
    if (laid_out_.empty()) return {};
    if (laid_out_.size() > room_) {
        if (corners_) {
            static_cast<void>(device_->wait_idle());
            device_->destroy(corners_);
            corners_ = {};
        }
        const auto room = static_cast<std::uint32_t>(std::bit_ceil(laid_out_.size()));
        auto buffer = device_->create(rhi::BufferDesc{static_cast<std::uint32_t>(room * sizeof(Corner)), rhi::BufferUsage::vertex, "xrender.particle.corners"});
        if (!buffer) return std::unexpected(buffer.error());
        corners_ = *buffer, room_ = room;
    }
    list.upload(corners_, 0, std::span<const Corner>(laid_out_));
    struct ViewUniforms {
        Row projection[4];
    } seen{};
    for (int row = 0; row < 4; ++row) seen.projection[row] = row_of(view.projection, row);
    list.begin_render({light, rhi::LoadOp::load});
    list.set_viewport(0, 0, static_cast<float>(width), static_cast<float>(height));
    for (const Run& run : runs_) {
        const Batch& batch = batches[run.batch];
        const bool over = batch.blend == Blend::over;
        list.set_pipeline(over ? laid_ : added_);
        list.push_uniforms(rhi::Stage::vertex, 0, seen);
        list.push_uniforms(rhi::Stage::fragment, 0, Row{static_cast<float>(width), static_cast<float>(height), batch.soft, over ? 1.0f : 0.0f});
        list.bind_texture(rhi::Stage::fragment, 0, surface, nearest_);
        list.bind_vertex_buffer(0, corners_);
        list.draw(run.count, 1, run.first);
    }
    list.end_render();
    return {};
}

}  // namespace xrender::particle::gpu
