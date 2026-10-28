// SDL_GPU for openmui.rhi.sdl: the one unit that uses SDL (imported as a
// module, mcpplibs.mwrap.sdl3). rhi's
// descriptions become SDL's create-infos, a validated CommandList becomes one
// SDL command buffer.
module openmui.rhi.sdl;

import std;
import mcpplibs.mwrap.sdl3;
import openmui.memory;
import openmui.rhi;
using namespace mcpplibs::mwrap::sdl3::macros;

namespace omui::rhi::sdl {

namespace {

std::string sdl_error(std::string_view what) {
    const char* e = SDL_GetError();
    return std::format("{}: {}", what, e && *e ? e : "SDL gave no reason");
}

// ── vocabulary ───────────────────────────────────────────────────────────

constexpr std::array<std::pair<ShaderFormat, SDL_GPUShaderFormat>, shader_format_count> shader_formats{{
    {ShaderFormat::spirv, SDL_GPU_SHADERFORMAT_SPIRV},
    {ShaderFormat::msl, SDL_GPU_SHADERFORMAT_MSL},
    {ShaderFormat::metallib, SDL_GPU_SHADERFORMAT_METALLIB},
    {ShaderFormat::dxbc, SDL_GPU_SHADERFORMAT_DXBC},
    {ShaderFormat::dxil, SDL_GPU_SHADERFORMAT_DXIL},
}};

SDL_GPUShaderFormat sdl_format(ShaderFormat f) {
    for (const auto& [ours, theirs] : shader_formats)
        if (ours == f) return theirs;
    return SDL_GPU_SHADERFORMAT_INVALID;
}

ShaderFormats ours(SDL_GPUShaderFormat bits) {
    ShaderFormats s;
    for (const auto& [o, theirs] : shader_formats)
        if (bits & theirs) s.add(o);
    return s;
}

std::optional<Backend> backend_of(std::string_view driver) {
    if (driver == "vulkan") return Backend::vulkan;
    if (driver == "metal") return Backend::metal;
    if (driver == "direct3d12") return Backend::d3d12;
    return std::nullopt;
}

const char* driver_of(Backend b) {
    switch (b) {
        case Backend::vulkan: return "vulkan";
        case Backend::metal: return "metal";
        case Backend::d3d12: return "direct3d12";
    }
    return "";
}

SDL_GPUTextureFormat texture_format(Format f) {
    switch (f) {
        case Format::rgba8_unorm: return SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
        case Format::bgra8_unorm: return SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM;
        case Format::r8_unorm: return SDL_GPU_TEXTUREFORMAT_R8_UNORM;
        case Format::rgba16_float: return SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
        case Format::r32_float: return SDL_GPU_TEXTUREFORMAT_R32_FLOAT;
        case Format::r32_uint: return SDL_GPU_TEXTUREFORMAT_R32_UINT;
        case Format::d32_float: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
        case Format::d32_float_s8: return SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT;
    }
    return SDL_GPU_TEXTUREFORMAT_INVALID;
}

SDL_GPUSampleCount sample_count(std::uint32_t n) {
    switch (n) {
        case 2: return SDL_GPU_SAMPLECOUNT_2;
        case 4: return SDL_GPU_SAMPLECOUNT_4;
        case 8: return SDL_GPU_SAMPLECOUNT_8;
        default: return SDL_GPU_SAMPLECOUNT_1;
    }
}

SDL_GPUCompareOp compare_op(Compare c) {
    switch (c) {
        case Compare::never: return SDL_GPU_COMPAREOP_NEVER;
        case Compare::less: return SDL_GPU_COMPAREOP_LESS;
        case Compare::equal: return SDL_GPU_COMPAREOP_EQUAL;
        case Compare::less_equal: return SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
        case Compare::greater: return SDL_GPU_COMPAREOP_GREATER;
        case Compare::not_equal: return SDL_GPU_COMPAREOP_NOT_EQUAL;
        case Compare::greater_equal: return SDL_GPU_COMPAREOP_GREATER_OR_EQUAL;
        case Compare::always: return SDL_GPU_COMPAREOP_ALWAYS;
    }
    return SDL_GPU_COMPAREOP_ALWAYS;
}

SDL_GPULoadOp load_op(LoadOp l) {
    return l == LoadOp::clear ? SDL_GPU_LOADOP_CLEAR : l == LoadOp::load ? SDL_GPU_LOADOP_LOAD : SDL_GPU_LOADOP_DONT_CARE;
}

SDL_GPUTextureUsageFlags texture_usage(TextureUsage u) {
    SDL_GPUTextureUsageFlags f = 0;
    if (has(u, TextureUsage::sampled)) f |= SDL_GPU_TEXTUREUSAGE_SAMPLER;
    if (has(u, TextureUsage::color_target)) f |= SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
    if (has(u, TextureUsage::storage_read)) f |= SDL_GPU_TEXTUREUSAGE_GRAPHICS_STORAGE_READ;
    if (has(u, TextureUsage::compute_read)) f |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_READ;
    if (has(u, TextureUsage::compute_write)) f |= SDL_GPU_TEXTUREUSAGE_COMPUTE_STORAGE_WRITE;
    if (has(u, TextureUsage::depth_target)) f |= SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET;
    return f;
}

SDL_GPUBufferUsageFlags buffer_usage(BufferUsage u) {
    SDL_GPUBufferUsageFlags f = 0;
    if (has(u, BufferUsage::vertex)) f |= SDL_GPU_BUFFERUSAGE_VERTEX;
    if (has(u, BufferUsage::index)) f |= SDL_GPU_BUFFERUSAGE_INDEX;
    if (has(u, BufferUsage::indirect)) f |= SDL_GPU_BUFFERUSAGE_INDIRECT;
    if (has(u, BufferUsage::storage_read)) f |= SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    if (has(u, BufferUsage::compute_read)) f |= SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    if (has(u, BufferUsage::compute_write)) f |= SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    return f;
}

SDL_GPUFilter filter(Filter f) { return f == Filter::nearest ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR; }

SDL_GPUSamplerAddressMode address(AddressMode a) {
    switch (a) {
        case AddressMode::clamp: return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        case AddressMode::repeat: return SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
        case AddressMode::mirror: return SDL_GPU_SAMPLERADDRESSMODE_MIRRORED_REPEAT;
    }
    return SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
}

SDL_GPUVertexElementFormat vertex_format(VertexFormat f) {
    switch (f) {
        case VertexFormat::float1: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT;
        case VertexFormat::float2: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
        case VertexFormat::float3: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3;
        case VertexFormat::float4: return SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4;
        case VertexFormat::ubyte4_norm: return SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
        case VertexFormat::uint1: return SDL_GPU_VERTEXELEMENTFORMAT_UINT;
    }
    return SDL_GPU_VERTEXELEMENTFORMAT_INVALID;
}

SDL_GPUBlendFactor blend_factor(BlendFactor f) {
    switch (f) {
        case BlendFactor::zero: return SDL_GPU_BLENDFACTOR_ZERO;
        case BlendFactor::one: return SDL_GPU_BLENDFACTOR_ONE;
        case BlendFactor::src_color: return SDL_GPU_BLENDFACTOR_SRC_COLOR;
        case BlendFactor::one_minus_src_color: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::dst_color: return SDL_GPU_BLENDFACTOR_DST_COLOR;
        case BlendFactor::one_minus_dst_color: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::src_alpha: return SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        case BlendFactor::one_minus_src_alpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::dst_alpha: return SDL_GPU_BLENDFACTOR_DST_ALPHA;
        case BlendFactor::one_minus_dst_alpha: return SDL_GPU_BLENDFACTOR_ONE_MINUS_DST_ALPHA;
    }
    return SDL_GPU_BLENDFACTOR_ONE;
}

SDL_GPUBlendOp blend_op(BlendOp o) {
    switch (o) {
        case BlendOp::add: return SDL_GPU_BLENDOP_ADD;
        case BlendOp::subtract: return SDL_GPU_BLENDOP_SUBTRACT;
        case BlendOp::reverse_subtract: return SDL_GPU_BLENDOP_REVERSE_SUBTRACT;
        case BlendOp::min: return SDL_GPU_BLENDOP_MIN;
        case BlendOp::max: return SDL_GPU_BLENDOP_MAX;
    }
    return SDL_GPU_BLENDOP_ADD;
}

SDL_GPUPrimitiveType primitive(Topology t) {
    switch (t) {
        case Topology::triangle_list: return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        case Topology::triangle_strip: return SDL_GPU_PRIMITIVETYPE_TRIANGLESTRIP;
        case Topology::line_list: return SDL_GPU_PRIMITIVETYPE_LINELIST;
    }
    return SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
}

bool software_device(std::string_view name) {
    for (std::string_view s : {"llvmpipe", "lavapipe", "SwiftShader", "Microsoft Basic Render", "WARP"})
        if (name.contains(s)) return true;
    return false;
}

// SDL's video subsystem, while any device lives.
std::mutex video_mutex;
int        video_users = 0;

bool video_acquire(const std::string& driver) {
    std::scoped_lock lock(video_mutex);
    if (video_users == 0) {
        const bool choose = !driver.empty() && SDL_WasInit(SDL_INIT_VIDEO) == 0;
        if (choose) SDL_SetHintWithPriority(SDL_HINT_VIDEO_DRIVER, driver.c_str(), SDL_HINT_OVERRIDE);
        const bool ok = SDL_InitSubSystem(SDL_INIT_VIDEO);
        if (choose) SDL_ResetHint(SDL_HINT_VIDEO_DRIVER);
        if (!ok) return false;
    }
    ++video_users;
    return true;
}

void video_release() {
    std::scoped_lock lock(video_mutex);
    if (--video_users == 0) SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

constexpr std::uint32_t staging_alignment = 512;  // D3D12's placement alignment, the strictest

std::uint32_t align_up(std::uint32_t v, std::uint32_t a) { return (v + a - 1) / a * a; }

// ── the device ───────────────────────────────────────────────────────────

class SdlDevice final : public Device {
public:
    SdlDevice(SDL_GPUDevice* device, Caps caps) : device_(device), caps_(std::move(caps)) {
        // The order formats are preferred in for this backend.
        switch (caps_.backend) {
            case Backend::vulkan: preference_ = {ShaderFormat::spirv}; break;
            case Backend::metal: preference_ = {ShaderFormat::metallib, ShaderFormat::msl}; break;
            case Backend::d3d12: preference_ = {ShaderFormat::dxil, ShaderFormat::dxbc}; break;
        }
    }

    ~SdlDevice() override {
        SDL_WaitForGPUIdle(device_);
        if (window_) SDL_ReleaseWindowFromGPUDevice(device_, window_);
        for (auto& [id, fence] : fences_) SDL_ReleaseGPUFence(device_, fence);
        buffers_.for_each([&](Buffer, BufferSlot& b) { SDL_ReleaseGPUBuffer(device_, b.buffer); });
        textures_.for_each([&](Texture, TextureSlot& t) { SDL_ReleaseGPUTexture(device_, t.texture); });
        samplers_.for_each([&](Sampler, SDL_GPUSampler*& s) { SDL_ReleaseGPUSampler(device_, s); });
        graphics_.for_each([&](GraphicsPipeline, GraphicsSlot& p) { SDL_ReleaseGPUGraphicsPipeline(device_, p.pipeline); });
        compute_.for_each([&](ComputePipeline, ComputeSlot& p) { SDL_ReleaseGPUComputePipeline(device_, p.pipeline); });
        if (upload_) SDL_ReleaseGPUTransferBuffer(device_, upload_);
        if (download_) SDL_ReleaseGPUTransferBuffer(device_, download_);
        SDL_DestroyGPUDevice(device_);
        video_release();
    }

    const Caps& caps() const noexcept override { return caps_; }

    void destroy(Buffer b) noexcept override {
        if (auto* s = buffers_.get(b)) SDL_ReleaseGPUBuffer(device_, s->buffer), buffers_.erase(b);
    }
    void destroy(Texture t) noexcept override {
        if (auto* s = textures_.get(t)) SDL_ReleaseGPUTexture(device_, s->texture), textures_.erase(t);
    }
    void destroy(Sampler s) noexcept override {
        if (auto* p = samplers_.get(s)) SDL_ReleaseGPUSampler(device_, *p), samplers_.erase(s);
    }
    void destroy(GraphicsPipeline p) noexcept override {
        if (auto* s = graphics_.get(p)) SDL_ReleaseGPUGraphicsPipeline(device_, s->pipeline), graphics_.erase(p);
    }
    void destroy(ComputePipeline p) noexcept override {
        if (auto* s = compute_.get(p)) SDL_ReleaseGPUComputePipeline(device_, s->pipeline), compute_.erase(p);
    }

    std::optional<BufferDesc> describe(Buffer b) const noexcept override {
        const auto* s = buffers_.get(b);
        return s ? std::optional(s->desc) : std::nullopt;
    }
    std::optional<TextureDesc> describe(Texture t) const noexcept override {
        const auto* s = textures_.get(t);
        return s ? std::optional(s->desc) : std::nullopt;
    }
    bool exists(Sampler s) const noexcept override { return samplers_.contains(s); }
    std::optional<GraphicsPipelineInfo> describe(GraphicsPipeline p) const noexcept override {
        const auto* s = graphics_.get(p);
        return s ? std::optional(s->info) : std::nullopt;
    }
    std::optional<ComputePipelineInfo> describe(ComputePipeline p) const noexcept override {
        const auto* s = compute_.get(p);
        return s ? std::optional(s->info) : std::nullopt;
    }

    Result<void> wait(Submission s) override {
        auto it = fences_.find(s.id);
        if (it == fences_.end()) return {};  // already waited for
        const bool ok = SDL_WaitForGPUFences(device_, true, &it->second, 1);
        SDL_ReleaseGPUFence(device_, it->second);
        fences_.erase(it);
        if (!ok) return fail(Errc::device_lost, sdl_error("waiting for a submission"));
        return {};
    }

    Result<void> wait_idle() override {
        const bool ok = SDL_WaitForGPUIdle(device_);
        for (auto& [id, fence] : fences_) SDL_ReleaseGPUFence(device_, fence);
        fences_.clear();
        if (!ok) return fail(Errc::device_lost, sdl_error("waiting for the device"));
        return {};
    }

protected:
    Result<Buffer> make(const BufferDesc& d) override {
        SDL_GPUBufferCreateInfo info{};
        info.usage = buffer_usage(d.usage);
        info.size = d.size;
        SDL_GPUBuffer* b = SDL_CreateGPUBuffer(device_, &info);
        if (!b) return fail(Errc::out_of_memory, sdl_error("creating a buffer"));
        if (!d.label.empty()) SDL_SetGPUBufferName(device_, b, std::string(d.label).c_str());
        return buffers_.insert({b, {d.size, d.usage, {}}});
    }

    Result<Texture> make(const TextureDesc& d) override {
        SDL_GPUTextureCreateInfo info{};
        info.type = SDL_GPU_TEXTURETYPE_2D;
        info.format = texture_format(d.format);
        info.usage = texture_usage(d.usage);
        info.width = d.width;
        info.height = d.height;
        info.layer_count_or_depth = 1;
        info.num_levels = 1;
        info.sample_count = sample_count(d.samples);
        if (!SDL_GPUTextureSupportsFormat(device_, info.format, info.type, info.usage))
            return fail(Errc::unsupported, std::format("{} cannot make a {} texture for that use", caps_.device, name(d.format)));
        if (d.samples > 1 && !SDL_GPUTextureSupportsSampleCount(device_, info.format, info.sample_count))
            return fail(Errc::unsupported, std::format("{} cannot make a {} texture of {} samples", caps_.device, name(d.format), d.samples));
        SDL_GPUTexture* t = SDL_CreateGPUTexture(device_, &info);
        if (!t) return fail(Errc::out_of_memory, sdl_error("creating a texture"));
        if (!d.label.empty()) SDL_SetGPUTextureName(device_, t, std::string(d.label).c_str());
        return textures_.insert({t, {d.format, d.width, d.height, d.usage, {}, d.samples}});
    }

    Result<Sampler> make(const SamplerDesc& d) override {
        SDL_GPUSamplerCreateInfo info{};
        info.min_filter = filter(d.min);
        info.mag_filter = filter(d.mag);
        info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
        info.address_mode_u = address(d.u);
        info.address_mode_v = address(d.v);
        info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
        info.max_lod = 0;
        SDL_GPUSampler* s = SDL_CreateGPUSampler(device_, &info);
        if (!s) return fail(Errc::backend, sdl_error("creating a sampler"));
        return samplers_.insert(s);
    }

    Result<GraphicsPipeline> make(const GraphicsPipelineDesc& d) override {
        auto vs = shader(*d.vertex);
        if (!vs) return std::unexpected(vs.error());
        auto fs = shader(*d.fragment);
        if (!fs) {
            SDL_ReleaseGPUShader(device_, *vs);
            return std::unexpected(fs.error());
        }
        std::vector<SDL_GPUVertexBufferDescription> buffers;
        std::vector<SDL_GPUVertexAttribute> attributes;
        for (std::size_t slot = 0; slot < d.vertex_buffers.size(); ++slot) {
            const VertexBufferLayout& l = d.vertex_buffers[slot];
            buffers.push_back({static_cast<Uint32>(slot), l.stride,
                               l.per_instance ? SDL_GPU_VERTEXINPUTRATE_INSTANCE : SDL_GPU_VERTEXINPUTRATE_VERTEX, 0});
            for (const VertexAttribute& a : l.attributes)
                attributes.push_back({a.location, static_cast<Uint32>(slot), vertex_format(a.format), a.offset});
        }
        SDL_GPUColorTargetDescription target{};
        target.format = texture_format(d.target);
        target.blend_state.enable_blend = d.blend.enabled;
        target.blend_state.src_color_blendfactor = blend_factor(d.blend.src_color);
        target.blend_state.dst_color_blendfactor = blend_factor(d.blend.dst_color);
        target.blend_state.color_blend_op = blend_op(d.blend.color_op);
        target.blend_state.src_alpha_blendfactor = blend_factor(d.blend.src_alpha);
        target.blend_state.dst_alpha_blendfactor = blend_factor(d.blend.dst_alpha);
        target.blend_state.alpha_blend_op = blend_op(d.blend.alpha_op);

        SDL_GPUGraphicsPipelineCreateInfo info{};
        info.vertex_shader = *vs;
        info.fragment_shader = *fs;
        info.vertex_input_state.vertex_buffer_descriptions = buffers.data();
        info.vertex_input_state.num_vertex_buffers = static_cast<Uint32>(buffers.size());
        info.vertex_input_state.vertex_attributes = attributes.data();
        info.vertex_input_state.num_vertex_attributes = static_cast<Uint32>(attributes.size());
        info.primitive_type = primitive(d.topology);
        info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
        info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
        info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
        info.multisample_state.sample_count = sample_count(d.samples);
        info.target_info.color_target_descriptions = &target;
        info.target_info.num_color_targets = 1;
        if (d.depth_target) {
            info.target_info.has_depth_stencil_target = true;
            info.target_info.depth_stencil_format = texture_format(*d.depth_target);
            info.depth_stencil_state.enable_depth_test = d.depth.test;
            info.depth_stencil_state.enable_depth_write = d.depth.write;
            info.depth_stencil_state.compare_op = compare_op(d.depth.compare);
        }
        SDL_GPUGraphicsPipeline* p = SDL_CreateGPUGraphicsPipeline(device_, &info);
        SDL_ReleaseGPUShader(device_, *vs);
        SDL_ReleaseGPUShader(device_, *fs);
        if (!p) return fail(Errc::backend, sdl_error(std::format("creating the pipeline {}", d.label.empty() ? d.fragment->entry : d.label)));
        return graphics_.insert(
            {p, {counts(*d.vertex), counts(*d.fragment), static_cast<std::uint32_t>(d.vertex_buffers.size()), d.target, d.depth_target, d.samples}});
    }

    Result<ComputePipeline> make(const ComputePipelineDesc& d) override {
        const ShaderCode* code = pick(*d.shader);
        if (!code) return fail(Errc::unsupported, std::format("`{}` has no code this device takes", d.shader->entry));
        const std::string entry(d.shader->entry);
        SDL_GPUComputePipelineCreateInfo info{};
        info.code_size = code->bytes.size();
        info.code = reinterpret_cast<const Uint8*>(code->bytes.data());
        info.entrypoint = entry.c_str();
        info.format = sdl_format(code->format);
        info.num_samplers = d.shader->samplers;
        info.num_readonly_storage_textures = d.shader->storage_textures;
        info.num_readonly_storage_buffers = d.shader->storage_buffers;
        info.num_readwrite_storage_textures = d.shader->rw_storage_textures;
        info.num_readwrite_storage_buffers = d.shader->rw_storage_buffers;
        info.num_uniform_buffers = d.shader->uniform_buffers;
        info.threadcount_x = d.shader->threads[0];
        info.threadcount_y = d.shader->threads[1];
        info.threadcount_z = d.shader->threads[2];
        SDL_GPUComputePipeline* p = SDL_CreateGPUComputePipeline(device_, &info);
        if (!p) return fail(Errc::backend, sdl_error(std::format("creating the compute pipeline {}", d.shader->entry)));
        return compute_.insert({p, {counts(*d.shader)}});
    }

    Result<Submission> run(const CommandList& list) override;
    Result<void> fetch(Texture t, Region region, std::span<std::byte> out) override;
    Result<void> fetch(Buffer b, std::uint32_t offset, std::span<std::byte> out) override;

public:
    // The window this device draws into: claimed, its swapchain in sync with
    // the display (or as fast as it goes).
    Result<void> claim(SDL_Window* window, bool vsync) {
        if (!SDL_ClaimWindowForGPUDevice(device_, window)) {
            // A window of the offscreen video driver has no display: when the
            // backend makes no swapchain for it (no headless surface), the
            // device still draws, and present() shows nothing.
            const char* video = SDL_GetCurrentVideoDriver();
            if (video && std::string_view(video) == "offscreen") {
                displayless_ = true;
                return {};
            }
            return fail(Errc::unsupported, sdl_error("claiming the window for the GPU"));
        }
        window_ = window;
        const SDL_GPUPresentMode mode =
            vsync || !SDL_WindowSupportsGPUPresentMode(device_, window, SDL_GPU_PRESENTMODE_IMMEDIATE) ? SDL_GPU_PRESENTMODE_VSYNC
                                                                                                       : SDL_GPU_PRESENTMODE_IMMEDIATE;
        if (!SDL_SetGPUSwapchainParameters(device_, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode))
            return fail(Errc::unsupported, sdl_error("setting the window's swapchain"));
        return {};
    }

protected:
    // The frame blitted into the next swapchain texture (SDL converts to the
    // swapchain's format and scales to its size), after every submission so
    // far, since command buffers run in order.
    Result<bool> show(Texture frame) override {
        if (displayless_) return false;  // an offscreen window: nothing to show
        if (!window_) return fail(Errc::unsupported, "present: the device was made for no window (rhi::sdl::Options::window)");
        const TextureSlot* f = textures_.get(frame);
        if (!f) return fail(Errc::invalid, "present: a stale texture handle");
        SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(device_);
        if (!cmd) return fail(Errc::device_lost, sdl_error("acquiring a command buffer"));
        SDL_GPUTexture* swap = nullptr;
        Uint32 w = 0, h = 0;
        if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window_, &swap, &w, &h)) {
            SDL_CancelGPUCommandBuffer(cmd);
            return fail(Errc::device_lost, sdl_error("acquiring the window's swapchain texture"));
        }
        if (swap && w > 0 && h > 0) {
            SDL_GPUBlitInfo blit{};
            blit.source.texture = f->texture;
            blit.source.w = f->desc.width;
            blit.source.h = f->desc.height;
            blit.destination.texture = swap;
            blit.destination.w = w;
            blit.destination.h = h;
            blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
            blit.filter = (w == f->desc.width && h == f->desc.height) ? SDL_GPU_FILTER_NEAREST : SDL_GPU_FILTER_LINEAR;
            SDL_BlitGPUTexture(cmd, &blit);
        }
        if (!SDL_SubmitGPUCommandBuffer(cmd)) return fail(Errc::device_lost, sdl_error("submitting the present"));
        return swap != nullptr;
    }

private:
    SDL_Window* window_ = nullptr;  // claimed (Options::window)
    bool        displayless_ = false;  // made for an offscreen window it could not claim

