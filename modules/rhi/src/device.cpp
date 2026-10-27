module openmui.rhi;

import std;

namespace omui::rhi {

namespace {

bool inside(const Region& r, std::uint32_t width, std::uint32_t height) noexcept {
    return r.width > 0 && r.height > 0 && r.x <= width && r.y <= height && r.width <= width - r.x && r.height <= height - r.y;
}

// Slots bound so far, per stage and kind (SDL_GPU and its peers take at most a
// few dozen of each; 32 is the most a shader here may declare).
struct Bound {
    std::uint32_t samplers = 0, storage_textures = 0, storage_buffers = 0;
};

std::string missing(std::string_view what, std::uint32_t declared, std::uint32_t bound, std::string_view stage) {
    for (std::uint32_t i = 0; i < declared; ++i)
        if (!(bound >> i & 1u)) return std::format("the {} shader reads {} at slot {}, and nothing is bound there", stage, what, i);
    return {};
}

std::string covers(const Counts& c, const Bound& b, std::string_view stage) {
    if (c.samplers > 32 || c.storage_textures > 32 || c.storage_buffers > 32) return std::format("the {} shader declares more than 32 of a kind", stage);
    if (auto e = missing("a sampled texture", c.samplers, b.samplers, stage); !e.empty()) return e;
    if (auto e = missing("a storage texture", c.storage_textures, b.storage_textures, stage); !e.empty()) return e;
    return missing("a storage buffer", c.storage_buffers, b.storage_buffers, stage);
}

}  // namespace

Result<Buffer> Device::create(const BufferDesc& d) {
    if (auto e = check(d); !e.empty()) return fail(Errc::invalid, std::move(e));
    return make(d);
}

Result<Texture> Device::create(const TextureDesc& d) {
    if (auto e = check(d, caps()); !e.empty()) return fail(Errc::invalid, std::move(e));
    return make(d);
}

Result<GraphicsPipeline> Device::create(const GraphicsPipelineDesc& d) {
    if (auto e = check(d, caps().formats); !e.empty()) return fail(Errc::invalid, std::move(e));
    return make(d);
}

Result<ComputePipeline> Device::create(const ComputePipelineDesc& d) {
    if (!caps().compute) return fail(Errc::unsupported, std::format("{} has no compute shaders", caps().device));
    if (auto e = check(d, caps().formats); !e.empty()) return fail(Errc::invalid, std::move(e));
    return make(d);
}

Result<Submission> Device::submit(const CommandList& list) {
    if (auto e = validate(list, *this); !e.empty()) return fail(Errc::invalid, std::move(e));
    return run(list);
}

Result<void> Device::read(Texture t, Region region, std::span<std::byte> out) {
    const auto d = describe(t);
    if (!d) return fail(Errc::invalid, "read: a stale texture handle");
    if (!inside(region, d->width, d->height))
        return fail(Errc::invalid, std::format("read: the region {}+{} {}×{} is not inside the {}×{} texture", region.x, region.y,
                                               region.width, region.height, d->width, d->height));
    const std::size_t want = std::size_t{region.width} * region.height * bytes_per_pixel(d->format);
    if (out.size() != want) return fail(Errc::invalid, std::format("read: {} bytes asked for a region of {}", out.size(), want));
    return fetch(t, region, out);
}

Result<std::vector<std::byte>> Device::read(Texture t, Region region) {
    const auto d = describe(t);
    if (!d) return fail(Errc::invalid, "read: a stale texture handle");
    std::vector<std::byte> out(std::size_t{region.width} * region.height * bytes_per_pixel(d->format));
    if (auto r = read(t, region, out); !r) return std::unexpected(r.error());
    return out;
}

Result<bool> Device::present(Texture frame) {
    const auto d = describe(frame);
    if (!d) return fail(Errc::invalid, "present: a stale texture handle");
    if (d->format != Format::rgba8_unorm && d->format != Format::bgra8_unorm)
        return fail(Errc::invalid, std::format("present: a {} texture; a frame is rgba8_unorm or bgra8_unorm", name(d->format)));
    if (!has(d->usage, TextureUsage::sampled)) return fail(Errc::invalid, "present: the frame is not sampled (TextureUsage::sampled): the device reads it");
    return show(frame);
}

Result<bool> Device::show(Texture) { return fail(Errc::unsupported, "present: the device was made for no window"); }

Result<void> Device::read(Buffer b, std::uint32_t offset, std::span<std::byte> out) {
    const auto d = describe(b);
    if (!d) return fail(Errc::invalid, "read: a stale buffer handle");
    if (out.empty() || offset > d->size || out.size() > d->size - offset)
        return fail(Errc::invalid, std::format("read: bytes {}–{} of a buffer of {}", offset, offset + out.size(), d->size));
    return fetch(b, offset, out);
}

std::string validate(const CommandList& list, const Device& device) {
    enum class Pass : std::uint8_t { none, render, compute };
    Pass                                 pass = Pass::none;
    std::optional<TextureDesc>           target;
    std::optional<GraphicsPipelineInfo>  graphics;
    std::optional<ComputePipelineInfo>   compute;
    std::uint32_t                        vertex_buffers = 0;
    bool                                 index = false;
    std::array<Bound, 3>                 bound{};  // by Stage
    std::size_t                          compute_textures = 0, compute_buffers = 0;

    const auto commands = list.commands();
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const Command& c = commands[i];
        auto at = [&](std::string message) { return std::format("command {} ({}): {}", i, command_name(c), message); };
        auto outside = [&]() -> std::string { return pass == Pass::none ? std::string() : at("not allowed inside a pass"); };
        auto in = [&](Pass want) -> std::string {
            if (pass == want) return {};
            return at(want == Pass::render ? "only inside a render pass" : "only inside a compute pass");
        };
        auto texture = [&](Texture t, TextureUsage usage, std::string_view as) -> std::expected<TextureDesc, std::string> {
            auto d = device.describe(t);
            if (!d) return std::unexpected(at("a stale texture handle"));
            if (usage != TextureUsage::none && !has(d->usage, usage))
                return std::unexpected(at(std::format("the texture was not created for use as {}", as)));
            return *d;
        };
        auto buffer = [&](Buffer b, BufferUsage usage, std::string_view as) -> std::expected<BufferDesc, std::string> {
            auto d = device.describe(b);
            if (!d) return std::unexpected(at("a stale buffer handle"));
            if (usage != BufferUsage::none && !has(d->usage, usage))
                return std::unexpected(at(std::format("the buffer was not created for use as {}", as)));
            return *d;
        };
        auto stage_bound = [&](Stage s) -> Bound& { return bound[static_cast<std::size_t>(s)]; };
        auto stage_fits = [&](Stage s) -> std::string {
            if (s == Stage::compute) return in(Pass::compute);
            return in(Pass::render);
        };

        std::string problem = std::visit(
            [&]<class T>(const T& x) -> std::string {
                if constexpr (std::is_same_v<T, cmd::UploadBuffer>) {
                    if (auto e = outside(); !e.empty()) return e;
                    auto d = buffer(x.buffer, BufferUsage::none, "");
                    if (!d) return d.error();
                    if (x.data.empty()) return at("no bytes to upload");
                    if (x.offset > d->size || x.data.size() > d->size - x.offset)
                        return at(std::format("bytes {}–{} of a buffer of {}", x.offset, x.offset + x.data.size(), d->size));
                } else if constexpr (std::is_same_v<T, cmd::UploadTexture>) {
                    if (auto e = outside(); !e.empty()) return e;
                    auto d = texture(x.texture, TextureUsage::none, "");
                    if (!d) return d.error();
                    if (!inside(x.region, d->width, d->height)) return at("the region is not inside the texture");
                    const std::size_t row = std::size_t{x.region.width} * bytes_per_pixel(d->format);
                    const std::size_t pitch = x.row_bytes ? x.row_bytes : row;
                    if (pitch < row) return at(std::format("rows {} bytes apart, each {} long", pitch, row));
                    const std::size_t need = pitch * (x.region.height - 1) + row;
                    if (x.data.size() < need) return at(std::format("{} bytes for a region that needs {}", x.data.size(), need));
                } else if constexpr (std::is_same_v<T, cmd::CopyTexture>) {
                    if (auto e = outside(); !e.empty()) return e;
                    auto from = texture(x.from, TextureUsage::none, "");
                    if (!from) return from.error();
                    auto to = texture(x.to, TextureUsage::none, "");
                    if (!to) return to.error();
                    if (from->format != to->format) return at(std::format("from {} to {}", name(from->format), name(to->format)));
                    if (!inside(x.region, from->width, from->height)) return at("the region is not inside the source");
                    if (!inside({x.x, x.y, x.region.width, x.region.height}, to->width, to->height)) return at("the copy does not fit the destination");
                } else if constexpr (std::is_same_v<T, cmd::BeginRender>) {
                    if (auto e = outside(); !e.empty()) return at("a render pass inside a pass");
                    auto d = texture(x.target.texture, TextureUsage::color_target, "a colour target");
                    if (!d) return d.error();
                    pass = Pass::render;
                    target = *d;
                    graphics.reset();
                    vertex_buffers = 0;
                    index = false;
                    bound = {};
                } else if constexpr (std::is_same_v<T, cmd::EndRender>) {
                    if (auto e = in(Pass::render); !e.empty()) return e;
                    pass = Pass::none;
                } else if constexpr (std::is_same_v<T, cmd::SetPipeline>) {
                    if (auto e = in(Pass::render); !e.empty()) return e;
                    graphics = device.describe(x.pipeline);
                    if (!graphics) return at("a stale pipeline handle");
                    if (graphics->target != target->format)
                        return at(std::format("the pipeline renders {}; the pass's target is {}", name(graphics->target), name(target->format)));
                } else if constexpr (std::is_same_v<T, cmd::SetViewport>) {
                    if (auto e = in(Pass::render); !e.empty()) return e;
                    if (!(x.width > 0) || !(x.height > 0)) return at("an empty viewport");
                } else if constexpr (std::is_same_v<T, cmd::SetScissor>) {
                    if (auto e = in(Pass::render); !e.empty()) return e;
                    if (x.rect.x > target->width || x.rect.y > target->height) return at("the scissor starts outside the target");
                } else if constexpr (std::is_same_v<T, cmd::BindVertexBuffer>) {
                    if (auto e = in(Pass::render); !e.empty()) return e;
                    if (x.slot >= 16) return at("vertex buffer slots are 0–15");
                    auto d = buffer(x.buffer, BufferUsage::vertex, "vertices");
                    if (!d) return d.error();
                    vertex_buffers |= 1u << x.slot;
                } else if constexpr (std::is_same_v<T, cmd::BindIndexBuffer>) {
                    if (auto e = in(Pass::render); !e.empty()) return e;
                    auto d = buffer(x.buffer, BufferUsage::index, "indices");
                    if (!d) return d.error();
                    index = true;
                } else if constexpr (std::is_same_v<T, cmd::BindTexture>) {
                    if (auto e = stage_fits(x.stage); !e.empty()) return e;
                    if (x.slot >= 32) return at("slots are 0–31");
                    auto d = texture(x.texture, TextureUsage::sampled, "a sampled texture");
                    if (!d) return d.error();
                    if (!device.exists(x.sampler)) return at("a stale sampler handle");
                    stage_bound(x.stage).samplers |= 1u << x.slot;
                } else if constexpr (std::is_same_v<T, cmd::BindStorageTexture>) {
                    if (auto e = stage_fits(x.stage); !e.empty()) return e;
                    if (x.slot >= 32) return at("slots are 0–31");
                    const bool cs = x.stage == Stage::compute;
                    auto d = texture(x.texture, cs ? TextureUsage::compute_read : TextureUsage::storage_read,
                                     cs ? "compute storage" : "storage");
                    if (!d) return d.error();
                    stage_bound(x.stage).storage_textures |= 1u << x.slot;
                } else if constexpr (std::is_same_v<T, cmd::BindStorageBuffer>) {
                    if (auto e = stage_fits(x.stage); !e.empty()) return e;
                    if (x.slot >= 32) return at("slots are 0–31");
                    const bool cs = x.stage == Stage::compute;
                    auto d = buffer(x.buffer, cs ? BufferUsage::compute_read : BufferUsage::storage_read, cs ? "compute storage" : "storage");
                    if (!d) return d.error();
                    stage_bound(x.stage).storage_buffers |= 1u << x.slot;
                } else if constexpr (std::is_same_v<T, cmd::Draw> || std::is_same_v<T, cmd::DrawIndexed>) {
                    if (auto e = in(Pass::render); !e.empty()) return e;
                    if (!graphics) return at("no pipeline is set");
                    for (std::uint32_t s = 0; s < graphics->vertex_buffers; ++s)
                        if (!(vertex_buffers >> s & 1u)) return at(std::format("the pipeline reads vertex buffer {}, and none is bound", s));
                    if constexpr (std::is_same_v<T, cmd::DrawIndexed>) {
                        if (!index) return at("no index buffer is bound");
                        if (x.indices == 0) return at("no indices");
                    } else {
                        if (x.vertices == 0) return at("no vertices");
                    }
                    if (x.instances == 0) return at("no instances");
                    if (auto e = covers(graphics->vertex, stage_bound(Stage::vertex), "vertex"); !e.empty()) return at(e);
                    if (auto e = covers(graphics->fragment, stage_bound(Stage::fragment), "fragment"); !e.empty()) return at(e);
                } else if constexpr (std::is_same_v<T, cmd::BeginCompute>) {
                    if (auto e = outside(); !e.empty()) return at("a compute pass inside a pass");
                    for (Texture t : x.textures)
                        if (auto d = texture(t, TextureUsage::compute_write, "a compute shader's output"); !d) return d.error();
                    for (Buffer b : x.buffers)
                        if (auto d = buffer(b, BufferUsage::compute_write, "a compute shader's output"); !d) return d.error();
                    pass = Pass::compute;
                    compute.reset();
                    bound = {};
                    compute_textures = x.textures.size();
                    compute_buffers = x.buffers.size();
                } else if constexpr (std::is_same_v<T, cmd::EndCompute>) {
                    if (auto e = in(Pass::compute); !e.empty()) return e;
                    pass = Pass::none;
                } else if constexpr (std::is_same_v<T, cmd::SetComputePipeline>) {
                    if (auto e = in(Pass::compute); !e.empty()) return e;
                    compute = device.describe(x.pipeline);
                    if (!compute) return at("a stale pipeline handle");
                    if (compute->shader.rw_storage_textures > compute_textures || compute->shader.rw_storage_buffers > compute_buffers)
                        return at(std::format("the shader writes {} textures and {} buffers; the pass named {} and {}",
                                              compute->shader.rw_storage_textures, compute->shader.rw_storage_buffers, compute_textures,
                                              compute_buffers));
                } else if constexpr (std::is_same_v<T, cmd::Dispatch>) {
                    if (auto e = in(Pass::compute); !e.empty()) return e;
                    if (!compute) return at("no pipeline is set");
                    if (x.x == 0 || x.y == 0 || x.z == 0) return at("an empty dispatch");
                    if (auto e = covers(compute->shader, stage_bound(Stage::compute), "compute"); !e.empty()) return at(e);
                } else if constexpr (std::is_same_v<T, cmd::PushUniforms>) {
                    if (x.slot >= 4) return at("uniform slots are 0–3");
                    if (x.data.empty() || x.data.size() % 4) return at(std::format("{} bytes of uniforms (a multiple of 4)", x.data.size()));
                }
                return {};
            },
            c);
        if (!problem.empty()) return problem;
    }
    if (pass != Pass::none) return std::format("the list ends inside a {} pass", pass == Pass::render ? "render" : "compute");
    return {};
}

}  // namespace omui::rhi
