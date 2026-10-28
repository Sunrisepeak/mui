module openmui.rhi.cts;

import std;
import openmui.rhi;

namespace omui::rhi::cts {

namespace {

constexpr std::uint32_t W = 8, H = 8;

using Rgba = std::array<int, 4>;

struct V {  // fill's per-vertex data
    float x, y, r, g, b, a;
};
struct I {  // fill's per-instance offset
    float dx, dy;
};
struct Z {  // depth's vertex
    float x, y, z, r, g, b, a;
};
struct T {  // textured's vertex
    float x, y, u, v;
};

// The two triangles of an axis-aligned rectangle (NDC).
std::array<V, 6> quad(float x0, float y0, float x1, float y1, Rgba c) {
    const float r = c[0] / 255.0f, g = c[1] / 255.0f, b = c[2] / 255.0f, a = c[3] / 255.0f;
    return {V{x0, y0, r, g, b, a}, V{x1, y0, r, g, b, a}, V{x0, y1, r, g, b, a},
            V{x1, y0, r, g, b, a}, V{x1, y1, r, g, b, a}, V{x0, y1, r, g, b, a}};
}

std::array<float, 4> unit(Rgba c) { return {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f}; }

std::string show(Rgba c) { return std::format("({}, {}, {}, {})", c[0], c[1], c[2], c[3]); }

// The device, the suite's pipelines, and an 8×8 colour target.
struct Bench {
    Device&          d;
    Texture          target;
    Buffer           one_instance;  // a single zero offset
    Sampler          nearest;
    GraphicsPipeline fill, fill_blend, textured, depth, depth_msaa_fill;
    ComputePipeline  compute;
    std::vector<std::function<void()>> release;
    std::string      failure;  // the case's first problem ("" while it holds)

    [[nodiscard]] bool failed() const noexcept { return !failure.empty(); }
    void fail(std::string why) {
        if (failure.empty()) failure = std::move(why);
    }
    // A result's value, or (recording why) an empty one: what follows a
    // failure runs on invalid handles, which validation refuses harmlessly.
    template <class R>
    R must(Result<R> r, std::string_view what) {
        if (r) return std::move(*r);
        fail(std::format("{}: {} ({})", what, r.error().message, name(r.error().code)));
        return R{};
    }
    void must(Result<void> r, std::string_view what) {
        if (!r) fail(std::format("{}: {} ({})", what, r.error().message, name(r.error().code)));
    }

    std::array<VertexAttribute, 2> fill_vertex{VertexAttribute{0, VertexFormat::float2, 0}, VertexAttribute{1, VertexFormat::float4, 8}};
    std::array<VertexAttribute, 1> fill_instance{VertexAttribute{2, VertexFormat::float2, 0}};
    std::array<VertexBufferLayout, 2> fill_layout{VertexBufferLayout{sizeof(V), false, fill_vertex},
                                                  VertexBufferLayout{sizeof(I), true, fill_instance}};
    std::array<VertexAttribute, 2> textured_vertex{VertexAttribute{0, VertexFormat::float2, 0}, VertexAttribute{1, VertexFormat::float2, 8}};
    std::array<VertexBufferLayout, 1> textured_layout{VertexBufferLayout{sizeof(T), false, textured_vertex}};
    std::array<VertexAttribute, 2> depth_vertex{VertexAttribute{0, VertexFormat::float3, 0}, VertexAttribute{1, VertexFormat::float4, 12}};
    std::array<VertexBufferLayout, 1> depth_layout{VertexBufferLayout{sizeof(Z), false, depth_vertex}};

