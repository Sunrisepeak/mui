export module openmui.simulation.cellular.rhi;
import std;
import openmui.simulation.cellular;
import openmui.rhi;

export namespace omui::simulation::cellular::gpu {
// Borrowed device-local, contiguous channel-major field. Owner is explicit;
// neither resource lifetime nor state publication is transferred by this view.
struct Field {
    omui::rhi::Device* owner = nullptr;
    omui::rhi::Buffer buffer;
    Shape shape;
    std::uint32_t channels = 0;
};
[[nodiscard]] omui::rhi::ShaderFormats shader_formats();

class Kernel {
public:
    // Owns a copied CPU plan, a rule buffer and a compute pipeline. Device must
    // outlive Kernel. Upload is submitted once; queue order makes later records
    // see it, and ready() can be waited on explicitly. No state buffers owned.
    [[nodiscard]] static omui::rhi::Result<std::unique_ptr<Kernel>> prepare(omui::rhi::Device&,const Program&);
    ~Kernel();
    Kernel(const Kernel&)=delete;
    Kernel& operator=(const Kernel&)=delete;
    [[nodiscard]] const Program& program() const noexcept { return program_; }
    [[nodiscard]] omui::rhi::Submission ready() const noexcept { return ready_; }
    // Appends one full-field compute pass to the caller's shared command list.
    // No upload, submission, wait, readback or clock advancement here. Inputs
    // and outputs are disjoint on this device. Failed validation appends nothing.
    [[nodiscard]] omui::rhi::Result<void> record(omui::rhi::CommandList&,Field input,Field output,Boundary = {}) const;
private:
    explicit Kernel(omui::rhi::Device& d,const Program& p):device_(d),program_(p){}
    omui::rhi::Device& device_;
    Program program_;
    omui::rhi::Buffer rules_;
    omui::rhi::ComputePipeline pipeline_;
    omui::rhi::Submission ready_;
};
}
