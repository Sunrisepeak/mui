// openmui.rhi:types — what a GPU device is asked for: resources by
// description, shaders by code and resource counts, pipelines by state.
//
// The vocabulary is UI-scale (architecture §8.2, D-33): 2D colour targets,
// sampled and storage textures, vertex / index / storage buffers, uniforms
// pushed by value, one blend state per pipeline, compute. No depth, no
// multisampling, no bindless: what openmui's renderer, effects and canvas
// need, mapped thinly onto a backend (SDL_GPU first).
export module openmui.rhi:types;

import std;
import openmui.memory;

export namespace omui::rhi {

// ── errors ───────────────────────────────────────────────────────────────

enum class Errc : std::uint8_t {
    unsupported,    // no device, or not what this one can do (the caller falls back)
    invalid,        // a description or a command list that is wrong (a bug)
    out_of_memory,
    device_lost,
    backend,        // the backend failed for its own reason (its message says it)
};
[[nodiscard]] std::string_view name(Errc code) noexcept;

struct Error {
    Errc        code = Errc::backend;
    std::string message;
};

template <class T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(Errc code, std::string message) {
    return std::unexpected(Error{code, std::move(message)});
}

// ── devices ──────────────────────────────────────────────────────────────

enum class Backend : std::uint8_t { vulkan, metal, d3d12 };
[[nodiscard]] std::string_view name(Backend backend) noexcept;

// Shader code a device accepts, one format per backend family.
enum class ShaderFormat : std::uint8_t { spirv, msl, metallib, dxbc, dxil };
inline constexpr std::size_t shader_format_count = 5;
[[nodiscard]] std::string_view name(ShaderFormat format) noexcept;
[[nodiscard]] std::optional<ShaderFormat> shader_format(std::string_view name) noexcept;

// A set of shader formats.
struct ShaderFormats {
    std::uint8_t bits = 0;