    struct BufferSlot {
        SDL_GPUBuffer* buffer = nullptr;
        BufferDesc     desc;
    };
    struct TextureSlot {
        SDL_GPUTexture* texture = nullptr;
        TextureDesc     desc;
    };
    struct GraphicsSlot {
        SDL_GPUGraphicsPipeline* pipeline = nullptr;
        GraphicsPipelineInfo     info;
    };
    struct ComputeSlot {
        SDL_GPUComputePipeline* pipeline = nullptr;
        ComputePipelineInfo     info;
    };

    // The code of the format this backend prefers among a shader's.
    const ShaderCode* pick(const ShaderDesc& d) const {
        for (ShaderFormat f : preference_)
            if (caps_.formats.has(f))
                if (const ShaderCode* c = d.code_for(f)) return c;
        return nullptr;
    }

    Result<SDL_GPUShader*> shader(const ShaderDesc& d) {
        const ShaderCode* code = pick(d);
        if (!code) return fail(Errc::unsupported, std::format("`{}` has no code this device takes", d.entry));
        const std::string entry(d.entry);
        SDL_GPUShaderCreateInfo info{};
        info.code_size = code->bytes.size();
        info.code = reinterpret_cast<const Uint8*>(code->bytes.data());
        info.entrypoint = entry.c_str();
        info.format = sdl_format(code->format);
        info.stage = d.stage == Stage::vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT;
        info.num_samplers = d.samplers;
        info.num_storage_textures = d.storage_textures;
        info.num_storage_buffers = d.storage_buffers;
        info.num_uniform_buffers = d.uniform_buffers;
        SDL_GPUShader* s = SDL_CreateGPUShader(device_, &info);
        if (!s) return fail(Errc::backend, sdl_error(std::format("creating the shader `{}` ({})", d.entry, name(code->format))));
        return s;
    }

