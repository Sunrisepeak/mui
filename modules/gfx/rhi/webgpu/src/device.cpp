// The device: resources remembered by description (what validation reads),
// each made in the page under its slot index; a submission encoded as rhi's
// wire words and handed to the page in one call.
module openmui.rhi.webgpu;

import std;
import openmui.memory;
import openmui.rhi;
import :page;

namespace omui::rhi::webgpu {

namespace {

constexpr std::uint32_t none = wire::none;

// A graphics pipeline for the page (src/js/omui_webgpu.js reads the same
// positions): the stages' counts, the target and its blend, the topology,
// the depth, the samples, then the vertex buffers' layouts.
//
//   0–3   vertex: samplers, storage textures, storage buffers, uniform buffers
//   4–7   fragment: the same
//   8     target format          9–15  blend: enabled, src/dst colour, op, src/dst alpha, op
//   16    topology               17    depth format (none: no depth)
//   18–20 depth test, write, compare
//   21    samples                22    buffers, then each: stride, per instance, attributes, then each: location, format, offset
void graphics_words(const GraphicsPipelineDesc& d, std::vector<std::uint32_t>& w) {
    auto counts = [&](const ShaderDesc& s) { w.insert(w.end(), {s.samplers, s.storage_textures, s.storage_buffers, s.uniform_buffers}); };
    auto u = [](auto e) { return static_cast<std::uint32_t>(e); };
    counts(*d.vertex);
    counts(*d.fragment);
    w.insert(w.end(), {u(d.target), d.blend.enabled ? 1u : 0u, u(d.blend.src_color), u(d.blend.dst_color), u(d.blend.color_op),
                       u(d.blend.src_alpha), u(d.blend.dst_alpha), u(d.blend.alpha_op)});
    w.insert(w.end(), {u(d.topology), d.depth_target ? u(*d.depth_target) : none, d.depth.test ? 1u : 0u, d.depth.write ? 1u : 0u,
                       u(d.depth.compare), d.samples});
    w.push_back(static_cast<std::uint32_t>(d.vertex_buffers.size()));
    for (const VertexBufferLayout& b : d.vertex_buffers) {
        w.insert(w.end(), {b.stride, b.per_instance ? 1u : 0u, static_cast<std::uint32_t>(b.attributes.size())});
        for (const VertexAttribute& a : b.attributes) w.insert(w.end(), {a.location, u(a.format), a.offset});
    }
}

// A compute pipeline's counts: samplers, storage textures, storage buffers,
// uniform buffers, read-write textures, read-write buffers.
void compute_words(const ShaderDesc& s, std::vector<std::uint32_t>& w) {
    w.insert(w.end(), {s.samplers, s.storage_textures, s.storage_buffers, s.uniform_buffers, s.rw_storage_textures, s.rw_storage_buffers});
}

bool live = false;  // one device in a page at a time: the page's tables are its

class WebGpuDevice final : public Device {
public:
    WebGpuDevice(Caps caps, bool canvas) : caps_(std::move(caps)), canvas_(canvas) { live = true; }

    ~WebGpuDevice() override {
        buffers_.for_each([](Buffer b, BufferDesc&) { page::destroy(page::Kind::buffer, b.index); });
        textures_.for_each([](Texture t, TextureDesc&) { page::destroy(page::Kind::texture, t.index); });
        samplers_.for_each([](Sampler s, SamplerDesc&) { page::destroy(page::Kind::sampler, s.index); });
        graphics_.for_each([](GraphicsPipeline p, GraphicsPipelineInfo&) { page::destroy(page::Kind::graphics, p.index); });
        compute_.for_each([](ComputePipeline p, ComputePipelineInfo&) { page::destroy(page::Kind::compute, p.index); });
        live = false;
    }

    const Caps& caps() const noexcept override { return caps_; }

    void destroy(Buffer b) noexcept override {
        if (buffers_.contains(b)) page::destroy(page::Kind::buffer, b.index), buffers_.erase(b);
    }
    void destroy(Texture t) noexcept override {
        if (textures_.contains(t)) page::destroy(page::Kind::texture, t.index), textures_.erase(t);
    }
    void destroy(Sampler s) noexcept override {
        if (samplers_.contains(s)) page::destroy(page::Kind::sampler, s.index), samplers_.erase(s);
    }
    void destroy(GraphicsPipeline p) noexcept override {
        if (graphics_.contains(p)) page::destroy(page::Kind::graphics, p.index), graphics_.erase(p);
    }
    void destroy(ComputePipeline p) noexcept override {
        if (compute_.contains(p)) page::destroy(page::Kind::compute, p.index), compute_.erase(p);
    }

