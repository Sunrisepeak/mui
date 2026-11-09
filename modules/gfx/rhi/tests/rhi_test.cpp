// openmui.rhi without a GPU: descriptions checked before a backend sees
// them, command lists recorded as data (payloads owned, hashed by content),
// and every kind of mistake in a list caught by validation with the command
// that made it — against a device that only remembers what it created.
import std;
import mlibs.rhi;
import mlibs.memory;
import mlibs.testing;
import mcpplibs.mtest;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace mlibs::rhi;

namespace {

// Remembers resources; runs nothing.
class Recorder final : public Device {
public:
    Caps caps_{.backend = Backend::vulkan, .device = "recorder", .formats = formats({ShaderFormat::spirv}), .max_texture_size = 4096,
               .max_samples = 4, .depth = true};
    std::vector<std::uint64_t> ran;

    const Caps& caps() const noexcept override { return caps_; }
    void destroy(Buffer b) noexcept override { buffers_.erase(b); }
    void destroy(Texture t) noexcept override { textures_.erase(t); }
    void destroy(Sampler s) noexcept override { samplers_.erase(s); }
    void destroy(GraphicsPipeline p) noexcept override { graphics_.erase(p); }
    void destroy(ComputePipeline p) noexcept override { compute_.erase(p); }
    std::optional<BufferDesc> describe(Buffer b) const noexcept override {
        auto* d = buffers_.get(b);
        return d ? std::optional(*d) : std::nullopt;
    }
    std::optional<TextureDesc> describe(Texture t) const noexcept override {
        auto* d = textures_.get(t);
        return d ? std::optional(*d) : std::nullopt;
    }
    bool exists(Sampler s) const noexcept override { return samplers_.contains(s); }
    std::optional<GraphicsPipelineInfo> describe(GraphicsPipeline p) const noexcept override {
        auto* d = graphics_.get(p);
        return d ? std::optional(*d) : std::nullopt;
    }
    std::optional<ComputePipelineInfo> describe(ComputePipeline p) const noexcept override {
        auto* d = compute_.get(p);
        return d ? std::optional(*d) : std::nullopt;
    }
    Result<void> wait(Submission) override { return {}; }
    Result<void> wait_idle() override { return {}; }

protected:
    Result<Buffer> make(const BufferDesc& d) override { return buffers_.insert({d.size, d.usage, {}}); }
    Result<Texture> make(const TextureDesc& d) override { return textures_.insert({d.format, d.width, d.height, d.usage, {}, d.samples}); }
    Result<Sampler> make(const SamplerDesc& d) override { return samplers_.insert(d); }
    Result<GraphicsPipeline> make(const GraphicsPipelineDesc& d) override {
        return graphics_.insert(
            {counts(*d.vertex), counts(*d.fragment), static_cast<std::uint32_t>(d.vertex_buffers.size()), d.target, d.depth_target, d.samples});
    }
    Result<ComputePipeline> make(const ComputePipelineDesc& d) override { return compute_.insert({counts(*d.shader)}); }
    Result<Submission> run(const CommandList& list) override {
        ran.push_back(list.hash());
        return Submission{ran.size()};
    }
    Result<void> fetch(Texture, Region, std::span<std::byte> out) override {
        std::ranges::fill(out, std::byte{7});
        return {};
    }
    Result<void> fetch(Buffer, std::uint32_t, std::span<std::byte> out) override {
        std::ranges::fill(out, std::byte{9});
        return {};
    }

private:
    mlibs::memory::SlotMap<BufferDesc, BufferTag>                         buffers_;
    mlibs::memory::SlotMap<TextureDesc, TextureTag>                       textures_;
    mlibs::memory::SlotMap<SamplerDesc, SamplerTag>                       samplers_;
    mlibs::memory::SlotMap<GraphicsPipelineInfo, GraphicsPipelineTag>     graphics_;
    mlibs::memory::SlotMap<ComputePipelineInfo, ComputePipelineTag>       compute_;
};

constexpr std::array<std::byte, 4> code{std::byte{3}, std::byte{2}, std::byte{0x23}, std::byte{7}};
constexpr std::array<ShaderCode, 1> spirv{ShaderCode{ShaderFormat::spirv, code}};
constexpr std::array<ShaderCode, 1> msl_only{ShaderCode{ShaderFormat::msl, code}};

// The pieces of a textured-quad pipeline.
struct Scene {
    Recorder         d;
    ShaderDesc       vs{.stage = Stage::vertex, .entry = "vs_main", .code = spirv, .uniform_buffers = 1};
    ShaderDesc       fs{.stage = Stage::fragment, .entry = "fs_main", .code = spirv, .samplers = 1};
    std::array<VertexAttribute, 2> attrs{VertexAttribute{0, VertexFormat::float2, 0}, VertexAttribute{1, VertexFormat::float2, 8}};
    std::array<VertexBufferLayout, 1> layout{VertexBufferLayout{16, false, attrs}};
    Texture          target, image;
    Buffer           quad;
    Sampler          sampler;
    GraphicsPipeline pipeline;