    // A transfer buffer of at least `size` bytes (kept, grown by doubling).
    SDL_GPUTransferBuffer* transfer(SDL_GPUTransferBuffer*& tb, std::uint32_t& capacity, std::uint32_t size, SDL_GPUTransferBufferUsage usage) {
        if (tb && capacity >= size) return tb;
        if (tb) SDL_ReleaseGPUTransferBuffer(device_, tb);
        capacity = std::max(size, capacity * 2);
        SDL_GPUTransferBufferCreateInfo info{};
        info.usage = usage;
        info.size = capacity;
        tb = SDL_CreateGPUTransferBuffer(device_, &info);
        if (!tb) capacity = 0;
        return tb;
    }

    Result<void> finish(SDL_GPUCommandBuffer* cmd) {
        SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
        if (!fence) return fail(Errc::device_lost, sdl_error("submitting"));
        const bool ok = SDL_WaitForGPUFences(device_, true, &fence, 1);
        SDL_ReleaseGPUFence(device_, fence);
        if (!ok) return fail(Errc::device_lost, sdl_error("waiting for a readback"));
        return {};
    }

    SDL_GPUDevice*                                    device_;
    Caps                                              caps_;
    std::vector<ShaderFormat>                         preference_;
    SlotMap<BufferSlot, BufferTag>                    buffers_;
    SlotMap<TextureSlot, TextureTag>                  textures_;
    SlotMap<SDL_GPUSampler*, SamplerTag>              samplers_;
    SlotMap<GraphicsSlot, GraphicsPipelineTag>        graphics_;
    SlotMap<ComputeSlot, ComputePipelineTag>          compute_;
    std::map<std::uint64_t, SDL_GPUFence*>            fences_;
    std::uint64_t                                     next_submission_ = 1;
    SDL_GPUTransferBuffer*                            upload_ = nullptr;
    std::uint32_t                                     upload_capacity_ = 0;
    SDL_GPUTransferBuffer*                            download_ = nullptr;
    std::uint32_t                                     download_capacity_ = 0;
    std::vector<std::uint32_t>                        staged_;  // per upload command, its offset in upload_
};

Result<Submission> SdlDevice::run(const CommandList& list) {
    const auto commands = list.commands();

    // Every upload's bytes, staged in one mapping of the upload buffer.
    staged_.clear();
    std::uint32_t total = 0;
    for (const Command& c : commands) {
        std::uint32_t size = 0;
        if (const auto* u = std::get_if<cmd::UploadBuffer>(&c)) size = static_cast<std::uint32_t>(u->data.size());
        if (const auto* u = std::get_if<cmd::UploadTexture>(&c))
            size = u->region.width * u->region.height * bytes_per_pixel(textures_.get(u->texture)->desc.format);
        if (size) {
            total = align_up(total, staging_alignment);
            staged_.push_back(total);
            total += size;
        }
    }
    if (total) {
        if (!transfer(upload_, upload_capacity_, total, SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD))
            return fail(Errc::out_of_memory, sdl_error("creating the upload buffer"));
        auto* mapped = static_cast<std::byte*>(SDL_MapGPUTransferBuffer(device_, upload_, true));
        if (!mapped) return fail(Errc::backend, sdl_error("mapping the upload buffer"));
        std::size_t n = 0;
        for (const Command& c : commands) {
            if (const auto* u = std::get_if<cmd::UploadBuffer>(&c)) {
                std::memcpy(mapped + staged_[n++], u->data.data(), u->data.size());
            } else if (const auto* t = std::get_if<cmd::UploadTexture>(&c)) {
                // Rows packed tightly, whatever their pitch in the list.
                const std::size_t row = std::size_t{t->region.width} * bytes_per_pixel(textures_.get(t->texture)->desc.format);
                const std::size_t pitch = t->row_bytes ? t->row_bytes : row;
                for (std::uint32_t y = 0; y < t->region.height; ++y)
                    std::memcpy(mapped + staged_[n] + y * row, t->data.data() + y * pitch, row);
                ++n;
            }
        }
        SDL_UnmapGPUTransferBuffer(device_, upload_);
    }

    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(device_);
    if (!cb) return fail(Errc::device_lost, sdl_error("acquiring a command buffer"));
    SDL_GPUCopyPass*    copy = nullptr;
    SDL_GPURenderPass*  render = nullptr;
    SDL_GPUComputePass* compute = nullptr;
    std::size_t         upload_index = 0;
    std::string         failure;
    auto copy_pass = [&] {
        if (!copy) copy = SDL_BeginGPUCopyPass(cb);
        return copy;
    };
    auto end_copy = [&] {
        if (copy) SDL_EndGPUCopyPass(copy), copy = nullptr;
    };

    for (const Command& c : commands) {
        std::visit(
            [&]<class T>(const T& x) {
                if constexpr (std::is_same_v<T, cmd::UploadBuffer>) {
                    const SDL_GPUTransferBufferLocation from{upload_, staged_[upload_index++]};
                    const SDL_GPUBufferRegion to{buffers_.get(x.buffer)->buffer, x.offset, static_cast<Uint32>(x.data.size())};
                    if (auto* p = copy_pass()) SDL_UploadToGPUBuffer(p, &from, &to, false);
                } else if constexpr (std::is_same_v<T, cmd::UploadTexture>) {
                    SDL_GPUTextureTransferInfo from{};
                    from.transfer_buffer = upload_;
                    from.offset = staged_[upload_index++];
                    from.pixels_per_row = x.region.width;
                    from.rows_per_layer = x.region.height;
                    SDL_GPUTextureRegion to{};
                    to.texture = textures_.get(x.texture)->texture;
                    to.x = x.region.x, to.y = x.region.y, to.w = x.region.width, to.h = x.region.height, to.d = 1;
                    if (auto* p = copy_pass()) SDL_UploadToGPUTexture(p, &from, &to, false);
                } else if constexpr (std::is_same_v<T, cmd::CopyTexture>) {
                    SDL_GPUTextureLocation from{}, to{};
                    from.texture = textures_.get(x.from)->texture;
                    from.x = x.region.x, from.y = x.region.y;
                    to.texture = textures_.get(x.to)->texture;
                    to.x = x.x, to.y = x.y;
                    if (auto* p = copy_pass()) SDL_CopyGPUTextureToTexture(p, &from, &to, x.region.width, x.region.height, 1, false);
                } else if constexpr (std::is_same_v<T, cmd::BeginRender>) {
                    end_copy();
                    SDL_GPUColorTargetInfo target{};
                    target.texture = textures_.get(x.target.texture)->texture;
                    target.clear_color = {x.target.clear[0], x.target.clear[1], x.target.clear[2], x.target.clear[3]};
                    target.load_op = x.target.load == LoadOp::clear  ? SDL_GPU_LOADOP_CLEAR
                                     : x.target.load == LoadOp::load ? SDL_GPU_LOADOP_LOAD
                                                                     : SDL_GPU_LOADOP_DONT_CARE;
                    target.store_op = SDL_GPU_STOREOP_STORE;
                    if (x.target.resolve) {
                        target.resolve_texture = textures_.get(x.target.resolve)->texture;
                        target.store_op = SDL_GPU_STOREOP_RESOLVE;
                    }
                    SDL_GPUDepthStencilTargetInfo depth{};
                    if (x.depth.texture) {
                        depth.texture = textures_.get(x.depth.texture)->texture;
                        depth.clear_depth = x.depth.clear_depth;
                        depth.load_op = load_op(x.depth.load);
                        depth.store_op = x.depth.store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
                        depth.stencil_load_op = load_op(x.depth.load);
                        depth.stencil_store_op = x.depth.store ? SDL_GPU_STOREOP_STORE : SDL_GPU_STOREOP_DONT_CARE;
                        depth.clear_stencil = x.depth.clear_stencil;
                    }
                    render = SDL_BeginGPURenderPass(cb, &target, 1, x.depth.texture ? &depth : nullptr);
                    if (!render) failure = sdl_error("beginning a render pass");
                } else if constexpr (std::is_same_v<T, cmd::EndRender>) {
                    if (render) SDL_EndGPURenderPass(render), render = nullptr;
                } else if constexpr (std::is_same_v<T, cmd::SetPipeline>) {
                    if (render) SDL_BindGPUGraphicsPipeline(render, graphics_.get(x.pipeline)->pipeline);
                } else if constexpr (std::is_same_v<T, cmd::SetViewport>) {
                    const SDL_GPUViewport v{x.x, x.y, x.width, x.height, 0, 1};
                    if (render) SDL_SetGPUViewport(render, &v);
                } else if constexpr (std::is_same_v<T, cmd::SetScissor>) {
                    const SDL_Rect r{static_cast<int>(x.rect.x), static_cast<int>(x.rect.y), static_cast<int>(x.rect.width),
                                     static_cast<int>(x.rect.height)};
                    if (render) SDL_SetGPUScissor(render, &r);
                } else if constexpr (std::is_same_v<T, cmd::BindVertexBuffer>) {
                    const SDL_GPUBufferBinding b{buffers_.get(x.buffer)->buffer, x.offset};
                    if (render) SDL_BindGPUVertexBuffers(render, x.slot, &b, 1);
                } else if constexpr (std::is_same_v<T, cmd::BindIndexBuffer>) {
                    const SDL_GPUBufferBinding b{buffers_.get(x.buffer)->buffer, x.offset};
                    if (render) SDL_BindGPUIndexBuffer(render, &b, x.wide ? SDL_GPU_INDEXELEMENTSIZE_32BIT : SDL_GPU_INDEXELEMENTSIZE_16BIT);
                } else if constexpr (std::is_same_v<T, cmd::BindTexture>) {
                    const SDL_GPUTextureSamplerBinding b{textures_.get(x.texture)->texture, *samplers_.get(x.sampler)};
                    if (x.stage == Stage::vertex && render) SDL_BindGPUVertexSamplers(render, x.slot, &b, 1);
                    if (x.stage == Stage::fragment && render) SDL_BindGPUFragmentSamplers(render, x.slot, &b, 1);
                    if (x.stage == Stage::compute && compute) SDL_BindGPUComputeSamplers(compute, x.slot, &b, 1);
                } else if constexpr (std::is_same_v<T, cmd::BindStorageTexture>) {
                    SDL_GPUTexture* t = textures_.get(x.texture)->texture;
                    if (x.stage == Stage::vertex && render) SDL_BindGPUVertexStorageTextures(render, x.slot, &t, 1);
                    if (x.stage == Stage::fragment && render) SDL_BindGPUFragmentStorageTextures(render, x.slot, &t, 1);
                    if (x.stage == Stage::compute && compute) SDL_BindGPUComputeStorageTextures(compute, x.slot, &t, 1);
                } else if constexpr (std::is_same_v<T, cmd::BindStorageBuffer>) {
                    SDL_GPUBuffer* b = buffers_.get(x.buffer)->buffer;
                    if (x.stage == Stage::vertex && render) SDL_BindGPUVertexStorageBuffers(render, x.slot, &b, 1);
                    if (x.stage == Stage::fragment && render) SDL_BindGPUFragmentStorageBuffers(render, x.slot, &b, 1);
                    if (x.stage == Stage::compute && compute) SDL_BindGPUComputeStorageBuffers(compute, x.slot, &b, 1);
                } else if constexpr (std::is_same_v<T, cmd::Draw>) {
                    if (render) SDL_DrawGPUPrimitives(render, x.vertices, x.instances, x.first_vertex, x.first_instance);
                } else if constexpr (std::is_same_v<T, cmd::DrawIndexed>) {
                    if (render) SDL_DrawGPUIndexedPrimitives(render, x.indices, x.instances, x.first_index, x.vertex_offset, x.first_instance);
                } else if constexpr (std::is_same_v<T, cmd::BeginCompute>) {
                    end_copy();
                    std::vector<SDL_GPUStorageTextureReadWriteBinding> textures;
                    for (Texture t : x.textures) {
                        SDL_GPUStorageTextureReadWriteBinding b{};
                        b.texture = textures_.get(t)->texture;
                        textures.push_back(b);
                    }
                    std::vector<SDL_GPUStorageBufferReadWriteBinding> buffers;
                    for (Buffer b : x.buffers) {
                        SDL_GPUStorageBufferReadWriteBinding s{};
                        s.buffer = buffers_.get(b)->buffer;
                        buffers.push_back(s);
                    }
                    compute = SDL_BeginGPUComputePass(cb, textures.data(), static_cast<Uint32>(textures.size()), buffers.data(),
                                                      static_cast<Uint32>(buffers.size()));
                    if (!compute) failure = sdl_error("beginning a compute pass");
                } else if constexpr (std::is_same_v<T, cmd::EndCompute>) {
                    if (compute) SDL_EndGPUComputePass(compute), compute = nullptr;
                } else if constexpr (std::is_same_v<T, cmd::SetComputePipeline>) {
                    if (compute) SDL_BindGPUComputePipeline(compute, compute_.get(x.pipeline)->pipeline);
                } else if constexpr (std::is_same_v<T, cmd::Dispatch>) {
                    if (compute) SDL_DispatchGPUCompute(compute, x.x, x.y, x.z);
                } else if constexpr (std::is_same_v<T, cmd::PushUniforms>) {
                    const auto len = static_cast<Uint32>(x.data.size());
                    if (x.stage == Stage::vertex) SDL_PushGPUVertexUniformData(cb, x.slot, x.data.data(), len);
                    if (x.stage == Stage::fragment) SDL_PushGPUFragmentUniformData(cb, x.slot, x.data.data(), len);
                    if (x.stage == Stage::compute) SDL_PushGPUComputeUniformData(cb, x.slot, x.data.data(), len);
                }
            },
            c);
        if (!failure.empty()) break;
    }
    end_copy();
    if (render) SDL_EndGPURenderPass(render);
    if (compute) SDL_EndGPUComputePass(compute);
    if (!failure.empty()) {
        SDL_CancelGPUCommandBuffer(cb);
        return fail(Errc::backend, failure);
    }
    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cb);
    if (!fence) return fail(Errc::device_lost, sdl_error("submitting"));
    const Submission s{next_submission_++};
    fences_.emplace(s.id, fence);
    return s;
}