    explicit Bench(Device& device) : d(device) {
        target = texture(Format::rgba8_unorm, W, H, TextureUsage::color_target | TextureUsage::sampled, "cts.target");
        one_instance = buffer(sizeof(I), BufferUsage::vertex, "cts.instance");
        nearest = must(d.create(SamplerDesc{Filter::nearest, Filter::nearest}), "a nearest sampler");
        release.push_back([this] { d.destroy(nearest); });
        fill = pipeline({.vertex = &shaders::fill::fill_vs, .fragment = &shaders::fill::fill_fs, .vertex_buffers = fill_layout,
                         .blend = Blend::replace(), .label = "cts.fill"});
        fill_blend = pipeline({.vertex = &shaders::fill::fill_vs, .fragment = &shaders::fill::fill_fs, .vertex_buffers = fill_layout,
                               .blend = Blend::premultiplied(), .label = "cts.fill_blend"});
        textured = pipeline({.vertex = &shaders::textured::textured_vs, .fragment = &shaders::textured::textured_fs,
                             .vertex_buffers = textured_layout, .blend = Blend::replace(), .label = "cts.textured"});
        depth = pipeline({.vertex = &shaders::depth::depth_vs, .fragment = &shaders::depth::depth_fs, .vertex_buffers = depth_layout,
                          .blend = Blend::replace(), .depth_target = Format::d32_float,
                          .depth = {.test = true, .write = true, .compare = Compare::less}, .label = "cts.depth"});
        depth_msaa_fill = pipeline({.vertex = &shaders::fill::fill_vs, .fragment = &shaders::fill::fill_fs, .vertex_buffers = fill_layout,
                                    .blend = Blend::replace(), .samples = 4, .label = "cts.fill_msaa"});
        compute = must(d.create(ComputePipelineDesc{&shaders::compute::pattern, "cts.compute"}), "the compute pipeline");
        release.push_back([this] { d.destroy(compute); });
        CommandList l;
        const I zero{0, 0};
        l.upload(one_instance, 0, std::span<const I>(&zero, 1));
        submit(l);
    }
    ~Bench() {
        static_cast<void>(d.wait_idle());
        for (auto& r : std::views::reverse(release)) r();
    }
    Bench(const Bench&) = delete;
    Bench& operator=(const Bench&) = delete;

    Texture texture(Format f, std::uint32_t w, std::uint32_t h, TextureUsage u, std::string_view label) {
        Texture t = must(d.create(TextureDesc{f, w, h, u, label}), std::format("a {}×{} {} texture", w, h, name(f)));
        release.push_back([this, t] { d.destroy(t); });
        return t;
    }
    Buffer buffer(std::uint32_t size, BufferUsage u, std::string_view label) {
        Buffer b = must(d.create(BufferDesc{size, u, label}), std::format("a buffer of {} bytes", size));
        release.push_back([this, b] { d.destroy(b); });
        return b;
    }
    template <class Vertex, std::size_t N>
    Buffer vertices(const std::array<Vertex, N>& v) {
        Buffer b = buffer(static_cast<std::uint32_t>(sizeof(Vertex) * N), BufferUsage::vertex, "cts.vertices");
        CommandList l;
        l.upload(b, 0, std::span<const Vertex>(v));
        submit(l);
        return b;
    }
    GraphicsPipeline pipeline(const GraphicsPipelineDesc& desc) {
        GraphicsPipeline p = must(d.create(desc), std::format("the pipeline {}", desc.label));
        release.push_back([this, p] { d.destroy(p); });
        return p;
    }
    void submit(const CommandList& l) { must(d.wait(must(d.submit(l), "submit")), "wait"); }

    // A pass on the target: clear (or load), then whatever `draw` records.
    void pass(LoadOp load, Rgba clear, std::function<void(CommandList&)> draw = {}) {
        CommandList l;
        l.begin_render({target, load, unit(clear)});
        if (draw) draw(l);
        l.end_render();
        submit(l);
    }
    void fill_with(CommandList& l, Buffer quad_vertices, std::uint32_t count, std::uint32_t instances = 1, Buffer instance = {},
                   bool blend = false) {
        l.set_pipeline(blend ? fill_blend : fill);
        l.bind_vertex_buffer(0, quad_vertices);
        l.bind_vertex_buffer(1, instance ? instance : one_instance);
        l.draw(count, instances);
    }

