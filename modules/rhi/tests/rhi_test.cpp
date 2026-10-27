// openmui.rhi without a GPU: descriptions checked before a backend sees
// them, command lists recorded as data (payloads owned, hashed by content),
// and every kind of mistake in a list caught by validation with the command
// that made it — against a device that only remembers what it created.
import std;
import openmui.rhi;
import openmui.memory;
import openmui.testing;

using namespace omui::testing;
using namespace omui::rhi;

namespace {

// Remembers resources; runs nothing.
class Recorder final : public Device {
public:
    Caps caps_{.backend = Backend::vulkan, .device = "recorder", .formats = formats({ShaderFormat::spirv}), .max_texture_size = 4096};
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
    Result<Texture> make(const TextureDesc& d) override { return textures_.insert({d.format, d.width, d.height, d.usage, {}}); }
    Result<Sampler> make(const SamplerDesc& d) override { return samplers_.insert(d); }
    Result<GraphicsPipeline> make(const GraphicsPipelineDesc& d) override {
        return graphics_.insert({counts(*d.vertex), counts(*d.fragment), static_cast<std::uint32_t>(d.vertex_buffers.size()), d.target});
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
    omui::SlotMap<BufferDesc, BufferTag>                         buffers_;
    omui::SlotMap<TextureDesc, TextureTag>                       textures_;
    omui::SlotMap<SamplerDesc, SamplerTag>                       samplers_;
    omui::SlotMap<GraphicsPipelineInfo, GraphicsPipelineTag>     graphics_;
    omui::SlotMap<ComputePipelineInfo, ComputePipelineTag>       compute_;
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
    return std::format("{}: {}", name(r.error().code), r.error().message);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"rhi"};

    s.test("descriptions are checked before a backend sees them", [] {
        Recorder d;
        auto bad = [&](auto desc) {
            auto r = d.create(desc);
            return r ? std::string("<made>") : r.error().message;
        };
        check_eq(bad(BufferDesc{.size = 0}), std::string("a buffer of 0 bytes"));
        check_eq(bad(TextureDesc{.width = 5000, .height = 10}), std::string("a texture of 5000×10: this device's largest is 4096"));
        check_eq(bad(TextureDesc{.format = Format::bgra8_unorm, .width = 4, .height = 4, .usage = TextureUsage::compute_write}),
                 std::string("bgra8_unorm cannot be a storage texture (rgba8_unorm, rgba16_float, r32_float or r32_uint can)"));
        const ShaderDesc metal_vs{.stage = Stage::vertex, .entry = "vs", .code = msl_only};
        const ShaderDesc fs{.stage = Stage::fragment, .entry = "fs", .code = spirv};
        check_eq(bad(GraphicsPipelineDesc{.vertex = &metal_vs, .fragment = &fs}), std::string("the shader `vs` is compiled to msl; this device takes spirv"));
        check_eq(bad(GraphicsPipelineDesc{.vertex = &fs, .fragment = &fs}), std::string("`fs` is a fragment shader, not a vertex shader"));
        const ShaderDesc vs{.stage = Stage::vertex, .entry = "vs", .code = spirv};
        const std::array<VertexAttribute, 1> wide{VertexAttribute{0, VertexFormat::float4, 8}};
        const std::array<VertexBufferLayout, 1> layout{VertexBufferLayout{16, false, wide}};
        check_eq(bad(GraphicsPipelineDesc{.vertex = &vs, .fragment = &fs, .vertex_buffers = layout}),
                 std::string("the attribute at location 0 ends at byte 24, past the stride 16 of vertex buffer 0"));
        const ShaderDesc writes{.stage = Stage::fragment, .entry = "fs", .code = spirv, .rw_storage_buffers = 1};
        check_eq(bad(GraphicsPipelineDesc{.vertex = &vs, .fragment = &writes}),
                 std::string("the fragment shader `fs` declares read-write storage; only compute shaders write storage"));
        const ShaderDesc cs{.stage = Stage::compute, .entry = "cs", .code = spirv, .threads = {8, 0, 1}};
        check_eq(bad(ComputePipelineDesc{.shader = &cs}), std::string("the compute shader `cs` has a work group of 8×0×1"));
    });

    s.test("a list is data: payloads owned, hashed by content, cleared without freeing", [] {
        Scene sc;
        CommandList a, b;
        sc.frame(a);
        sc.frame(b);
        check_eq(a.size(), std::size_t{8});
        check_eq(command_name(a.commands()[6]), std::string_view("draw"));
        check(a.hash() == b.hash(), "the same frame, the same hash");
        std::vector<float> uniforms{1, 2, 3, 4};
        b.push_uniforms(Stage::fragment, 0, std::as_bytes(std::span(uniforms)));
        const auto before = b.hash();
        uniforms[0] = 99;  // the list kept its own copy
        check(b.hash() == before && a.hash() != before);
        b.clear();
        sc.frame(b);
        check(a.hash() == b.hash(), "cleared and recorded again");
        check(sc.d.submit(a).has_value(), "a valid frame is accepted");
        check_eq(sc.d.ran.size(), std::size_t{1});
    });