Result<void> SdlDevice::fetch(Texture t, Region region, std::span<std::byte> out) {
    const auto size = static_cast<std::uint32_t>(out.size());
    if (!transfer(download_, download_capacity_, size, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD))
        return fail(Errc::out_of_memory, sdl_error("creating the download buffer"));
    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(device_);
    if (!cb) return fail(Errc::device_lost, sdl_error("acquiring a command buffer"));
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cb);
    SDL_GPUTextureRegion from{};
    from.texture = textures_.get(t)->texture;
    from.x = region.x, from.y = region.y, from.w = region.width, from.h = region.height, from.d = 1;
    SDL_GPUTextureTransferInfo to{};
    to.transfer_buffer = download_;
    to.pixels_per_row = region.width;
    to.rows_per_layer = region.height;
    SDL_DownloadFromGPUTexture(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    if (auto r = finish(cb); !r) return r;
    const auto* mapped = static_cast<const std::byte*>(SDL_MapGPUTransferBuffer(device_, download_, false));
    if (!mapped) return fail(Errc::backend, sdl_error("mapping the download buffer"));
    std::memcpy(out.data(), mapped, out.size());
    SDL_UnmapGPUTransferBuffer(device_, download_);
    return {};
}

Result<void> SdlDevice::fetch(Buffer b, std::uint32_t offset, std::span<std::byte> out) {
    const auto size = static_cast<std::uint32_t>(out.size());
    if (!transfer(download_, download_capacity_, size, SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD))
        return fail(Errc::out_of_memory, sdl_error("creating the download buffer"));
    SDL_GPUCommandBuffer* cb = SDL_AcquireGPUCommandBuffer(device_);
    if (!cb) return fail(Errc::device_lost, sdl_error("acquiring a command buffer"));
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cb);
    const SDL_GPUBufferRegion from{buffers_.get(b)->buffer, offset, size};
    const SDL_GPUTransferBufferLocation to{download_, 0};
    SDL_DownloadFromGPUBuffer(copy, &from, &to);
    SDL_EndGPUCopyPass(copy);
    if (auto r = finish(cb); !r) return r;
    const auto* mapped = static_cast<const std::byte*>(SDL_MapGPUTransferBuffer(device_, download_, false));
    if (!mapped) return fail(Errc::backend, sdl_error("mapping the download buffer"));
    std::memcpy(out.data(), mapped, out.size());
    SDL_UnmapGPUTransferBuffer(device_, download_);
    return {};
}

}  // namespace