    [[nodiscard]] constexpr bool has(ShaderFormat f) const noexcept { return (bits >> static_cast<int>(f)) & 1u; }
    constexpr ShaderFormats& add(ShaderFormat f) noexcept {
        bits = static_cast<std::uint8_t>(bits | (1u << static_cast<int>(f)));
        return *this;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return bits == 0; }
    [[nodiscard]] constexpr ShaderFormats operator&(ShaderFormats o) const noexcept { return {static_cast<std::uint8_t>(bits & o.bits)}; }
    [[nodiscard]] constexpr ShaderFormats operator|(ShaderFormats o) const noexcept { return {static_cast<std::uint8_t>(bits | o.bits)}; }
    // Spelled out, not defaulted: GCC 16.1 crashes comparing a defaulted
    // friend of an exported type from another module.
    friend constexpr bool operator==(ShaderFormats a, ShaderFormats b) noexcept { return a.bits == b.bits; }
};
[[nodiscard]] constexpr ShaderFormats formats(std::initializer_list<ShaderFormat> list) noexcept {
    ShaderFormats s;
    for (ShaderFormat f : list) s.add(f);
    return s;
}

struct Caps {
    Backend       backend = Backend::vulkan;
    std::string   device;                 // what the backend calls the adapter
    ShaderFormats formats;                // the shader formats it accepts
    bool          software = false;       // a CPU implementation (lavapipe, WARP)
    bool          compute = true;
    std::uint32_t max_texture_size = 8192;
};

// ── resources ────────────────────────────────────────────────────────────

struct BufferTag;
struct TextureTag;
struct SamplerTag;
struct GraphicsPipelineTag;
struct ComputePipelineTag;
using Buffer           = Handle<BufferTag>;
using Texture          = Handle<TextureTag>;
using Sampler          = Handle<SamplerTag>;
using GraphicsPipeline = Handle<GraphicsPipelineTag>;
using ComputePipeline  = Handle<ComputePipelineTag>;

enum class Format : std::uint8_t {
    rgba8_unorm,    // the renderer's premultiplied RGBA, byte order R G B A
    bgra8_unorm,    // what swapchains often are
    r8_unorm,       // coverage masks, glyph atlases
    rgba16_float,   // effects' intermediate layers
    r32_float,
    r32_uint,       // object ids (pixel provenance)
};
inline constexpr std::size_t format_count = 6;
[[nodiscard]] std::string_view name(Format format) noexcept;
[[nodiscard]] std::uint32_t bytes_per_pixel(Format format) noexcept;

// Usage flags: what a resource will be used for, declared up front.
enum class TextureUsage : std::uint8_t {
    none          = 0,
    sampled       = 1 << 0,  // read through a sampler in vertex or fragment shaders
    color_target  = 1 << 1,  // rendered to
    storage_read  = 1 << 2,  // read as a storage texture by vertex or fragment shaders
    compute_read  = 1 << 3,  // read as a storage texture by compute shaders
    compute_write = 1 << 4,  // written by compute shaders
};
enum class BufferUsage : std::uint8_t {
    none          = 0,
    vertex        = 1 << 0,
    index         = 1 << 1,
    indirect      = 1 << 2,
    storage_read  = 1 << 3,  // read by vertex or fragment shaders
    compute_read  = 1 << 4,
    compute_write = 1 << 5,
};
[[nodiscard]] constexpr TextureUsage operator|(TextureUsage a, TextureUsage b) noexcept {
    return static_cast<TextureUsage>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr BufferUsage operator|(BufferUsage a, BufferUsage b) noexcept {
    return static_cast<BufferUsage>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
[[nodiscard]] constexpr bool has(TextureUsage set, TextureUsage u) noexcept {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(u)) != 0;
}
[[nodiscard]] constexpr bool has(BufferUsage set, BufferUsage u) noexcept {
    return (static_cast<std::uint8_t>(set) & static_cast<std::uint8_t>(u)) != 0;
}

struct BufferDesc {
    std::uint32_t    size = 0;   // bytes
    BufferUsage      usage = BufferUsage::vertex;
    std::string_view label;
};

struct TextureDesc {
    Format           format = Format::rgba8_unorm;
    std::uint32_t    width = 0;
    std::uint32_t    height = 0;
    TextureUsage     usage = TextureUsage::sampled;
    std::string_view label;
};

enum class Filter : std::uint8_t { nearest, linear };
enum class AddressMode : std::uint8_t { clamp, repeat, mirror };

struct SamplerDesc {
    Filter      min = Filter::linear;
    Filter      mag = Filter::linear;
    AddressMode u = AddressMode::clamp;
    AddressMode v = AddressMode::clamp;
};

// A rectangle of texels.
struct Region {
    std::uint32_t x = 0, y = 0, width = 0, height = 0;
    friend constexpr bool operator==(Region a, Region b) noexcept {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
};

// ── shaders ──────────────────────────────────────────────────────────────

enum class Stage : std::uint8_t { vertex, fragment, compute };
[[nodiscard]] std::string_view name(Stage stage) noexcept;

struct ShaderCode {
    ShaderFormat              format = ShaderFormat::spirv;
    std::span<const std::byte> bytes;  // MSL is text, the others binary
};

// One entry point: its code in every format it was compiled to, and the
// resources it binds, by kind (backends need the counts at creation). Slots
// follow SDL_GPU's convention, which the Slang compiler step checks: for
// vertex and fragment shaders sampled textures, storage textures, storage
// buffers and uniform buffers each from slot 0; for compute shaders the
// read-only and the read-write resources, then the uniform buffers.
struct ShaderDesc {
    Stage                       stage = Stage::vertex;
    std::string_view            entry;
    std::span<const ShaderCode> code;
    std::uint32_t               samplers = 0;          // sampled textures (combined with their sampler)
    std::uint32_t               storage_textures = 0;  // read-only
    std::uint32_t               storage_buffers = 0;   // read-only
    std::uint32_t               uniform_buffers = 0;
    std::uint32_t               rw_storage_textures = 0;  // compute only
    std::uint32_t               rw_storage_buffers = 0;   // compute only
    std::array<std::uint32_t, 3> threads{1, 1, 1};        // compute only: the work group size
    std::string_view            label;

    [[nodiscard]] const ShaderCode* code_for(ShaderFormat f) const noexcept {
        for (const ShaderCode& c : code)
            if (c.format == f) return &c;
        return nullptr;
    }
    [[nodiscard]] ShaderFormats formats() const noexcept {
        ShaderFormats s;
        for (const ShaderCode& c : code) s.add(c.format);
        return s;
    }
};

enum class VertexFormat : std::uint8_t { float1, float2, float3, float4, ubyte4_norm, uint1 };
[[nodiscard]] std::uint32_t size_of(VertexFormat format) noexcept;

struct VertexAttribute {
    std::uint32_t location = 0;  // the shader's TEXCOORD<n> / location n
    VertexFormat  format = VertexFormat::float2;
    std::uint32_t offset = 0;
};

// The layout of the vertex buffer bound at its index in the pipeline's list.
struct VertexBufferLayout {
    std::uint32_t                    stride = 0;
    bool                             per_instance = false;
    std::span<const VertexAttribute> attributes;
};

enum class BlendFactor : std::uint8_t {
    zero, one, src_color, one_minus_src_color, dst_color, one_minus_dst_color,
    src_alpha, one_minus_src_alpha, dst_alpha, one_minus_dst_alpha,
};
enum class BlendOp : std::uint8_t { add, subtract, reverse_subtract, min, max };

struct Blend {
    bool        enabled = false;
    BlendFactor src_color = BlendFactor::one, dst_color = BlendFactor::zero;
    BlendOp     color_op = BlendOp::add;
    BlendFactor src_alpha = BlendFactor::one, dst_alpha = BlendFactor::zero;
    BlendOp     alpha_op = BlendOp::add;

    // Source over destination, premultiplied: openmui's normal compositing.
    [[nodiscard]] static constexpr Blend premultiplied() noexcept {
        return {true, BlendFactor::one, BlendFactor::one_minus_src_alpha, BlendOp::add,
                BlendFactor::one, BlendFactor::one_minus_src_alpha, BlendOp::add};
    }
    [[nodiscard]] static constexpr Blend additive() noexcept {
        return {true, BlendFactor::one, BlendFactor::one, BlendOp::add, BlendFactor::one, BlendFactor::one, BlendOp::add};
    }
    [[nodiscard]] static constexpr Blend replace() noexcept { return {}; }
    friend constexpr bool operator==(Blend a, Blend b) noexcept {
        return a.enabled == b.enabled && a.src_color == b.src_color && a.dst_color == b.dst_color && a.color_op == b.color_op &&
               a.src_alpha == b.src_alpha && a.dst_alpha == b.dst_alpha && a.alpha_op == b.alpha_op;
    }
};

enum class Topology : std::uint8_t { triangle_list, triangle_strip, line_list };

struct GraphicsPipelineDesc {
    const ShaderDesc*                   vertex = nullptr;
    const ShaderDesc*                   fragment = nullptr;
    std::span<const VertexBufferLayout> vertex_buffers;
    Format                              target = Format::rgba8_unorm;
    Blend                               blend = Blend::premultiplied();
    Topology                            topology = Topology::triangle_list;
    std::string_view                    label;
};

struct ComputePipelineDesc {
    const ShaderDesc* shader = nullptr;
    std::string_view  label;
};

// What a pipeline's shaders bind, by kind: what validation checks slots against.
struct Counts {
    std::uint32_t samplers = 0, storage_textures = 0, storage_buffers = 0, uniform_buffers = 0;
    std::uint32_t rw_storage_textures = 0, rw_storage_buffers = 0;
    friend constexpr bool operator==(Counts a, Counts b) noexcept {
        return a.samplers == b.samplers && a.storage_textures == b.storage_textures && a.storage_buffers == b.storage_buffers &&
               a.uniform_buffers == b.uniform_buffers && a.rw_storage_textures == b.rw_storage_textures &&
               a.rw_storage_buffers == b.rw_storage_buffers;
    }
};
[[nodiscard]] constexpr Counts counts(const ShaderDesc& d) noexcept {
    return {d.samplers, d.storage_textures, d.storage_buffers, d.uniform_buffers, d.rw_storage_textures, d.rw_storage_buffers};
}
struct GraphicsPipelineInfo {
    Counts        vertex, fragment;
    std::uint32_t vertex_buffers = 0;
    Format        target = Format::rgba8_unorm;
};
struct ComputePipelineInfo {
    Counts shader;
};

// What a submission is waited on by.
struct Submission {
    std::uint64_t id = 0;
    friend constexpr bool operator==(Submission a, Submission b) noexcept { return a.id == b.id; }
};

// What makes a description wrong before any backend sees it ("" when it is
// right): sizes, usages, the stages and counts of shaders.
[[nodiscard]] std::string check(const BufferDesc& d);
[[nodiscard]] std::string check(const TextureDesc& d, const Caps& caps);
[[nodiscard]] std::string check(const ShaderDesc& d, ShaderFormats accepted);
[[nodiscard]] std::string check(const GraphicsPipelineDesc& d, ShaderFormats accepted);
[[nodiscard]] std::string check(const ComputePipelineDesc& d, ShaderFormats accepted);

}  // namespace omui::rhi
