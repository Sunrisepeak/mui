// openmui.rhi:device — what a backend implements (architecture §8.2).
//
// A Device creates resources from descriptions and runs command lists. Its
// virtual functions are per resource and per submission, never per draw. The
// public entry points check before the backend sees anything: descriptions
// (`check`), every command of a list against the resources it names
// (`validate`), readback sizes — so a backend translates only well-formed
// work, and a mistake is an Errc::invalid naming the command, not a driver
// crash.
export module openmui.rhi:device;

import std;
import :types;
import :commands;

export namespace omui::rhi {

class Device {
public:
    Device() = default;
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;
    virtual ~Device() = default;

    [[nodiscard]] virtual const Caps& caps() const noexcept = 0;

    [[nodiscard]] Result<Buffer> create(const BufferDesc& d);
    [[nodiscard]] Result<Texture> create(const TextureDesc& d);
    [[nodiscard]] Result<Sampler> create(const SamplerDesc& d) { return make(d); }
    [[nodiscard]] Result<GraphicsPipeline> create(const GraphicsPipelineDesc& d);
    [[nodiscard]] Result<ComputePipeline> create(const ComputePipelineDesc& d);

    // Destroying what a submission still uses is safe: the backend releases
    // it when the GPU is done with it. A stale handle is ignored.
    virtual void destroy(Buffer b) noexcept = 0;
    virtual void destroy(Texture t) noexcept = 0;
    virtual void destroy(Sampler s) noexcept = 0;
    virtual void destroy(GraphicsPipeline p) noexcept = 0;
    virtual void destroy(ComputePipeline p) noexcept = 0;

    // What a live handle was created as (nullopt: stale or never made). Labels
    // are not kept.
    [[nodiscard]] virtual std::optional<BufferDesc> describe(Buffer b) const noexcept = 0;
    [[nodiscard]] virtual std::optional<TextureDesc> describe(Texture t) const noexcept = 0;
    [[nodiscard]] virtual bool exists(Sampler s) const noexcept = 0;
    [[nodiscard]] virtual std::optional<GraphicsPipelineInfo> describe(GraphicsPipeline p) const noexcept = 0;
    [[nodiscard]] virtual std::optional<ComputePipelineInfo> describe(ComputePipeline p) const noexcept = 0;

    // Validates the list, then runs it; returns at once (wait() for it).
    [[nodiscard]] Result<Submission> submit(const CommandList& list);
    [[nodiscard]] virtual Result<void> wait(Submission s) = 0;
    [[nodiscard]] virtual Result<void> wait_idle() = 0;

    // The texels of `region` (tightly packed rows) or the bytes of a buffer,
    // once every submission so far has finished writing them.
    [[nodiscard]] Result<void> read(Texture t, Region region, std::span<std::byte> out);
    [[nodiscard]] Result<std::vector<std::byte>> read(Texture t, Region region);
    [[nodiscard]] Result<void> read(Buffer b, std::uint32_t offset, std::span<std::byte> out);

    // The window the device was made for (a backend's option) shows `frame`:
    // an rgba8 or bgra8 texture the device can sample, scaled to the window's
    // swapchain, after every submission so far. False: nothing shown (the
    // window is minimised, its swapchain not ready). Errc::unsupported: the
    // device was made for no window.
    [[nodiscard]] Result<bool> present(Texture frame);

protected:
    // A description already checked.
    virtual Result<Buffer> make(const BufferDesc& d) = 0;
    virtual Result<Texture> make(const TextureDesc& d) = 0;
    virtual Result<Sampler> make(const SamplerDesc& d) = 0;
    virtual Result<GraphicsPipeline> make(const GraphicsPipelineDesc& d) = 0;
    virtual Result<ComputePipeline> make(const ComputePipelineDesc& d) = 0;
    // A list already validated.
    virtual Result<Submission> run(const CommandList& list) = 0;
    // A region inside the texture, `out` exactly its size; a range inside the buffer.
    virtual Result<void> fetch(Texture t, Region region, std::span<std::byte> out) = 0;
    virtual Result<void> fetch(Buffer b, std::uint32_t offset, std::span<std::byte> out) = 0;
    // A frame already checked; a device made for no window keeps this one.
    virtual Result<bool> show(Texture frame);
};

// Every command of `list` against `device`'s resources: passes balanced and
// not nested, each command in the kind of pass it belongs to, handles live,
// usages declared, regions and ranges inside their resources, slots inside the
// bound pipeline's shaders' counts, a pipeline set before a draw or a
// dispatch, its vertex buffers bound. "" when it is right; otherwise the
// first problem, with the command's index and kind.
[[nodiscard]] std::string validate(const CommandList& list, const Device& device);

}  // namespace omui::rhi