    Scene() {
        target = *d.create(TextureDesc{.width = 64, .height = 32, .usage = TextureUsage::color_target});
        image = *d.create(TextureDesc{.width = 8, .height = 8, .usage = TextureUsage::sampled});
        quad = *d.create(BufferDesc{.size = 64, .usage = BufferUsage::vertex});
        sampler = *d.create(SamplerDesc{});
        pipeline = *d.create(GraphicsPipelineDesc{.vertex = &vs, .fragment = &fs, .vertex_buffers = layout});
    }

    // A complete, valid frame: upload, then a pass drawing the quad.
    void frame(CommandList& l) {
        const std::array<float, 16> vertices{};
        l.upload(quad, 0, std::span<const float>(vertices));
        l.begin_render({target, LoadOp::clear, {0, 0, 0, 1}});
        l.set_pipeline(pipeline);
        l.bind_vertex_buffer(0, quad);
        l.bind_texture(Stage::fragment, 0, image, sampler);
        l.push_uniforms(Stage::vertex, 0, std::array<float, 4>{1, 1, 1, 1});
        l.draw(4);
        l.end_render();
    }
};

std::string invalid(Scene& s, const CommandList& l) {
    auto r = s.d.submit(l);
    if (r) return "<accepted>";
    return r.error().text();
}

}  // namespace