Result<std::unique_ptr<Device>> create(const Options& options) {
    if (options.formats.empty()) return fail(Errc::invalid, "no shader format: say which formats the program's shaders carry");
    if (!options.vulkan_library.empty()) SDL_SetHint(SDL_HINT_VULKAN_LIBRARY, options.vulkan_library.c_str());
    if (!video_acquire(options.video_driver)) return fail(Errc::unsupported, sdl_error("initialising SDL's video subsystem"));
    const SDL_PropertiesID props = SDL_CreateProperties();
    auto want = [&](const char* key, ShaderFormat f) { SDL_SetBooleanProperty(props, key, options.formats.has(f)); };
    want(SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, ShaderFormat::spirv);
    want(SDL_PROP_GPU_DEVICE_CREATE_SHADERS_MSL_BOOLEAN, ShaderFormat::msl);
    want(SDL_PROP_GPU_DEVICE_CREATE_SHADERS_METALLIB_BOOLEAN, ShaderFormat::metallib);
    want(SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXBC_BOOLEAN, ShaderFormat::dxbc);
    want(SDL_PROP_GPU_DEVICE_CREATE_SHADERS_DXIL_BOOLEAN, ShaderFormat::dxil);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, options.debug);
    SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_PREFERLOWPOWER_BOOLEAN, options.prefer_low_power);
    if (options.backend) SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, driver_of(*options.backend));
    SDL_GPUDevice* device = SDL_CreateGPUDeviceWithProperties(props);
    SDL_DestroyProperties(props);
    if (!device) {
        std::string why = sdl_error("no GPU device");
        video_release();
        return fail(Errc::unsupported, std::move(why));
    }
    const char* driver = SDL_GetGPUDeviceDriver(device);
    const auto backend = backend_of(driver ? driver : "");
    if (!backend) {
        SDL_DestroyGPUDevice(device);
        video_release();
        return fail(Errc::unsupported, std::format("SDL chose the GPU driver `{}`, which rhi does not map", driver ? driver : "?"));
    }
    Caps caps;
    caps.backend = *backend;
    const SDL_PropertiesID info = SDL_GetGPUDeviceProperties(device);
    const char* device_name = info ? SDL_GetStringProperty(info, SDL_PROP_GPU_DEVICE_NAME_STRING, nullptr) : nullptr;
    caps.device = device_name ? device_name : std::string(name(*backend));
    caps.formats = ours(SDL_GetGPUShaderFormats(device)) & options.formats;
    caps.software = software_device(caps.device);
    caps.compute = true;
    caps.max_texture_size = 8192;
    for (const auto& [n, count] : {std::pair{8u, SDL_GPU_SAMPLECOUNT_8}, std::pair{4u, SDL_GPU_SAMPLECOUNT_4}, std::pair{2u, SDL_GPU_SAMPLECOUNT_2}})
        if (SDL_GPUTextureSupportsSampleCount(device, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, count)) {
            caps.max_samples = n;
            break;
        }
    caps.depth = SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT, SDL_GPU_TEXTURETYPE_2D,
                                              SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET) &&
                 SDL_GPUTextureSupportsFormat(device, SDL_GPU_TEXTUREFORMAT_D32_FLOAT_S8_UINT, SDL_GPU_TEXTURETYPE_2D,
                                              SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
    auto made = std::make_unique<SdlDevice>(device, std::move(caps));
    if (options.window != 0) {
        SDL_Window* window = SDL_GetWindowFromID(static_cast<SDL_WindowID>(options.window));
        if (!window) return fail(Errc::invalid, std::format("no window has the id {} (SDL_GetWindowID)", options.window));
        if (auto r = made->claim(window, options.vsync); !r) return std::unexpected(r.error());
    }
    return std::unique_ptr<Device>(std::move(made));
}

std::vector<Backend> backends() {
    std::vector<Backend> out;
    for (int i = 0; i < SDL_GetNumGPUDrivers(); ++i)
        if (auto b = backend_of(SDL_GetGPUDriver(i))) out.push_back(*b);
    return out;
}

}  // namespace omui::rhi::sdl