    std::optional<BufferDesc> describe(Buffer b) const noexcept override {
        const auto* d = buffers_.get(b);
        return d ? std::optional(*d) : std::nullopt;
    }
    std::optional<TextureDesc> describe(Texture t) const noexcept override {
        const auto* d = textures_.get(t);
        return d ? std::optional(*d) : std::nullopt;
    }
    bool exists(Sampler s) const noexcept override { return samplers_.contains(s); }
    std::optional<GraphicsPipelineInfo> describe(GraphicsPipeline p) const noexcept override {
        const auto* d = graphics_.get(p);
        return d ? std::optional(*d) : std::nullopt;
    }
    std::optional<ComputePipelineInfo> describe(ComputePipeline p) const noexcept override {
        const auto* d = compute_.get(p);
        return d ? std::optional(*d) : std::nullopt;
    }

    Result<void> wait(Submission) override { return wait_idle(); }

    Result<void> wait_idle() override {
        // Unable to wait (-1): the queue runs every submission in order, and
        // what each reads was copied when it was submitted.
        const int r = page::wait();
        if (auto e = reported(); !e) return e;
        if (r == 0) return fail(Errc::device_lost, "WebGPU: waiting for the queue failed");
        return {};
    }

protected:
    Result<Buffer> make(const BufferDesc& d) override {
        const Buffer b = buffers_.insert({d.size, d.usage, {}});
        if (!page::buffer(b.index, d.size, static_cast<std::uint32_t>(d.usage), d.label)) {
            buffers_.erase(b);
            return fail(Errc::backend, std::format("WebGPU: creating a buffer of {} bytes: {}", d.size, page::error()));
        }
        return b;
    }

    Result<Texture> make(const TextureDesc& d) override {
        if (d.samples != 1 && d.samples != 4)
            return fail(Errc::unsupported, std::format("WebGPU multisamples 4 samples a texel, not {}", d.samples));
        const Texture t = textures_.insert({d.format, d.width, d.height, d.usage, {}, d.samples});
        if (!page::texture(t.index, static_cast<std::uint32_t>(d.format), d.width, d.height, static_cast<std::uint32_t>(d.usage), d.samples, d.label)) {
            textures_.erase(t);
            return fail(Errc::backend, std::format("WebGPU: creating a {}×{} {} texture: {}", d.width, d.height, name(d.format), page::error()));
        }
        return t;
    }

    Result<Sampler> make(const SamplerDesc& d) override {
        const Sampler s = samplers_.insert(d);
        if (!page::sampler(s.index, static_cast<std::uint32_t>(d.min), static_cast<std::uint32_t>(d.mag), static_cast<std::uint32_t>(d.u),
                           static_cast<std::uint32_t>(d.v))) {
            samplers_.erase(s);
            return fail(Errc::backend, std::format("WebGPU: creating a sampler: {}", page::error()));
        }
        return s;
    }

    Result<GraphicsPipeline> make(const GraphicsPipelineDesc& d) override {
        if (d.samples != 1 && d.samples != 4)
            return fail(Errc::unsupported, std::format("WebGPU multisamples 4 samples a texel, not {}", d.samples));
        const ShaderCode* vs = d.vertex->code_for(ShaderFormat::wgsl);
        const ShaderCode* fs = d.fragment->code_for(ShaderFormat::wgsl);
        words_.clear();
        graphics_words(d, words_);
        const GraphicsPipeline p = graphics_.insert(
            {counts(*d.vertex), counts(*d.fragment), static_cast<std::uint32_t>(d.vertex_buffers.size()), d.target, d.depth_target, d.samples});
        const std::string_view label = d.label.empty() ? d.fragment->entry : d.label;
        if (!page::graphics(p.index, words_, vs->bytes, d.vertex->entry, fs->bytes, d.fragment->entry, label)) {
            graphics_.erase(p);
            return fail(Errc::backend, std::format("WebGPU: creating the pipeline {}: {}", label, page::error()));
        }
        return p;
    }

    Result<ComputePipeline> make(const ComputePipelineDesc& d) override {
        const ShaderCode* cs = d.shader->code_for(ShaderFormat::wgsl);
        words_.clear();
        compute_words(*d.shader, words_);
        const ComputePipeline p = compute_.insert({counts(*d.shader)});
        const std::string_view label = d.label.empty() ? d.shader->entry : d.label;
        if (!page::compute(p.index, words_, cs->bytes, d.shader->entry, label)) {
            compute_.erase(p);
            return fail(Errc::backend, std::format("WebGPU: creating the compute pipeline {}: {}", label, page::error()));
        }
        return p;
    }

