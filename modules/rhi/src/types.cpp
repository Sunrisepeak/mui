module openmui.rhi;

import std;

namespace omui::rhi {

std::string_view name(Errc code) noexcept {
    switch (code) {
        case Errc::unsupported: return "unsupported";
        case Errc::invalid: return "invalid";
        case Errc::out_of_memory: return "out-of-memory";
        case Errc::device_lost: return "device-lost";
        case Errc::backend: return "backend";
    }
    return "?";
}

std::string_view name(Backend backend) noexcept {
    switch (backend) {
        case Backend::vulkan: return "vulkan";
        case Backend::metal: return "metal";
        case Backend::d3d12: return "d3d12";
    }
    return "?";
}

namespace {
constexpr std::array<std::string_view, shader_format_count> shader_format_names{"spirv", "msl", "metallib", "dxbc", "dxil"};
}

std::string_view name(ShaderFormat format) noexcept { return shader_format_names[static_cast<std::size_t>(format)]; }

std::optional<ShaderFormat> shader_format(std::string_view n) noexcept {
    for (std::size_t i = 0; i < shader_format_names.size(); ++i)
        if (shader_format_names[i] == n) return static_cast<ShaderFormat>(i);
    return std::nullopt;
}

std::string_view name(Format format) noexcept {
    switch (format) {
        case Format::rgba8_unorm: return "rgba8_unorm";
        case Format::bgra8_unorm: return "bgra8_unorm";
        case Format::r8_unorm: return "r8_unorm";
        case Format::rgba16_float: return "rgba16_float";
        case Format::r32_float: return "r32_float";
        case Format::r32_uint: return "r32_uint";
    }
    return "?";
}

std::uint32_t bytes_per_pixel(Format format) noexcept {
    switch (format) {
        case Format::rgba8_unorm:
        case Format::bgra8_unorm:
        case Format::r32_float:
        case Format::r32_uint: return 4;
        case Format::r8_unorm: return 1;
        case Format::rgba16_float: return 8;
    }
    return 0;
}

std::string_view name(Stage stage) noexcept {
    switch (stage) {
        case Stage::vertex: return "vertex";
        case Stage::fragment: return "fragment";
        case Stage::compute: return "compute";
    }
    return "?";
}

std::uint32_t size_of(VertexFormat format) noexcept {
    switch (format) {
        case VertexFormat::float1: return 4;
        case VertexFormat::float2: return 8;
        case VertexFormat::float3: return 12;
        case VertexFormat::float4: return 16;
        case VertexFormat::ubyte4_norm: return 4;
        case VertexFormat::uint1: return 4;
    }
    return 0;
}

std::string check(const BufferDesc& d) {
    if (d.size == 0) return "a buffer of 0 bytes";
    if (d.usage == BufferUsage::none) return "a buffer with no usage";
    return {};
}

std::string check(const TextureDesc& d, const Caps& caps) {
    if (d.width == 0 || d.height == 0) return std::format("a texture of {}×{}", d.width, d.height);
    if (d.width > caps.max_texture_size || d.height > caps.max_texture_size)
        return std::format("a texture of {}×{}: this device's largest is {}", d.width, d.height, caps.max_texture_size);
    if (d.usage == TextureUsage::none) return "a texture with no usage";
    const bool storage = has(d.usage, TextureUsage::storage_read) || has(d.usage, TextureUsage::compute_read) ||
                         has(d.usage, TextureUsage::compute_write);
    if (storage && (d.format == Format::bgra8_unorm))
        return std::format("{} cannot be a storage texture (rgba8_unorm, rgba16_float, r32_float or r32_uint can)", name(d.format));
    return {};
}

std::string check(const ShaderDesc& d, ShaderFormats accepted) {
    if (d.entry.empty()) return "a shader without an entry point";
    if (d.code.empty()) return std::format("the shader `{}` has no code", d.entry);
    if ((d.formats() & accepted).empty()) {
        std::string have, want;
        for (const ShaderCode& c : d.code) have += std::format("{}{}", have.empty() ? "" : ", ", name(c.format));
        for (std::size_t i = 0; i < shader_format_count; ++i)
            if (accepted.has(static_cast<ShaderFormat>(i)))
                want += std::format("{}{}", want.empty() ? "" : ", ", name(static_cast<ShaderFormat>(i)));
        return std::format("the shader `{}` is compiled to {}; this device takes {}", d.entry, have, want);
    }
    for (const ShaderCode& c : d.code)
        if (c.bytes.empty()) return std::format("the shader `{}` has empty {} code", d.entry, name(c.format));
    if (d.stage != Stage::compute && (d.rw_storage_textures || d.rw_storage_buffers))
        return std::format("the {} shader `{}` declares read-write storage; only compute shaders write storage", name(d.stage), d.entry);
    if (d.stage == Stage::compute && (d.threads[0] == 0 || d.threads[1] == 0 || d.threads[2] == 0))
        return std::format("the compute shader `{}` has a work group of {}×{}×{}", d.entry, d.threads[0], d.threads[1], d.threads[2]);
    return {};
}

std::string check(const GraphicsPipelineDesc& d, ShaderFormats accepted) {
    if (!d.vertex || !d.fragment) return "a graphics pipeline needs a vertex and a fragment shader";
    if (d.vertex->stage != Stage::vertex) return std::format("`{}` is a {} shader, not a vertex shader", d.vertex->entry, name(d.vertex->stage));
    if (d.fragment->stage != Stage::fragment)
        return std::format("`{}` is a {} shader, not a fragment shader", d.fragment->entry, name(d.fragment->stage));
    if (auto e = check(*d.vertex, accepted); !e.empty()) return e;
    if (auto e = check(*d.fragment, accepted); !e.empty()) return e;
    std::vector<std::uint32_t> locations;
    for (std::size_t slot = 0; slot < d.vertex_buffers.size(); ++slot) {
        const VertexBufferLayout& b = d.vertex_buffers[slot];
        if (b.stride == 0) return std::format("vertex buffer {} has a stride of 0", slot);
        for (const VertexAttribute& a : b.attributes) {
            if (a.offset + size_of(a.format) > b.stride)
                return std::format("the attribute at location {} ends at byte {}, past the stride {} of vertex buffer {}", a.location,
                                   a.offset + size_of(a.format), b.stride, slot);
            if (std::ranges::contains(locations, a.location)) return std::format("two vertex attributes at location {}", a.location);
            locations.push_back(a.location);
        }
    }
    return {};
}

std::string check(const ComputePipelineDesc& d, ShaderFormats accepted) {
    if (!d.shader) return "a compute pipeline needs a shader";
    if (d.shader->stage != Stage::compute) return std::format("`{}` is a {} shader, not a compute shader", d.shader->entry, name(d.shader->stage));
    return check(*d.shader, accepted);
}

}  // namespace omui::rhi