    std::vector<Rgba> pixels(Texture t = {}, std::uint32_t w = W, std::uint32_t h = H) {
        const auto bytes = must(d.read(t ? t : target, Region{0, 0, w, h}), "read back");
        if (bytes.empty()) return {};
        std::vector<Rgba> out(std::size_t{w} * h);
        for (std::size_t i = 0; i < out.size(); ++i)
            for (int c = 0; c < 4; ++c) out[i][c] = std::to_integer<int>(bytes[i * 4 + c]);
        return out;
    }
};

// Every pixel against `want(x, y)`, within `tolerance`.
void expect(Bench& b, const std::vector<Rgba>& px, std::uint32_t w, const std::function<Rgba(std::uint32_t, std::uint32_t)>& want,
            int tolerance = 0) {
    for (std::uint32_t i = 0; i < px.size() && !b.failed(); ++i) {
        const std::uint32_t x = i % w, y = i / w;
        const Rgba e = want(x, y);
        for (int c = 0; c < 4; ++c)
            if (std::abs(px[i][c] - e[c]) > tolerance) {
                b.fail(std::format("pixel ({}, {}) is {}; it should be {}{}", x, y, show(px[i]), show(e),
                                   tolerance ? std::format(" (±{})", tolerance) : ""));
                return;
            }
    }
}

constexpr Rgba black{0, 0, 0, 255}, red{255, 0, 0, 255}, green{0, 255, 0, 255}, blue{0, 0, 255, 255}, white{255, 255, 255, 255};

// ── the cases ────────────────────────────────────────────────────────────

Texture must_msaa(Bench& b) {
    return b.must(b.d.create(TextureDesc{Format::rgba8_unorm, W, H, TextureUsage::color_target, "cts.msaa", 4}), "a 4-sample target");
}

// The two triangles of a rectangle at depth z (NDC).
std::array<Z, 6> zquad(float x0, float y0, float x1, float y1, float z, Rgba c) {
    const float r = c[0] / 255.0f, g = c[1] / 255.0f, b = c[2] / 255.0f, a = c[3] / 255.0f;
    return {Z{x0, y0, z, r, g, b, a}, Z{x1, y0, z, r, g, b, a}, Z{x0, y1, z, r, g, b, a},
            Z{x1, y0, z, r, g, b, a}, Z{x1, y1, z, r, g, b, a}, Z{x0, y1, z, r, g, b, a}};
}

// The depth test, in the order drawn: what lies behind is not drawn, what
// lies in front is, whatever the order.
void depth_test(Bench& b) {
    const Texture z = b.texture(Format::d32_float, W, H, TextureUsage::depth_target, "cts.depth");
    const Buffer middle = b.vertices(zquad(-1, -1, 1, 1, 0.5f, red));
    const Buffer behind = b.vertices(zquad(-1, -1, 0, 1, 0.75f, blue));  // the left half, behind
    const Buffer front = b.vertices(zquad(-1, 0, 1, 1, 0.25f, green));   // the top half, in front
    CommandList l;
    l.begin_render({b.target, LoadOp::clear, unit(black)}, {z, LoadOp::clear, 1.0f});
    l.set_pipeline(b.depth);
    for (Buffer q : {middle, behind, front}) {
        l.bind_vertex_buffer(0, q);
        l.draw(6);
    }
    l.end_render();
    b.submit(l);
    expect(b, b.pixels(), W, [](auto, auto y) { return y < 4 ? green : red; });
}

// Depths kept by one pass (store) and tested by the next (load).
void depth_kept(Bench& b) {
    const Texture z = b.texture(Format::d32_float, W, H, TextureUsage::depth_target, "cts.depth_kept");
    const Buffer first = b.vertices(zquad(-1, -1, 1, 1, 0.5f, red));
    const Buffer behind = b.vertices(zquad(-1, -1, 1, 1, 0.6f, blue));
    const Buffer front = b.vertices(zquad(-1, -1, 0, 1, 0.4f, green));  // the left half
    CommandList l;
    l.begin_render({b.target, LoadOp::clear, unit(black)}, {z, LoadOp::clear, 1.0f, 0, true});
    l.set_pipeline(b.depth);
    l.bind_vertex_buffer(0, first);
    l.draw(6);
    l.end_render();
    l.begin_render({b.target, LoadOp::load}, {z, LoadOp::load, 1.0f, 0, true});
    l.set_pipeline(b.depth);
    l.bind_vertex_buffer(0, behind);
    l.draw(6);
    l.bind_vertex_buffer(0, front);
    l.draw(6);
    l.end_render();
    b.submit(l);
    expect(b, b.pixels(), W, [](auto x, auto) { return x < 4 ? green : red; });
}

// Four samples a texel, resolved: a texel half inside an edge is half
// covered — the standard 4× sample positions (the same on Vulkan, Metal and
// Direct3D 12) put two of its samples left of the texel's middle.
void multisample(Bench& b) {
    if (b.d.caps().max_samples < 4) {
        b.fail(std::format("the device multisamples to {}; every backend must take 4", b.d.caps().max_samples));
        return;
    }
    const Texture msaa = must_msaa(b);
    const Buffer q = b.vertices(quad(-1, -1, 0.125f, 1, red));  // to x = 4.5 texels
    CommandList l;
    l.begin_render({msaa, LoadOp::clear, unit(black), b.target});
    l.set_pipeline(b.depth_msaa_fill);
    l.bind_vertex_buffer(0, q);
    l.bind_vertex_buffer(1, b.one_instance);
    l.draw(6);
    l.end_render();
    b.submit(l);
    expect(b, b.pixels(), W, [](auto x, auto) { return x < 4 ? red : x == 4 ? Rgba{128, 0, 0, 255} : black; }, 1);
}

void clear(Bench& b) {
    b.pass(LoadOp::clear, {51, 102, 153, 204});
    expect(b, b.pixels(), W, [](auto, auto) { return Rgba{51, 102, 153, 204}; });
}

// NDC +y is the top row, x −1 the left column, on every backend.
void orientation(Bench& b) {
    const Buffer q = b.vertices(quad(-1, 0, 0, 1, red));
    b.pass(LoadOp::clear, black, [&](CommandList& l) { b.fill_with(l, q, 6); });
    expect(b, b.pixels(), W, [](auto x, auto y) { return x < 4 && y < 4 ? red : black; });
}

void instancing(Bench& b) {
    const Buffer q = b.vertices(quad(-1, 0.5f, -0.5f, 1, green));  // the 2×2 block at the top left
    const std::array<I, 4> offsets{I{0, 0}, I{1, 0}, I{0, -1}, I{1, -1}};
    const Buffer inst = b.vertices(offsets);
    b.pass(LoadOp::clear, black, [&](CommandList& l) { b.fill_with(l, q, 6, 4, inst); });
    expect(b, b.pixels(), W, [](auto x, auto y) { return (x % 4) < 2 && (y % 4) < 2 ? green : black; });
}

// 16-bit indices on the left half; 32-bit ones, with a first index and a
// vertex offset, on the right.
void indexed(Bench& b) {
    const std::array<V, 8> v{V{-1, -1, 1, 0, 0, 1}, V{0, -1, 1, 0, 0, 1}, V{0, 1, 1, 0, 0, 1}, V{-1, 1, 1, 0, 0, 1},
                             V{0, -1, 0, 1, 0, 1}, V{1, -1, 0, 1, 0, 1},  V{1, 1, 0, 1, 0, 1}, V{0, 1, 0, 1, 0, 1}};
    const Buffer vb = b.vertices(v);
    const std::array<std::uint16_t, 6> i16{0, 1, 2, 0, 2, 3};
    const std::array<std::uint32_t, 9> i32{9, 9, 9, 0, 1, 2, 0, 2, 3};  // the first three are skipped
    const Buffer b16 = b.buffer(sizeof i16, BufferUsage::index, "cts.i16");
    const Buffer b32 = b.buffer(sizeof i32, BufferUsage::index, "cts.i32");
    CommandList up;
    up.upload(b16, 0, std::span<const std::uint16_t>(i16));
    up.upload(b32, 0, std::span<const std::uint32_t>(i32));
    b.submit(up);
    b.pass(LoadOp::clear, black, [&](CommandList& l) {
        l.set_pipeline(b.fill);
        l.bind_vertex_buffer(0, vb);
        l.bind_vertex_buffer(1, b.one_instance);
        l.bind_index_buffer(b16, false);
        l.draw_indexed(6);
        l.bind_index_buffer(b32, true);
        l.draw_indexed(6, 1, 3, 4);
    });
    expect(b, b.pixels(), W, [](auto x, auto) { return x < 4 ? red : green; });
}

// Premultiplied source over: 0.4 red at 0.4 alpha over 0.8 blue.
void blending(Bench& b) {
    const Buffer q = b.vertices(quad(-1, -1, 1, 1, {102, 0, 0, 102}));
    b.pass(LoadOp::clear, {0, 0, 204, 255}, [&](CommandList& l) { b.fill_with(l, q, 6, 1, {}, true); });
    expect(b, b.pixels(), W, [](auto, auto) { return Rgba{102, 0, 122, 255}; }, 1);
}

void scissor(Bench& b) {
    const Buffer q = b.vertices(quad(-1, -1, 1, 1, white));
    b.pass(LoadOp::clear, black, [&](CommandList& l) {
        l.set_scissor(Region{2, 2, 4, 3});
        b.fill_with(l, q, 6);
    });
    expect(b, b.pixels(), W, [](auto x, auto y) { return x >= 2 && x < 6 && y >= 2 && y < 5 ? white : black; });
}

void viewport(Bench& b) {
    const Buffer q = b.vertices(quad(-1, -1, 1, 1, red));
    b.pass(LoadOp::clear, black, [&](CommandList& l) {
        l.set_viewport(0, 4, 4, 4);  // the bottom-left quarter
        b.fill_with(l, q, 6);
    });
    expect(b, b.pixels(), W, [](auto x, auto y) { return x < 4 && y >= 4 ? red : black; });
}

// A 2×2 texture over the target, nearest: uv (0, 0) is the top-left texel.
void sampling(Bench& b, Rgba tint, const std::function<Rgba(Rgba)>& tinted) {
    const Texture image = b.texture(Format::rgba8_unorm, 2, 2, TextureUsage::sampled, "cts.image");
    const std::array<std::uint8_t, 16> texels{255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255};
    const std::array<T, 6> v{T{-1, 1, 0, 0}, T{1, 1, 1, 0}, T{-1, -1, 0, 1}, T{1, 1, 1, 0}, T{1, -1, 1, 1}, T{-1, -1, 0, 1}};
    const Buffer vb = b.vertices(v);
    CommandList up;
    up.upload(image, Region{0, 0, 2, 2}, std::as_bytes(std::span(texels)));
    b.submit(up);
    b.pass(LoadOp::clear, black, [&](CommandList& l) {
        l.set_pipeline(b.textured);
        l.bind_vertex_buffer(0, vb);
        l.bind_texture(Stage::fragment, 0, image, b.nearest);
        l.push_uniforms(Stage::fragment, 0, unit(tint));
        l.draw(6);
    });
    expect(b, b.pixels(), W, [&](auto x, auto y) {
        const Rgba quadrant = y < 4 ? (x < 4 ? red : green) : (x < 4 ? blue : white);
        return tinted(quadrant);
    });
}

void texture_nearest(Bench& b) { sampling(b, white, [](Rgba c) { return c; }); }

// The uniform multiplies: white × (0.4, 0.6, 1, 1).
void uniforms(Bench& b) {
    sampling(b, {102, 153, 255, 255}, [](Rgba c) {
        return Rgba{c[0] * 102 / 255, c[1] * 153 / 255, c[2], c[3]};
    });
}

// A second pass that loads keeps the first's pixels.
void load_keeps(Bench& b) {
    b.pass(LoadOp::clear, red);
    const Buffer q = b.vertices(quad(-1, -1, 0, 1, green));
    b.pass(LoadOp::load, black, [&](CommandList& l) { b.fill_with(l, q, 6); });
    expect(b, b.pixels(), W, [](auto x, auto) { return x < 4 ? green : red; });
}

void copy(Bench& b) {
    const Texture src = b.texture(Format::rgba8_unorm, 4, 4, TextureUsage::sampled, "cts.copy_src");
    std::array<std::uint8_t, 64> texels{};
    for (std::uint32_t i = 0; i < 16; ++i) {
        texels[i * 4] = static_cast<std::uint8_t>(i * 16);
        texels[i * 4 + 1] = static_cast<std::uint8_t>(255 - i * 16);
        texels[i * 4 + 3] = 255;
    }
    CommandList l;
    l.upload(src, Region{0, 0, 4, 4}, std::as_bytes(std::span(texels)));
    b.submit(l);
    b.pass(LoadOp::clear, blue);
    CommandList c;
    c.copy(src, Region{1, 1, 2, 2}, b.target, 5, 5);
    b.submit(c);
    expect(b, b.pixels(), W, [](auto x, auto y) {
        if (x >= 5 && x < 7 && y >= 5 && y < 7) {
            const int i = static_cast<int>((y - 5 + 1) * 4 + (x - 5 + 1));
            return Rgba{i * 16, 255 - i * 16, 0, 255};
        }
        return blue;
    });
}

// A compute pass writes every texel and element; the uniforms say how many.
void compute(Bench& b) {
    const Texture out = b.texture(Format::rgba8_unorm, W, H, TextureUsage::compute_write | TextureUsage::sampled, "cts.compute_out");
    const Buffer values = b.buffer(W * H * 4, BufferUsage::compute_write, "cts.compute_values");
    const std::array<std::uint32_t, 4> params{W, H, 0, 0};
    CommandList l;
    const std::array<Texture, 1> textures{out};
    const std::array<Buffer, 1> buffers{values};
    l.begin_compute(textures, buffers);
    l.set_pipeline(b.compute);
    l.push_uniforms(Stage::compute, 0, params);
    l.dispatch(1, 1);
    l.end_compute();
    b.submit(l);
    expect(b, b.pixels(out), W, [](auto x, auto y) { return Rgba{static_cast<int>(x * 16), static_cast<int>(y * 16), 0, 255}; });
    std::array<std::uint32_t, W * H> got{};
    b.must(b.d.read(values, 0, std::as_writable_bytes(std::span(got))), "read the buffer");
    for (std::uint32_t i = 0; i < got.size() && !b.failed(); ++i)
        if (got[i] != i % W + i / W * 100) b.fail(std::format("element {} is {}; it should be {}", i, got[i], i % W + i / W * 100));
}

// r8_unorm and rgba16_float, uploaded or cleared, read back byte for byte.
void formats(Bench& b) {
    const Texture r8 = b.texture(Format::r8_unorm, 4, 2, TextureUsage::sampled, "cts.r8");
    const std::array<std::uint8_t, 8> bytes{0, 51, 102, 153, 204, 255, 17, 34};
    CommandList l;
    l.upload(r8, Region{0, 0, 4, 2}, std::as_bytes(std::span(bytes)));
    b.submit(l);
    const auto back = b.must(b.d.read(r8, Region{0, 0, 4, 2}), "read r8");
    if (b.failed()) return;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        if (std::to_integer<int>(back[i]) != bytes[i]) {
            b.fail(std::format("r8 texel {} is {}; it should be {}", i, std::to_integer<int>(back[i]), bytes[i]));
            return;
        }
    const Texture half = b.texture(Format::rgba16_float, 2, 2, TextureUsage::color_target, "cts.rgba16f");
    CommandList c;
    c.begin_render({half, LoadOp::clear, {0.5f, 0.25f, 1.0f, 0.0f}});
    c.end_render();
    b.submit(c);
    const auto h = b.must(b.d.read(half, Region{0, 0, 2, 2}), "read rgba16f");
    if (b.failed()) return;
    const std::array<std::uint16_t, 4> want{0x3800, 0x3400, 0x3c00, 0x0000};  // 0.5, 0.25, 1, 0 as halves
    for (std::size_t p = 0; p < 4; ++p)
        for (std::size_t c2 = 0; c2 < 4; ++c2) {
            const auto lo = std::to_integer<std::uint16_t>(h[p * 8 + c2 * 2]), hi = std::to_integer<std::uint16_t>(h[p * 8 + c2 * 2 + 1]);
            const std::uint16_t got = static_cast<std::uint16_t>(lo | hi << 8);
            if (got != want[c2]) {
                b.fail(std::format("rgba16f texel {} channel {} is {:#06x}; it should be {:#06x}", p, c2, got, want[c2]));
                return;
            }
        }
}

// Submissions run in order: a copy submitted after a clear sees it, waited on alone.
void ordering(Bench& b) {
    const Texture other = b.texture(Format::rgba8_unorm, W, H, TextureUsage::color_target | TextureUsage::sampled, "cts.other");
    CommandList first;
    first.begin_render({b.target, LoadOp::clear, unit(red)});
    first.end_render();
    CommandList second;
    second.copy(b.target, Region{0, 0, W, H}, other);
    static_cast<void>(b.must(b.d.submit(first), "the first submission"));
    b.must(b.d.wait(b.must(b.d.submit(second), "the second submission")), "wait for the second");
    expect(b, b.pixels(other), W, [](auto, auto) { return red; });
}

void buffers(Bench& b) {
    const Buffer buf = b.buffer(64, BufferUsage::vertex, "cts.buffer");
    const std::array<std::uint32_t, 4> words{1, 2, 3, 0xdeadbeef};
    CommandList l;
    l.upload(buf, 16, std::span<const std::uint32_t>(words));
    b.submit(l);
    std::array<std::uint32_t, 4> back{};
    b.must(b.d.read(buf, 16, std::as_writable_bytes(std::span(back))), "read the buffer");
    if (!b.failed() && back != words) b.fail(std::format("read back {}; it should be {}", back, words));
}

struct Case {
    std::string_view name;
    void (*run)(Bench&);
};
constexpr std::array<Case, 18> all{{
    {"clear", clear},
    {"orientation", orientation},
    {"instancing", instancing},
    {"indexed", indexed},
    {"blending", blending},
    {"scissor", scissor},
    {"viewport", viewport},
    {"texture_nearest", texture_nearest},
    {"uniforms", uniforms},
    {"load_keeps", load_keeps},
    {"copy", copy},
    {"compute", compute},
    {"formats", formats},
    {"ordering", ordering},
    {"buffers", buffers},
    {"depth_test", depth_test},
    {"depth_kept", depth_kept},
    {"multisample", multisample},
}};

}  // namespace

std::vector<std::string_view> cases() {
    std::vector<std::string_view> out;
    for (const Case& c : all) out.push_back(c.name);
    return out;
}

ShaderFormats shader_formats() noexcept { return shaders::fill::fill_vs.formats(); }

std::vector<Outcome> run(Device& device) {
    std::vector<Outcome> out;
    Bench bench(device);
    if (bench.failed()) {
        for (const Case& c : all) out.push_back({std::string(c.name), false, "the suite's resources: " + bench.failure});
        return out;
    }
    for (const Case& c : all) {
        bench.failure.clear();
        c.run(bench);
        out.push_back({std::string(c.name), !bench.failed(), bench.failure});
    }
    return out;
}

}  // namespace omui::rhi::cts
