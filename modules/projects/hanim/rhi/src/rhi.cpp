module hanim.rhi.api;

import std;
import mlibs.trace;
import :shape_shaders;

namespace hanim::gpu {
namespace rhi = mlibs::rhi;

rhi::ShaderFormats shader_formats() {
    return shaders::shape::interpolate.formats();
}

ShapeBatch::~ShapeBatch() {
    if (from_)
        device_.destroy(from_);
    if (to_)
        device_.destroy(to_);
    if (pipeline_)
        device_.destroy(pipeline_);
}

rhi::Result<std::unique_ptr<ShapeBatch>>
ShapeBatch::prepare(rhi::Device& device, std::span<const PreparedShapeMorph> plans) {
    mlibs::trace::Span span("shape.gpu.prepare", "hanim.rhi");
    const auto refuse = [](rhi::Errc code, std::string message) {
        return std::unexpected(rhi::Error{code, std::move(message)});
    };
    if (plans.empty())
        return refuse(rhi::Errc::invalid, "an empty shape batch");
    if (!device.caps().compute)
        return refuse(rhi::Errc::unsupported, "shape batches need compute");
    const auto path = plans.front().point_path();
    if (path.noise.amplitude != 0)
        return refuse(rhi::Errc::unsupported,
                      "path noise is evaluated on the CPU (its hash needs 64-bit integers)");
    std::vector<float> from, to;
    auto batch = std::unique_ptr<ShapeBatch>(new ShapeBatch(device));
    batch->path_ = path;
    for (const auto& plan : plans) {
        if (!(plan.point_path() == path))
            return refuse(rhi::Errc::invalid, "a batch's plans share one point path");
        if (!plan.segment_count())
            return refuse(rhi::Errc::invalid, "an unprepared shape plan");
        batch->first_.push_back(batch->segments_);
        batch->segments_ += plan.segment_count();
        const auto append = [](std::span<const ShapeCubic> cubics, std::vector<float>& out) {
            for (const auto& cubic : cubics)
                for (const auto point : {cubic.start, cubic.control1, cubic.control2, cubic.end}) {
                    out.push_back(static_cast<float>(point.x));
                    out.push_back(static_cast<float>(point.y));
                }
        };
        append(plan.from_cubics(), from);
        append(plan.to_cubics(), to);
    }
    if (from.size() * sizeof(float) > std::numeric_limits<std::uint32_t>::max())
        return refuse(rhi::Errc::invalid, "a shape batch too large for one buffer");
    auto pipeline =
        device.create(rhi::ComputePipelineDesc{&shaders::shape::interpolate, "hanim.shape.sample"});
    if (!pipeline)
        return std::unexpected(pipeline.error());
    batch->pipeline_ = *pipeline;
    const rhi::BufferDesc desc{.size = static_cast<std::uint32_t>(from.size() * sizeof(float)),
                               .usage = rhi::BufferUsage::compute_read,
                               .label = "hanim.shape.ends"};
    auto from_buffer = device.create(desc);
    if (!from_buffer)
        return std::unexpected(from_buffer.error());
    batch->from_ = *from_buffer;
    auto to_buffer = device.create(desc);
    if (!to_buffer)
        return std::unexpected(to_buffer.error());
    batch->to_ = *to_buffer;
    rhi::CommandList upload;
    upload.upload(batch->from_, 0, std::span<const float>(from));
    upload.upload(batch->to_, 0, std::span<const float>(to));
    auto submitted = device.submit(upload);
    if (!submitted)
        return std::unexpected(submitted.error());
    batch->ready_ = *submitted;
    return batch;
}

rhi::Result<void> ShapeBatch::record(rhi::CommandList& commands, double progress,
                                     rhi::Buffer output) const {
    mlibs::trace::Span span("shape.gpu.record", "hanim.rhi");
    const auto refuse = [](std::string message) {
        return std::unexpected(rhi::Error{rhi::Errc::invalid, std::move(message)});
    };
    if (!std::isfinite(progress))
        return refuse("a nonfinite progress");
    const auto described = device_.describe(output);
    if (!described || described->size < floats() * sizeof(float) ||
        !rhi::has(described->usage, rhi::BufferUsage::compute_write))
        return refuse("the output buffer is too small or not compute-writable");
    struct Params {
        std::uint32_t points;
        float progress, control1_x, control1_y, control2_x, control2_y;
        std::uint32_t curved, pad;
    };
    const Params params{static_cast<std::uint32_t>(segments_ * 4),
                        static_cast<float>(std::clamp(progress, 0.0, 1.0)),
                        static_cast<float>(path_.control1.x),
                        static_cast<float>(path_.control1.y),
                        static_cast<float>(path_.control2.x),
                        static_cast<float>(path_.control2.y),
                        path_.linear() ? 0u : 1u,
                        0u};
    const std::array writes{output};
    commands.begin_compute({}, writes);
    commands.set_pipeline(pipeline_);
    commands.bind_storage(rhi::Stage::compute, 0, from_);
    commands.bind_storage(rhi::Stage::compute, 1, to_);
    commands.push_uniforms(rhi::Stage::compute, 0, params);
    commands.dispatch(static_cast<std::uint32_t>((segments_ * 4 + 63) / 64), 1);
    commands.end_compute();
    return {};
}

}  // namespace hanim::gpu