    Result<Submission> run(const CommandList& list) override {
        // WebGPU writes buffers four bytes at a time: an upload starts on a
        // multiple of 4, and ends on one or at the buffer's end (its GPU
        // buffer's size was rounded up to one).
        const auto commands = list.commands();
        for (std::size_t i = 0; i < commands.size(); ++i)
            if (const auto* u = std::get_if<cmd::UploadBuffer>(&commands[i])) {
                const std::uint32_t size = buffers_.get(u->buffer)->size;
                const std::size_t end = std::size_t{u->offset} + u->data.size();
                if (u->offset % 4 != 0 || (u->data.size() % 4 != 0 && end != size))
                    return fail(Errc::unsupported, std::format("command {} (upload_buffer): WebGPU writes buffers 4 bytes at a time; bytes {}–{} of "
                                                               "a buffer of {} neither start and end on a multiple of 4 nor end the buffer",
                                                               i, u->offset, end, size));
            }
        if (auto e = reported(); !e) return std::unexpected(e.error());
        words_.clear();
        wire::encode(list, words_);
        if (!page::run(words_)) return fail(Errc::backend, std::format("WebGPU: {}", page::error()));
        return Submission{next_++};
    }

    Result<void> fetch(Texture t, Region region, std::span<std::byte> out) override {
        if (textures_.get(t)->format == Format::d32_float_s8)
            return fail(Errc::unsupported, "WebGPU reads a depth and stencil texture's depth alone (d32_float_s8 is read neither whole nor by rhi)");
        if (!page::suspends()) return cannot_wait();
        const int r = page::read(t.index, region.x, region.y, region.width, region.height, out);
        if (auto e = reported(); !e) return e;
        if (r != 1) return fail(Errc::backend, "WebGPU: reading the texture back failed");
        return {};
    }

    Result<void> fetch(Buffer b, std::uint32_t offset, std::span<std::byte> out) override {
        if (!page::suspends()) return cannot_wait();
        const int r = page::read(b.index, offset, out);
        if (auto e = reported(); !e) return e;
        if (r != 1) return fail(Errc::backend, "WebGPU: reading the buffer back failed");
        return {};
    }

    Result<bool> show(Texture frame) override {
        if (!canvas_) return fail(Errc::unsupported, "present: the device was made for no canvas (rhi::webgpu::Options::canvas)");
        if (auto e = reported(); !e) return std::unexpected(e.error());
        const int r = page::present(frame.index);
        if (r < 0) return fail(Errc::backend, std::format("WebGPU: presenting: {}", page::error()));
        return r == 1;
    }

private:
    // What WebGPU reported since the last call: asynchronous, so it belongs
    // to some earlier call; said by this one.
    static Result<void> reported() {
        std::string e = page::error();
        if (e.empty()) return {};
        return fail(page::lost() ? Errc::device_lost : Errc::backend, std::format("WebGPU: {}", e));
    }

    static Result<void> cannot_wait() {
        return fail(Errc::unsupported, "reading back from WebGPU waits for the GPU, and this program was not linked to wait (-sASYNCIFY or -sJSPI)");
    }

    Caps                                                   caps_;
    bool                                                   canvas_ = false;
    SlotMap<BufferDesc, BufferTag>                         buffers_;
    SlotMap<TextureDesc, TextureTag>                       textures_;
    SlotMap<SamplerDesc, SamplerTag>                       samplers_;
    SlotMap<GraphicsPipelineInfo, GraphicsPipelineTag>     graphics_;
    SlotMap<ComputePipelineInfo, ComputePipelineTag>       compute_;
    std::vector<std::uint32_t>                             words_;
    std::uint64_t                                          next_ = 1;
};

}  // namespace

Result<std::unique_ptr<Device>> create(const Options& options) {
    auto adapter = page::device();
    if (!adapter) return fail(Errc::unsupported, std::move(adapter.error()));
    if (live) return fail(Errc::unsupported, "a WebGPU device of this page's is alive: one at a time");
    if (!options.canvas.empty() && !page::canvas(options.canvas))
        return fail(Errc::unsupported, std::format("WebGPU: the canvas {}: {}", options.canvas, page::error()));
    Caps caps;
    caps.backend = Backend::webgpu;
    caps.device = adapter->name;
    caps.formats = formats({ShaderFormat::wgsl});
    caps.software = adapter->software;
    caps.compute = true;
    caps.max_texture_size = adapter->max_texture;
    caps.max_samples = 4;  // WebGPU's one count beside 1
    caps.depth = adapter->depth_stencil;
    return std::unique_ptr<Device>(std::make_unique<WebGpuDevice>(std::move(caps), !options.canvas.empty()));
}

bool available() { return page::device().has_value(); }

bool can_wait() { return page::suspends(); }

}  // namespace omui::rhi::webgpu
