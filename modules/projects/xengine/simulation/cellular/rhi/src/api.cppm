export module xengine.simulation.cellular.rhi.api;
import std;
import xengine.simulation.cellular;
import mlibs.rhi;

export namespace xengine::simulation::cellular::gpu {
// Borrowed device-local, contiguous channel-major field. Owner is explicit;
// neither resource lifetime nor state publication is transferred by this view.
struct Field {
    mlibs::rhi::Device* owner = nullptr;
    mlibs::rhi::Buffer buffer;
    Shape shape;
    std::uint32_t channels = 0;
};
[[nodiscard]] mlibs::rhi::ShaderFormats shader_formats();

class Kernel {
public:
    // Owns a copied CPU plan, a rule buffer and a compute pipeline. Device must
    // outlive Kernel. Upload is submitted once; queue order makes later records
    // see it, and ready() can be waited on explicitly. No state buffers owned.
    [[nodiscard]] static mlibs::rhi::Result<std::unique_ptr<Kernel>> prepare(mlibs::rhi::Device&,const Program&);
    ~Kernel();
    Kernel(const Kernel&)=delete;
    Kernel& operator=(const Kernel&)=delete;
    [[nodiscard]] const Program& program() const noexcept { return program_; }
    [[nodiscard]] mlibs::rhi::Submission ready() const noexcept { return ready_; }
    // Appends one full-field compute pass to the caller's shared command list.
    // No upload, submission, wait, readback or clock advancement here. Inputs
    // and outputs are disjoint on this device. Failed validation appends nothing.
    [[nodiscard]] mlibs::rhi::Result<void> record(mlibs::rhi::CommandList&,Field input,Field output,Boundary = {}) const;
private:
    explicit Kernel(mlibs::rhi::Device& d,const Program& p):device_(d),program_(p){}
    mlibs::rhi::Device& device_;
    Program program_;
    mlibs::rhi::Buffer rules_;
    mlibs::rhi::ComputePipeline pipeline_;
    mlibs::rhi::Submission ready_;
};

// A field's colours on the device (D-132): each cell's pixel by the first
// swatch it passes (pixel_of), written into an rgba8 texture the renderer
// draws as an external texture — the field is never read back.
class Painter {
public:
    // Its palette (pixels as cellular.present makes them) uploaded once, for
    // fields of `channels`. Device must outlive Painter.
    [[nodiscard]] static mlibs::rhi::Result<std::unique_ptr<Painter>> prepare(mlibs::rhi::Device&, std::span<const PixelSwatch> swatches,
                                                                               std::uint32_t fallback, std::uint32_t channels);
    ~Painter();
    Painter(const Painter&)=delete;
    Painter& operator=(const Painter&)=delete;
    [[nodiscard]] mlibs::rhi::Submission ready() const noexcept { return ready_; }
    // Appends one compute pass writing `cells`' pixels into `image` (rgba8,
    // the field's size, compute_write). Failed validation appends nothing.
    [[nodiscard]] mlibs::rhi::Result<void> record(mlibs::rhi::CommandList&, Field cells, mlibs::rhi::Texture image) const;
private:
    explicit Painter(mlibs::rhi::Device& d,std::uint32_t channels):device_(d),channels_(channels){}
    mlibs::rhi::Device& device_;
    std::uint32_t channels_ = 0;
    mlibs::rhi::Buffer palette_;
    mlibs::rhi::ComputePipeline pipeline_;
    mlibs::rhi::Submission ready_;
};
}
