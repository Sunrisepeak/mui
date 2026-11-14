// hanim.rhi — many prepared shape morphs sampled at once on an mlibs.rhi
// device (convergence plan §6.8 ⑧): the plans hanim.core prepares, their two
// ends' cubics uploaded once, every control point interpolated by one compute
// dispatch per sample. The same semantics as PreparedShapeMorph::sample in
// 32-bit floats (error: about 1e-6 of the coordinates' magnitude, CPU reference
// in double), path noise included: mlibs.math's noise lattice is 32-bit
// integer arithmetic (noise_key), so the shader computes the same field.
//
//     auto batch = hanim::gpu::ShapeBatch::prepare(device, plans);
//     (*batch)->record(commands, 0.5, output);   // output: (*batch)->floats() floats, x then y per
//     point
export module hanim.rhi.api;

import std;
import hanim.core;
import mlibs.rhi;

export namespace hanim::gpu {

[[nodiscard]] mlibs::rhi::ShaderFormats shader_formats();

class ShapeBatch {
public:
    // Copies every plan's two ends (as floats) into device buffers, submitted
    // once (ready()). All plans share one point path (the batch's). Refuses
    // an empty batch, noise, and plans whose point paths differ.
    [[nodiscard]] static mlibs::rhi::Result<std::unique_ptr<ShapeBatch>>
    prepare(mlibs::rhi::Device& device, std::span<const PreparedShapeMorph> plans);
    ~ShapeBatch();
    ShapeBatch(const ShapeBatch&) = delete;
    ShapeBatch& operator=(const ShapeBatch&) = delete;
    // Segments in plan order; each plan's first segment index.
    [[nodiscard]] std::size_t segments() const noexcept {
        return segments_;
    }
    [[nodiscard]] std::span<const std::size_t> first_segments() const noexcept {
        return first_;
    }
    // Floats an output buffer holds: segments × 4 points × (x, y).
    [[nodiscard]] std::size_t floats() const noexcept {
        return segments_ * 8;
    }
    [[nodiscard]] mlibs::rhi::Submission ready() const noexcept {
        return ready_;
    }
    // Appends one dispatch sampling every plan at `progress` (clamped to
    // [0, 1]; nonfinite refused) into `output` (compute-writable, floats()
    // floats). No submission or readback; a refused record appends nothing.
    [[nodiscard]] mlibs::rhi::Result<void> record(mlibs::rhi::CommandList& commands,
                                                  double progress, mlibs::rhi::Buffer output) const;
private:
    explicit ShapeBatch(mlibs::rhi::Device& device) : device_(device) {}
    mlibs::rhi::Device& device_;
    mlibs::rhi::Buffer from_, to_;
    mlibs::rhi::ComputePipeline pipeline_;
    mlibs::rhi::Submission ready_;
    PointPath path_;
    std::size_t segments_ = 0;
    std::vector<std::size_t> first_;
};

}  // namespace hanim::gpu