UTest cases{"rhi", [] {

    "descriptions are checked before a backend sees them"_test = [] {
        Recorder d;
        auto bad = [&](auto desc) {
            auto r = d.create(desc);
            return r ? std::string("<made>") : r.error().message;
        };
        expect(eq(bad(BufferDesc{.size = 0}), std::string("a buffer of 0 bytes")));
        expect(eq(bad(TextureDesc{.width = 5000, .height = 10}), std::string("a texture of 5000×10: this device's largest is 4096")));
        expect(eq(bad(TextureDesc{.format = Format::bgra8_unorm, .width = 4, .height = 4, .usage = TextureUsage::compute_write}),
                 std::string("bgra8_unorm cannot be a storage texture (rgba8_unorm, rgba16_float, r32_float or r32_uint can)")));
        const ShaderDesc metal_vs{.stage = Stage::vertex, .entry = "vs", .code = msl_only};
        const ShaderDesc fs{.stage = Stage::fragment, .entry = "fs", .code = spirv};
        expect(eq(bad(GraphicsPipelineDesc{.vertex = &metal_vs, .fragment = &fs}), std::string("the shader `vs` is compiled to msl; this device takes spirv")));
        expect(eq(bad(GraphicsPipelineDesc{.vertex = &fs, .fragment = &fs}), std::string("`fs` is a fragment shader, not a vertex shader")));
        const ShaderDesc vs{.stage = Stage::vertex, .entry = "vs", .code = spirv};
        const std::array<VertexAttribute, 1> wide{VertexAttribute{0, VertexFormat::float4, 8}};
        const std::array<VertexBufferLayout, 1> layout{VertexBufferLayout{16, false, wide}};
        expect(eq(bad(GraphicsPipelineDesc{.vertex = &vs, .fragment = &fs, .vertex_buffers = layout}),
                 std::string("the attribute at location 0 ends at byte 24, past the stride 16 of vertex buffer 0")));
        const ShaderDesc writes{.stage = Stage::fragment, .entry = "fs", .code = spirv, .rw_storage_buffers = 1};
        expect(eq(bad(GraphicsPipelineDesc{.vertex = &vs, .fragment = &writes}),
                 std::string("the fragment shader `fs` declares read-write storage; only compute shaders write storage")));
        const ShaderDesc cs{.stage = Stage::compute, .entry = "cs", .code = spirv, .threads = {8, 0, 1}};
        expect(eq(bad(ComputePipelineDesc{.shader = &cs}), std::string("the compute shader `cs` has a work group of 8×0×1")));
    };

    "a list is data: payloads owned, hashed by content, cleared without freeing"_test = [] {
        Scene sc;
        CommandList a, b;
        sc.frame(a);
        sc.frame(b);
        expect(eq(a.size(), std::size_t{8}));
        expect(eq(command_name(a.commands()[6]), std::string_view("draw")));
        expect(a.hash() == b.hash()) << "the same frame, the same hash";
        std::vector<float> uniforms{1, 2, 3, 4};
        b.push_uniforms(Stage::fragment, 0, std::as_bytes(std::span(uniforms)));
        const auto before = b.hash();
        uniforms[0] = 99;  // the list kept its own copy
        expect(b.hash() == before && a.hash() != before);
        b.clear();
        sc.frame(b);
        expect(a.hash() == b.hash()) << "cleared and recorded again";
        expect(sc.d.submit(a).has_value()) << "a valid frame is accepted";
        expect(eq(sc.d.ran.size(), std::size_t{1}));
    };

    "validation names the command and what is wrong with it"_test = [] {
        Scene sc;
        auto list = [&](auto&& record) {
            CommandList l;
            record(l);
            return invalid(sc, l);
        };
        expect(eq(list([&](CommandList& l) { l.draw(3); }), std::string("rhi.invalid (invalid-argument): command 0 (draw): only inside a render pass")));
        expect(eq(list([&](CommandList& l) {
                     l.begin_render({sc.target});
                     l.draw(3);
                     l.end_render();
                 }),
                 std::string("rhi.invalid (invalid-argument): command 1 (draw): no pipeline is set")));
        expect(eq(list([&](CommandList& l) {
                     l.begin_render({sc.target});
                     l.set_pipeline(sc.pipeline);
                     l.bind_texture(Stage::fragment, 0, sc.image, sc.sampler);
                     l.draw(3);
                     l.end_render();
                 }),
                 std::string("rhi.invalid (invalid-argument): command 3 (draw): the pipeline reads vertex buffer 0, and none is bound")));
        expect(eq(list([&](CommandList& l) {
                     l.begin_render({sc.target});
                     l.set_pipeline(sc.pipeline);
                     l.bind_vertex_buffer(0, sc.quad);
                     l.draw(3);
                     l.end_render();
                 }),
                 std::string("rhi.invalid (invalid-argument): command 3 (draw): the fragment shader reads a sampled texture at slot 0, and nothing is bound there")));
        expect(eq(list([&](CommandList& l) { l.begin_render({sc.image}); l.end_render(); }),
                 std::string("rhi.invalid (invalid-argument): command 0 (begin_render): the texture was not created for use as a colour target")));
        expect(eq(list([&](CommandList& l) { l.begin_render({sc.target}); }), std::string("rhi.invalid (invalid-argument): the list ends inside a render pass")));
        expect(eq(list([&](CommandList& l) {
                     l.begin_render({sc.target});
                     l.begin_render({sc.target});
                 }),
                 std::string("rhi.invalid (invalid-argument): command 1 (begin_render): a render pass inside a pass")));
        expect(eq(list([&](CommandList& l) {
                     const std::array<std::byte, 100> bytes{};
                     l.upload(sc.quad, 0, bytes);
                 }),
                 std::string("rhi.invalid (invalid-argument): command 0 (upload_buffer): bytes 0–100 of a buffer of 64")));
        expect(eq(list([&](CommandList& l) {
                     const std::array<std::byte, 16> bytes{};
                     l.upload(sc.image, Region{0, 0, 2, 3}, bytes);
                 }),
                 std::string("rhi.invalid (invalid-argument): command 0 (upload_texture): 16 bytes for a region that needs 24")));
        expect(eq(list([&](CommandList& l) { l.push_uniforms(Stage::vertex, 0, std::span<const std::byte>(code.data(), 3)); }),
                 std::string("rhi.invalid (invalid-argument): command 0 (push_uniforms): 3 bytes of uniforms (a multiple of 4)")));
        // A stale handle: destroyed, then named.
        const Buffer gone = *sc.d.create(BufferDesc{.size = 16, .usage = BufferUsage::vertex});
        sc.d.destroy(gone);
        expect(eq(list([&](CommandList& l) {
                     const std::array<std::byte, 4> bytes{};
                     l.upload(gone, 0, bytes);
                 }),
                 std::string("rhi.invalid (invalid-argument): command 0 (upload_buffer): a stale buffer handle")));
        // A pipeline for another target format.
        const Texture wide = *sc.d.create(TextureDesc{.format = Format::rgba16_float, .width = 4, .height = 4, .usage = TextureUsage::color_target});
        expect(eq(list([&](CommandList& l) {
                     l.begin_render({wide});
                     l.set_pipeline(sc.pipeline);
                     l.end_render();
                 }),
                 std::string("rhi.invalid (invalid-argument): command 1 (set_pipeline): the pipeline renders rgba8_unorm; the pass's target is rgba16_float")));
        expect(sc.d.ran.empty()) << "nothing invalid reached the backend";
    };

    "depth and multisampling: formats, usages, samples, and passes that match their pipelines"_test = [] {
        Scene sc;
        auto bad = [&](auto desc) {
            auto r = sc.d.create(desc);
            return r ? std::string("<made>") : r.error().message;
        };
        using U = TextureUsage;
        expect(eq(bad(TextureDesc{.format = Format::d32_float, .width = 4, .height = 4, .usage = U::sampled}),
                 std::string("a d32_float texture is a depth target")));
        expect(eq(bad(TextureDesc{.format = Format::d32_float, .width = 4, .height = 4, .usage = U::depth_target | U::color_target}),
                 std::string("a d32_float texture is a depth target, and may be sampled; nothing else")));
        expect(eq(bad(TextureDesc{.format = Format::rgba8_unorm, .width = 4, .height = 4, .usage = U::depth_target}),
                 std::string("rgba8_unorm is not a depth format (d32_float, d32_float_s8)")));
        expect(eq(bad(TextureDesc{.width = 4, .height = 4, .usage = U::color_target, .samples = 3}), std::string("3 samples a texel (1, 2, 4 or 8)")));
        expect(eq(bad(TextureDesc{.width = 4, .height = 4, .usage = U::color_target, .samples = 8}),
                 std::string("8 samples a texel: this device multisamples to 4 at most")));
        expect(eq(bad(TextureDesc{.width = 4, .height = 4, .usage = U::color_target | U::sampled, .samples = 4}),
                 std::string("a multisampled texture is only a render target (resolve it into one that is sampled)")));
        const ShaderDesc vs{.stage = Stage::vertex, .entry = "vs", .code = spirv};
        const ShaderDesc fs{.stage = Stage::fragment, .entry = "fs", .code = spirv};
        expect(eq(bad(GraphicsPipelineDesc{.vertex = &vs, .fragment = &fs, .depth = {.test = true}}),
                 std::string("a depth test or write needs the pipeline's depth_target format")));
        expect(eq(bad(GraphicsPipelineDesc{.vertex = &vs, .fragment = &fs, .depth_target = Format::r32_float}),
                 std::string("r32_float is not a depth format (d32_float, d32_float_s8)")));

        const Texture z = *sc.d.create(TextureDesc{.format = Format::d32_float, .width = 64, .height = 32, .usage = U::depth_target});
        const Texture msaa = *sc.d.create(TextureDesc{.width = 64, .height = 32, .usage = U::color_target, .samples = 4});
        const Texture big = *sc.d.create(TextureDesc{.width = 128, .height = 64, .usage = U::color_target});
        const GraphicsPipeline deep = *sc.d.create(GraphicsPipelineDesc{.vertex = &vs, .fragment = &fs, .depth_target = Format::d32_float,
                                                                        .depth = {.test = true, .write = true}});
        auto list = [&](auto&& record) {
            CommandList l;
            record(l);
            return invalid(sc, l);
        };
        expect(eq(list([&](CommandList& l) {
                     l.begin_render({sc.target}, {z});
                     l.set_pipeline(sc.pipeline);
                     l.end_render();
                 }),
                 std::string("rhi.invalid (invalid-argument): command 1 (set_pipeline): the pipeline renders with no depth; the pass has d32_float")));
        expect(eq(list([&](CommandList& l) {
                     l.begin_render({sc.target});
                     l.set_pipeline(deep);
                     l.end_render();
                 }),
                 std::string("rhi.invalid (invalid-argument): command 1 (set_pipeline): the pipeline renders with d32_float; the pass has no depth target")));
        expect(eq(list([&](CommandList& l) {
                     l.begin_render({msaa, LoadOp::clear, {}, sc.target});
                     l.set_pipeline(sc.pipeline);
                     l.end_render();
                 }),
                 std::string("rhi.invalid (invalid-argument): command 1 (set_pipeline): the pipeline renders 1 samples a texel; the pass's target has 4")));
        expect(eq(list([&](CommandList& l) { l.begin_render({sc.target, LoadOp::clear, {}, big}); l.end_render(); }),
                 std::string("rhi.invalid (invalid-argument): command 0 (begin_render): a resolve target for a target that is not multisampled")));
        expect(eq(list([&](CommandList& l) { l.begin_render({msaa, LoadOp::clear, {}, big}); l.end_render(); }),
                 std::string("rhi.invalid (invalid-argument): command 0 (begin_render): resolving rgba8_unorm 64×32 into rgba8_unorm 128×64")));
        expect(eq(list([&](CommandList& l) { l.begin_render({big}, {z}); l.end_render(); }),
                 std::string("rhi.invalid (invalid-argument): command 0 (begin_render): a depth target of 64×32 (1 samples) for a colour target of 128×64 (1 samples)")));
        CommandList ok;
        ok.begin_render({sc.target}, {z, LoadOp::clear, 1.0f, 0, true});
        ok.set_pipeline(deep);
        ok.end_render();
        expect(sc.d.submit(ok).has_value()) << "a pass and its pipeline with the same depth";
        // A pass without depth or resolve hashes as it always did.
        CommandList plain, again;
        plain.begin_render({sc.target});
        again.begin_render({sc.target}, {});
        expect(plain.hash() == again.hash());
    };

    "compute passes: outputs declared, the shader's writes covered, reads bound"_test = [] {
        Recorder d;
        const ShaderDesc cs{.stage = Stage::compute, .entry = "blur", .code = spirv, .samplers = 1, .rw_storage_textures = 1, .threads = {8, 8, 1}};
        const ComputePipeline p = *d.create(ComputePipelineDesc{.shader = &cs});
        const Texture out = *d.create(TextureDesc{.width = 16, .height = 16, .usage = TextureUsage::compute_write | TextureUsage::sampled});
        const Texture in = *d.create(TextureDesc{.width = 16, .height = 16, .usage = TextureUsage::sampled});
        const Sampler smp = *d.create(SamplerDesc{});
        auto run = [&](auto&& record) {
            CommandList l;
            record(l);
            auto r = d.submit(l);
            return r ? std::string("<accepted>") : r.error().message;
        };
        expect(eq(run([&](CommandList& l) {
                     const std::array<Texture, 1> writes{out};
                     l.begin_compute(writes);
                     l.set_pipeline(p);
                     l.bind_texture(Stage::compute, 0, in, smp);
                     l.dispatch(2, 2);
                     l.end_compute();
                 }),
                 std::string("<accepted>")));
        expect(eq(run([&](CommandList& l) {
                     l.begin_compute({});
                     l.set_pipeline(p);
                     l.end_compute();
                 }),
                 std::string("command 1 (set_compute_pipeline): the shader writes 1 textures and 0 buffers; the pass named 0 and 0")));
        expect(eq(run([&](CommandList& l) {
                     const std::array<Texture, 1> writes{in};
                     l.begin_compute(writes);
                     l.end_compute();
                 }),
                 std::string("command 0 (begin_compute): the texture was not created for use as a compute shader's output")));
        expect(eq(run([&](CommandList& l) {
                     const std::array<Texture, 1> writes{out};
                     l.begin_compute(writes);
                     l.set_pipeline(p);
                     l.dispatch(1);
                     l.end_compute();
                 }),
                 std::string("command 2 (dispatch): the compute shader reads a sampled texture at slot 0, and nothing is bound there")));
    };

    "readback is sized and bounded before the backend fetches"_test = [] {
        Scene sc;
        auto pixels = sc.d.read(sc.target, Region{0, 0, 4, 2});
        expect(pixels && pixels->size() == 32 && (*pixels)[0] == std::byte{7});
        expect(eq(sc.d.read(sc.target, Region{60, 0, 8, 1}).error().message, std::string("read: the region 60+0 8×1 is not inside the 64×32 texture")));
        std::array<std::byte, 8> few{};
        expect(eq(sc.d.read(sc.target, Region{0, 0, 4, 2}, few).error().message, std::string("read: 8 bytes asked for a region of 32")));
        std::array<std::byte, 16> b{};
        expect(sc.d.read(sc.quad, 48, b).has_value() && b[0] == std::byte{9});
        expect(eq(sc.d.read(sc.quad, 56, b).error().message, std::string("read: bytes 56–72 of a buffer of 64")));
    };

    "present: a sampled rgba8 or bgra8 frame, to a device made for a window"_test = [] {
        Scene sc;
        auto frame = sc.d.create(TextureDesc{Format::rgba8_unorm, 8, 8, TextureUsage::color_target | TextureUsage::sampled, "frame"});
        auto unsampled = sc.d.create(TextureDesc{Format::rgba8_unorm, 8, 8, TextureUsage::color_target, "unsampled"});
        auto r8 = sc.d.create(TextureDesc{Format::r8_unorm, 8, 8, TextureUsage::sampled, "mask"});
        expect(frame && unsampled && r8);
        if (!frame || !unsampled || !r8) return;
        expect(eq(sc.d.present(*unsampled).error().message,
                 std::string("present: the frame is not sampled (TextureUsage::sampled): the device reads it")));
        expect(eq(sc.d.present(*r8).error().message, std::string("present: a r8_unorm texture; a frame is rgba8_unorm or bgra8_unorm")));
        sc.d.destroy(*unsampled);
        expect(eq(sc.d.present(*unsampled).error().message, std::string("present: a stale texture handle")));
        // A device made for no window says so; the backend's own show() is not reached otherwise.
        const auto none = sc.d.present(*frame);
        expect(!none && none.error().code == Errc::unsupported) << (none ? "" : none.error().message);
    };

    "the wire: a list as words, each command its op, its length and its fields, payloads where they lie; a pipeline's state"_test = [] {
        Scene sc;
        CommandList l;
        sc.frame(l);
        const std::array<Texture, 1> writes{sc.image};
        l.begin_compute(writes);
        l.dispatch(2, 3);
        l.end_compute();
        std::vector<std::uint32_t> words;
        wire::encode(l, words);
        // Walk it: every header's length lands on the next header.
        std::vector<wire::Op> ops;
        std::vector<std::size_t> at;
        for (std::size_t i = 0; i < words.size(); i += 1 + wire::length(words[i])) {
            ops.push_back(wire::op(words[i]));
            at.push_back(i);
        }
        using enum wire::Op;
        expect(ops == std::vector{upload_buffer, begin_render, set_pipeline, bind_vertex_buffer, bind_texture, push_uniforms, draw, end_render,
                                 begin_compute, dispatch, end_compute}) << std::format("{} commands", ops.size());
        if (ops.size() != 11) return;
        auto field = [&](std::size_t command, std::size_t k) { return words[at[command] + 1 + k]; };
        // upload_buffer: buffer, offset, the payload's address and length — the list's own copy.
        const auto& up = std::get<cmd::UploadBuffer>(l.commands()[0]);
        expect(eq(field(0, 0), sc.quad.index));
        expect(eq(field(0, 1), 0u));
        const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(up.data.data()));
        expect(field(0, 2) == static_cast<std::uint32_t>(address) && field(0, 3) == static_cast<std::uint32_t>(address >> 32)) << "the address";
        expect(eq(field(0, 4), 64u));
        // begin_render: the clear colour as floats, no resolve, no depth.
        expect(eq(wire::length(words[at[1]]), 12u));
        expect(eq(field(1, 0), sc.target.index));
        expect(eq(field(1, 1), static_cast<std::uint32_t>(LoadOp::clear)));
        expect(eq(std::bit_cast<float>(field(1, 5)), 1.0f));
        expect(eq(field(1, 6), wire::none));
        expect(eq(field(1, 7), wire::none));
        expect(eq(field(1, 9), std::bit_cast<std::uint32_t>(1.0f)));
        // bind_texture: stage, slot, texture, sampler.
        expect(field(4, 0) == static_cast<std::uint32_t>(Stage::fragment) && field(4, 1) == 0 && field(4, 2) == sc.image.index &&
              field(4, 3) == sc.sampler.index);
        // push_uniforms: 16 bytes for the vertex stage's slot 0.
        expect(field(5, 0) == static_cast<std::uint32_t>(Stage::vertex) && field(5, 1) == 0 && field(5, 4) == 16);
        expect(field(6, 0) == 4 && field(6, 1) == 1 && field(6, 2) == 0 && field(6, 3) == 0) << "draw(4)";
        // begin_compute: one texture, no buffers.
        expect(wire::length(words[at[8]]) == 3 && field(8, 0) == 1 && field(8, 1) == sc.image.index && field(8, 2) == 0);
        expect(field(9, 0) == 2 && field(9, 1) == 3 && field(9, 2) == 1) << "dispatch(2, 3)";
        // Appended, the capacity kept.
        const std::size_t n = words.size();
        words.clear();
        wire::encode(l, words);
        expect(eq(words.size(), n));
        // A pipeline: the stages' counts, the target and blend, topology, depth, samples, the vertex layout.
        std::vector<std::uint32_t> p;
        wire::encode(GraphicsPipelineDesc{.vertex = &sc.vs, .fragment = &sc.fs, .vertex_buffers = sc.layout, .depth_target = Format::d32_float,
                                          .depth = {.test = true, .write = true, .compare = Compare::less}},
                     p);
        const std::vector<std::uint32_t> want{0, 0, 0, 1,  1, 0, 0, 0,  static_cast<std::uint32_t>(Format::rgba8_unorm), 1, 1, 7, 0, 1, 7, 0,
                                              0, static_cast<std::uint32_t>(Format::d32_float), 1, 1, 1, 1,
                                              1, 16, 0, 2, 0, 1, 0, 1, 1, 8};
        expect(p == want) << std::format("{}", p);
        p.clear();
        const ShaderDesc cs{.stage = Stage::compute, .entry = "cs", .code = spirv, .uniform_buffers = 1, .rw_storage_textures = 2};
        wire::encode(ComputePipelineDesc{&cs}, p);
        expect(p == std::vector<std::uint32_t>{0, 0, 0, 1, 2, 0}) << std::format("{}", p);
    };
}};
