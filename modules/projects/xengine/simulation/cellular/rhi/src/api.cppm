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
}
