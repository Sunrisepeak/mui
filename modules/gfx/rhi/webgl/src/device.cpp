// The device: resources remembered by description (what validation reads),
// each made in the page under its slot index; a submission encoded as rhi's
// wire words and handed to the page in one call. What WebGL2 cannot do is
// refused here, before the page sees it.
module openmui.rhi.webgl;

import std;
import openmui.memory;
import openmui.rhi;
import :page;

namespace omui::rhi::webgl {

namespace {

bool live = false;  // one device in a page at a time: the page's tables are its

class WebGlDevice final : public Device {
public:
    WebGlDevice(Caps caps, page::Context context, bool canvas) : caps_(std::move(caps)), context_(std::move(context)), canvas_(canvas) {
        live = true;
    }

    ~WebGlDevice() override {
        page::close();  // the context and everything made in it
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
    void destroy(ComputePipeline) noexcept override {}  // none are made

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
    std::optional<ComputePipelineInfo> describe(ComputePipeline) const noexcept override { return std::nullopt; }

    // WebGL2 is synchronous: what a caller can see of a submission (a
    // readback) comes after it in GL's order. Nothing to wait for.
    Result<void> wait(Submission) override { return reported(); }
    Result<void> wait_idle() override { return reported(); }

protected:
    Result<Buffer> make(const BufferDesc& d) override {
        if (has(d.usage, BufferUsage::storage_read) || has(d.usage, BufferUsage::compute_read) || has(d.usage, BufferUsage::compute_write))
            return fail(Errc::unsupported, "WebGL2 has no storage buffers");
        if (has(d.usage, BufferUsage::index) && d.usage != BufferUsage::index)
            return fail(Errc::unsupported, "WebGL2 binds a buffer as indices or as anything else, never both");
        const Buffer b = buffers_.insert({d.size, d.usage, {}});
        if (!page::buffer(b.index, d.size, static_cast<std::uint32_t>(d.usage))) {
            buffers_.erase(b);
            return fail(Errc::backend, std::format("WebGL2: creating a buffer of {} bytes: {}", d.size, page::error()));
        }
        return b;
    }

    Result<Texture> make(const TextureDesc& d) override {
        if (has(d.usage, TextureUsage::storage_read) || has(d.usage, TextureUsage::compute_read) || has(d.usage, TextureUsage::compute_write))
            return fail(Errc::unsupported, "WebGL2 has no storage textures");
        if (has(d.usage, TextureUsage::color_target)) {
            const bool half = d.format == Format::rgba16_float, single = d.format == Format::r32_float;
            if ((half && !context_.color_half_float) || (single && !context_.color_float))
                return fail(Errc::unsupported, std::format("{} renders to no {} target (EXT_color_buffer_float)", caps_.device, name(d.format)));
        }
        const Texture t = textures_.insert({d.format, d.width, d.height, d.usage, {}, d.samples});
        if (!page::texture(t.index, static_cast<std::uint32_t>(d.format), d.width, d.height, d.samples)) {
            textures_.erase(t);
            return fail(Errc::backend, std::format("WebGL2: creating a {}×{} {} texture: {}", d.width, d.height, name(d.format), page::error()));
        }
        return t;
    }

    Result<Sampler> make(const SamplerDesc& d) override {
        const Sampler s = samplers_.insert(d);
        if (!page::sampler(s.index, static_cast<std::uint32_t>(d.min), static_cast<std::uint32_t>(d.mag), static_cast<std::uint32_t>(d.u),
                           static_cast<std::uint32_t>(d.v))) {
            samplers_.erase(s);
            return fail(Errc::backend, std::format("WebGL2: creating a sampler: {}", page::error()));
        }
        return s;
    }

    Result<GraphicsPipeline> make(const GraphicsPipelineDesc& d) override {
        for (const ShaderDesc* s : {d.vertex, d.fragment})
            if (s->storage_textures || s->storage_buffers)
                return fail(Errc::unsupported, std::format("`{}` reads storage, which WebGL2 has not", s->entry));
        const ShaderCode* vs = d.vertex->code_for(ShaderFormat::essl);
        const ShaderCode* fs = d.fragment->code_for(ShaderFormat::essl);
        words_.clear();
        wire::encode(d, words_);
        const GraphicsPipeline p = graphics_.insert(
            {counts(*d.vertex), counts(*d.fragment), static_cast<std::uint32_t>(d.vertex_buffers.size()), d.target, d.depth_target, d.samples});
        const std::string_view label = d.label.empty() ? d.fragment->entry : d.label;
        if (!page::graphics(p.index, words_, vs->bytes, fs->bytes, label)) {
            graphics_.erase(p);
            return fail(Errc::backend, std::format("WebGL2: creating the pipeline {}: {}", label, page::error()));
        }
        return p;
    }

    Result<ComputePipeline> make(const ComputePipelineDesc&) override {
        return fail(Errc::unsupported, "WebGL2 has no compute shaders");  // Device::create says so first (Caps::compute)
    }

    Result<Submission> run(const CommandList& list) override {
        if (auto e = reported(); !e) return std::unexpected(e.error());
        words_.clear();
        wire::encode(list, words_);
        if (!page::run(words_)) return fail(page::lost() ? Errc::device_lost : Errc::backend, std::format("WebGL2: {}", page::error()));
        return Submission{next_++};
    }

    Result<void> fetch(Texture t, Region region, std::span<std::byte> out) override {
        const TextureDesc& d = *textures_.get(t);
        if (is_depth(d.format)) return fail(Errc::unsupported, "WebGL2 reads no depth back");
        if (d.samples > 1) return fail(Errc::unsupported, "a multisampled target is read through the texture it resolves into");
        if ((d.format == Format::rgba16_float || d.format == Format::r32_float) && !context_.color_float)
            return fail(Errc::unsupported, std::format("{} reads no {} back (EXT_color_buffer_float)", caps_.device, name(d.format)));
        if (!page::read(t.index, region.x, region.y, region.width, region.height, out))
            return fail(Errc::backend, std::format("WebGL2: reading the texture back: {}", page::error()));
        return {};
    }

    Result<void> fetch(Buffer b, std::uint32_t offset, std::span<std::byte> out) override {
        if (!page::read(b.index, offset, out)) return fail(Errc::backend, std::format("WebGL2: reading the buffer back: {}", page::error()));
        return {};
    }

    Result<bool> show(Texture frame) override {
        if (!canvas_) return fail(Errc::unsupported, "present: the device was made for no canvas (rhi::webgl::Options::canvas)");
        if (auto e = reported(); !e) return std::unexpected(e.error());
        const int r = page::present(frame.index);
        if (r < 0) return fail(Errc::backend, std::format("WebGL2: presenting: {}", page::error()));
        return r == 1;
    }

private:
    static Result<void> reported() {
        std::string e = page::error();
        if (e.empty()) return {};
        return fail(page::lost() ? Errc::device_lost : Errc::backend, std::format("WebGL2: {}", e));
    }

    Caps                                               caps_;
    page::Context                                      context_;
    bool                                               canvas_ = false;
    SlotMap<BufferDesc, BufferTag>                     buffers_;
    SlotMap<TextureDesc, TextureTag>                   textures_;
    SlotMap<SamplerDesc, SamplerTag>                   samplers_;
    SlotMap<GraphicsPipelineInfo, GraphicsPipelineTag> graphics_;
    std::vector<std::uint32_t>                         words_;
    std::uint64_t                                      next_ = 1;
};

}  // namespace

Result<std::unique_ptr<Device>> create(const Options& options) {
    if (live) return fail(Errc::unsupported, "a WebGL2 device of this page's is alive: one at a time");
    auto context = page::open(options.canvas);
    if (!context) return fail(Errc::unsupported, std::move(context.error()));
    Caps caps;
    caps.backend = Backend::webgl;
    caps.device = context->name;
    caps.formats = formats({ShaderFormat::essl});
    caps.software = context->software;
    caps.compute = false;
    caps.max_texture_size = context->max_texture;
    const std::uint32_t n = context->max_samples;  // rhi's counts: 1, 2, 4, 8
    caps.max_samples = n >= 8 ? 8 : n >= 4 ? 4 : n >= 2 ? 2 : 1;
    caps.depth = true;  // DEPTH_COMPONENT32F and DEPTH32F_STENCIL8 are WebGL2's own
    return std::unique_ptr<Device>(std::make_unique<WebGlDevice>(std::move(caps), std::move(*context), !options.canvas.empty()));
}

}  // namespace omui::rhi::webgl