    s.test("validation names the command and what is wrong with it", [] {
        Scene sc;
        auto list = [&](auto&& record) {
            CommandList l;
            record(l);
            return invalid(sc, l);
        };
        check_eq(list([&](CommandList& l) { l.draw(3); }), std::string("invalid: command 0 (draw): only inside a render pass"));
        check_eq(list([&](CommandList& l) {
                     l.begin_render({sc.target});
                     l.draw(3);
                     l.end_render();
                 }),
                 std::string("invalid: command 1 (draw): no pipeline is set"));
        check_eq(list([&](CommandList& l) {
                     l.begin_render({sc.target});
                     l.set_pipeline(sc.pipeline);
                     l.bind_texture(Stage::fragment, 0, sc.image, sc.sampler);
                     l.draw(3);
                     l.end_render();
                 }),
                 std::string("invalid: command 3 (draw): the pipeline reads vertex buffer 0, and none is bound"));
        check_eq(list([&](CommandList& l) {
                     l.begin_render({sc.target});
                     l.set_pipeline(sc.pipeline);
                     l.bind_vertex_buffer(0, sc.quad);
                     l.draw(3);
                     l.end_render();
                 }),
                 std::string("invalid: command 3 (draw): the fragment shader reads a sampled texture at slot 0, and nothing is bound there"));
        check_eq(list([&](CommandList& l) { l.begin_render({sc.image}); l.end_render(); }),
                 std::string("invalid: command 0 (begin_render): the texture was not created for use as a colour target"));
        check_eq(list([&](CommandList& l) { l.begin_render({sc.target}); }), std::string("invalid: the list ends inside a render pass"));
        check_eq(list([&](CommandList& l) {
                     l.begin_render({sc.target});
                     l.begin_render({sc.target});
                 }),
                 std::string("invalid: command 1 (begin_render): a render pass inside a pass"));
        check_eq(list([&](CommandList& l) {
                     const std::array<std::byte, 100> bytes{};
                     l.upload(sc.quad, 0, bytes);
                 }),
                 std::string("invalid: command 0 (upload_buffer): bytes 0–100 of a buffer of 64"));
        check_eq(list([&](CommandList& l) {
                     const std::array<std::byte, 16> bytes{};
                     l.upload(sc.image, Region{0, 0, 2, 3}, bytes);
                 }),
                 std::string("invalid: command 0 (upload_texture): 16 bytes for a region that needs 24"));
        check_eq(list([&](CommandList& l) { l.push_uniforms(Stage::vertex, 0, std::span<const std::byte>(code.data(), 3)); }),
                 std::string("invalid: command 0 (push_uniforms): 3 bytes of uniforms (a multiple of 4)"));
        // A stale handle: destroyed, then named.
        const Buffer gone = *sc.d.create(BufferDesc{.size = 16, .usage = BufferUsage::vertex});
        sc.d.destroy(gone);
        check_eq(list([&](CommandList& l) {
                     const std::array<std::byte, 4> bytes{};
                     l.upload(gone, 0, bytes);
                 }),
                 std::string("invalid: command 0 (upload_buffer): a stale buffer handle"));
        // A pipeline for another target format.
        const Texture wide = *sc.d.create(TextureDesc{.format = Format::rgba16_float, .width = 4, .height = 4, .usage = TextureUsage::color_target});
        check_eq(list([&](CommandList& l) {
                     l.begin_render({wide});
                     l.set_pipeline(sc.pipeline);
                     l.end_render();
                 }),
                 std::string("invalid: command 1 (set_pipeline): the pipeline renders rgba8_unorm; the pass's target is rgba16_float"));
        check(sc.d.ran.empty(), "nothing invalid reached the backend");
    });

    s.test("compute passes: outputs declared, the shader's writes covered, reads bound", [] {
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
        check_eq(run([&](CommandList& l) {
                     const std::array<Texture, 1> writes{out};
                     l.begin_compute(writes);
                     l.set_pipeline(p);
                     l.bind_texture(Stage::compute, 0, in, smp);
                     l.dispatch(2, 2);
                     l.end_compute();
                 }),
                 std::string("<accepted>"));
        check_eq(run([&](CommandList& l) {
                     l.begin_compute({});
                     l.set_pipeline(p);
                     l.end_compute();
                 }),
                 std::string("command 1 (set_compute_pipeline): the shader writes 1 textures and 0 buffers; the pass named 0 and 0"));
        check_eq(run([&](CommandList& l) {
                     const std::array<Texture, 1> writes{in};
                     l.begin_compute(writes);
                     l.end_compute();
                 }),
                 std::string("command 0 (begin_compute): the texture was not created for use as a compute shader's output"));
        check_eq(run([&](CommandList& l) {
                     const std::array<Texture, 1> writes{out};
                     l.begin_compute(writes);
                     l.set_pipeline(p);
                     l.dispatch(1);
                     l.end_compute();
                 }),
                 std::string("command 2 (dispatch): the compute shader reads a sampled texture at slot 0, and nothing is bound there"));
    });

    s.test("readback is sized and bounded before the backend fetches", [] {
        Scene sc;
        auto pixels = sc.d.read(sc.target, Region{0, 0, 4, 2});
        check(pixels && pixels->size() == 32 && (*pixels)[0] == std::byte{7});
        check_eq(sc.d.read(sc.target, Region{60, 0, 8, 1}).error().message, std::string("read: the region 60+0 8×1 is not inside the 64×32 texture"));
        std::array<std::byte, 8> few{};
        check_eq(sc.d.read(sc.target, Region{0, 0, 4, 2}, few).error().message, std::string("read: 8 bytes asked for a region of 32"));
        std::array<std::byte, 16> b{};
        check(sc.d.read(sc.quad, 48, b).has_value() && b[0] == std::byte{9});
        check_eq(sc.d.read(sc.quad, 56, b).error().message, std::string("read: bytes 56–72 of a buffer of 64"));
    });

    return s.run(argc, argv);
}
