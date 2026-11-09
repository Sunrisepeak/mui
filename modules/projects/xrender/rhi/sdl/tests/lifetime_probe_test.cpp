// Narrow lifetime baseline for sanitizer/driver investigations. Run cases in
// separate processes by substring to distinguish device setup from shader JIT.
// No vector renderer, materials, UI or site code is involved.
import std;
import mlibs.rhi;
import xrender.rhi.sdl;
import mlibs.rhi.cts;
import mcpplibs.os;
import mcpplibs.mtest;

namespace sdl = xrender::rhi::sdl;
using namespace mcpplibs::mtest;
using namespace mlibs::rhi;
namespace {
std::unique_ptr<Device> device() {
    sdl::Options options; options.formats = cts::shader_formats();
    if (auto value = mcpplibs::os::env::get("OMUI_VULKAN_LIBRARY")) options.vulkan_library = *value;
    if (auto value = mcpplibs::os::env::get("OMUI_GPU_VIDEO_DRIVER")) options.video_driver = *value;
    auto result = sdl::create(options);
    if (!result) {
        if (mcpplibs::os::env::get("OMUI_REQUIRE_GPU") == "1") expect(false) << result.error().message;
        else std::println("no GPU device: {}", result.error().message);
        return {};
    }
    std::println("    device: {} ({}, software={})", (*result)->caps().device, name((*result)->caps().backend), (*result)->caps().software);
    return std::move(*result);
}
struct Vertex { float x, y, r, g, b, a; };
struct Instance { float x, y; };
void pipeline_lifetime(bool submit) {
    auto d = device(); if (!d) return;
    const std::array<VertexAttribute, 2> vertex{{{0, VertexFormat::float2, 0}, {1, VertexFormat::float4, 8}}};
    const std::array<VertexAttribute, 1> instance{{{2, VertexFormat::float2, 0}}};
    const std::array<VertexBufferLayout, 2> layouts{{{sizeof(Vertex), false, vertex}, {sizeof(Instance), true, instance}}};
    const auto pipeline = d->create(GraphicsPipelineDesc{.vertex = &cts::shaders::fill::fill_vs, .fragment = &cts::shaders::fill::fill_fs,
        .vertex_buffers = layouts, .blend = Blend::replace(), .label = "lifetime.one.pipeline"});
    expect(pipeline.has_value()) << (pipeline ? "" : pipeline.error().message); if (!pipeline) return;
    if (submit) {
        const auto target = d->create(TextureDesc{.width = 8, .height = 8, .usage = TextureUsage::color_target | TextureUsage::sampled});
        const auto vertices = d->create(BufferDesc{.size = sizeof(Vertex) * 3, .usage = BufferUsage::vertex});
        const auto instances = d->create(BufferDesc{.size = sizeof(Instance), .usage = BufferUsage::vertex});
        expect(target && vertices && instances); if (!target || !vertices || !instances) return;
        const std::array<Vertex, 3> points{{{-1, -1, 1, 0, 0, 1}, {1, -1, 1, 0, 0, 1}, {0, 1, 1, 0, 0, 1}}};
        const std::array<Instance, 1> offsets{{{0, 0}}};
        CommandList commands;
        commands.upload(*vertices, 0, std::span<const Vertex>(points));
        commands.upload(*instances, 0, std::span<const Instance>(offsets));
        commands.begin_render({*target, LoadOp::clear, {0, 0, 0, 0}});
        commands.set_pipeline(*pipeline); commands.bind_vertex_buffer(0, *vertices); commands.bind_vertex_buffer(1, *instances);
        commands.draw(3); commands.end_render();
        const auto submission = d->submit(commands); expect(submission.has_value());
        if (submission) expect(d->wait(*submission).has_value());
        const auto pixels = d->read(*target, {0, 0, 8, 8}); expect(pixels.has_value());
        if (pixels) expect(eq(pixels->size(), std::size_t{8 * 8 * 4}));
        d->destroy(*target); d->destroy(*vertices); d->destroy(*instances);
    }
    d->destroy(*pipeline); expect(d->wait_idle().has_value());
    d.reset();
    std::println("    one pipeline and device explicitly destroyed, submit={}", submit);
}
}
UTest cases{"rhi.sdl-lifetime-probe", [] {
    "no-device control"_test = [] { expect(true); };
    "device-only create wait destroy"_test = [] {
        auto d = device(); if (!d) return;
        expect(d->wait_idle().has_value()); d.reset();
        std::println("    device explicitly destroyed without creating any application pipeline");
    };
    "one-pipeline create destroy without submission"_test = [] { pipeline_lifetime(false); };
    "one-pipeline submit readback wait destroy"_test = [] { pipeline_lifetime(true); };
}};
