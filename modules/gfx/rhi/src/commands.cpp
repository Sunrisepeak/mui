module openmui.rhi;

import std;

namespace omui::rhi {

namespace {

constexpr std::array<std::string_view, std::variant_size_v<Command>> command_names{
    "upload_buffer", "upload_texture", "copy_texture", "begin_render", "end_render", "set_pipeline", "set_viewport",
    "set_scissor", "bind_vertex_buffer", "bind_index_buffer", "bind_texture", "bind_storage_texture", "bind_storage_buffer",
    "draw", "draw_indexed", "begin_compute", "end_compute", "set_compute_pipeline", "dispatch", "push_uniforms",
};

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;
    void bytes(const void* p, std::size_t n) noexcept {
        const auto* b = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 1099511628211ull;
        }
    }
    template <class T>
        requires std::is_trivially_copyable_v<T>
    void value(const T& v) noexcept {
        bytes(&v, sizeof v);
    }
    void span(std::span<const std::byte> s) noexcept {
        value(s.size());
        bytes(s.data(), s.size());
    }
};

// Field by field: padding never reaches the hash.
template <class Tag>
void handle(Fnv& f, Handle<Tag> h) noexcept {
    f.value(h.index);
    f.value(h.generation);
}
void region(Fnv& f, const Region& r) noexcept {
    f.value(r.x);
    f.value(r.y);
    f.value(r.width);
    f.value(r.height);
}

}  // namespace

std::string_view command_name(const Command& c) noexcept { return command_names[c.index()]; }

std::span<const std::byte> CommandList::own(std::span<const std::byte> bytes) { return arena_.copy(bytes); }

void CommandList::upload(Buffer buffer, std::uint32_t offset, std::span<const std::byte> data) {
    commands_.emplace_back(cmd::UploadBuffer{buffer, offset, own(data)});
}

void CommandList::upload(Texture texture, Region region, std::span<const std::byte> data, std::uint32_t row_bytes) {
    commands_.emplace_back(cmd::UploadTexture{texture, region, own(data), row_bytes});
}

void CommandList::upload_borrowed(Texture texture, Region region, std::span<const std::byte> data, std::uint32_t row_bytes) {
    commands_.emplace_back(cmd::UploadTexture{texture, region, data, row_bytes});
}

void CommandList::copy(Texture from, Region region, Texture to, std::uint32_t x, std::uint32_t y) {
    commands_.emplace_back(cmd::CopyTexture{from, region, to, x, y});
}

void CommandList::begin_render(const ColorTarget& target, const DepthTarget& depth) { commands_.emplace_back(cmd::BeginRender{target, depth}); }
void CommandList::end_render() { commands_.emplace_back(cmd::EndRender{}); }
void CommandList::set_pipeline(GraphicsPipeline pipeline) { commands_.emplace_back(cmd::SetPipeline{pipeline}); }
void CommandList::set_viewport(float x, float y, float width, float height) {
    commands_.emplace_back(cmd::SetViewport{x, y, width, height});
}
void CommandList::set_scissor(Region rect) { commands_.emplace_back(cmd::SetScissor{rect}); }
void CommandList::bind_vertex_buffer(std::uint32_t slot, Buffer buffer, std::uint32_t offset) {
    commands_.emplace_back(cmd::BindVertexBuffer{slot, buffer, offset});
}
void CommandList::bind_index_buffer(Buffer buffer, bool wide, std::uint32_t offset) {
    commands_.emplace_back(cmd::BindIndexBuffer{buffer, offset, wide});
}
void CommandList::bind_texture(Stage stage, std::uint32_t slot, Texture texture, Sampler sampler) {
    commands_.emplace_back(cmd::BindTexture{stage, slot, texture, sampler});
}
void CommandList::bind_storage(Stage stage, std::uint32_t slot, Texture texture) {
    commands_.emplace_back(cmd::BindStorageTexture{stage, slot, texture});
}
void CommandList::bind_storage(Stage stage, std::uint32_t slot, Buffer buffer) {
    commands_.emplace_back(cmd::BindStorageBuffer{stage, slot, buffer});
}
void CommandList::draw(std::uint32_t vertices, std::uint32_t instances, std::uint32_t first_vertex, std::uint32_t first_instance) {
    commands_.emplace_back(cmd::Draw{vertices, instances, first_vertex, first_instance});
}
void CommandList::draw_indexed(std::uint32_t indices, std::uint32_t instances, std::uint32_t first_index, std::int32_t vertex_offset,
                               std::uint32_t first_instance) {
    commands_.emplace_back(cmd::DrawIndexed{indices, instances, first_index, vertex_offset, first_instance});
}

void CommandList::begin_compute(std::span<const Texture> writes_textures, std::span<const Buffer> writes_buffers) {
    commands_.emplace_back(cmd::BeginCompute{arena_.copy(writes_textures), arena_.copy(writes_buffers)});
}
void CommandList::end_compute() { commands_.emplace_back(cmd::EndCompute{}); }
void CommandList::set_pipeline(ComputePipeline pipeline) { commands_.emplace_back(cmd::SetComputePipeline{pipeline}); }
void CommandList::dispatch(std::uint32_t x, std::uint32_t y, std::uint32_t z) { commands_.emplace_back(cmd::Dispatch{x, y, z}); }

void CommandList::push_uniforms(Stage stage, std::uint32_t slot, std::span<const std::byte> data) {
    commands_.emplace_back(cmd::PushUniforms{stage, slot, own(data)});
}

void CommandList::clear() noexcept {
    commands_.clear();
    arena_.reset();
}

std::uint64_t CommandList::hash() const noexcept {
    Fnv f;
    for (const Command& c : commands_) {
        f.value(c.index());
        std::visit(
            [&]<class T>(const T& x) {
                if constexpr (std::is_same_v<T, cmd::UploadBuffer>) {
                    handle(f, x.buffer), f.value(x.offset), f.span(x.data);
                } else if constexpr (std::is_same_v<T, cmd::UploadTexture>) {
                    handle(f, x.texture), region(f, x.region), f.span(x.data), f.value(x.row_bytes);
                } else if constexpr (std::is_same_v<T, cmd::CopyTexture>) {
                    handle(f, x.from), region(f, x.region), handle(f, x.to), f.value(x.x), f.value(x.y);
                } else if constexpr (std::is_same_v<T, cmd::BeginRender>) {
                    handle(f, x.target.texture), f.value(x.target.load), f.value(x.target.clear);
                    // Only what a pass has: a list without them hashes as it always did.
                    if (x.target.resolve) handle(f, x.target.resolve);
                    if (x.depth.texture)
                        handle(f, x.depth.texture), f.value(x.depth.load), f.value(x.depth.clear_depth), f.value(x.depth.clear_stencil),
                            f.value(x.depth.store);
                } else if constexpr (std::is_same_v<T, cmd::SetPipeline> || std::is_same_v<T, cmd::SetComputePipeline>) {
                    handle(f, x.pipeline);
                } else if constexpr (std::is_same_v<T, cmd::SetViewport>) {
                    f.value(x.x), f.value(x.y), f.value(x.width), f.value(x.height);
                } else if constexpr (std::is_same_v<T, cmd::SetScissor>) {
                    region(f, x.rect);
                } else if constexpr (std::is_same_v<T, cmd::BindVertexBuffer>) {
                    f.value(x.slot), handle(f, x.buffer), f.value(x.offset);
                } else if constexpr (std::is_same_v<T, cmd::BindIndexBuffer>) {
                    handle(f, x.buffer), f.value(x.offset), f.value(x.wide);
                } else if constexpr (std::is_same_v<T, cmd::BindTexture>) {
                    f.value(x.stage), f.value(x.slot), handle(f, x.texture), handle(f, x.sampler);
                } else if constexpr (std::is_same_v<T, cmd::BindStorageTexture>) {
                    f.value(x.stage), f.value(x.slot), handle(f, x.texture);
                } else if constexpr (std::is_same_v<T, cmd::BindStorageBuffer>) {
                    f.value(x.stage), f.value(x.slot), handle(f, x.buffer);
                } else if constexpr (std::is_same_v<T, cmd::Draw>) {
                    f.value(x.vertices), f.value(x.instances), f.value(x.first_vertex), f.value(x.first_instance);
                } else if constexpr (std::is_same_v<T, cmd::DrawIndexed>) {
                    f.value(x.indices), f.value(x.instances), f.value(x.first_index), f.value(x.vertex_offset), f.value(x.first_instance);
                } else if constexpr (std::is_same_v<T, cmd::BeginCompute>) {
                    f.value(x.textures.size());
                    for (Texture t : x.textures) handle(f, t);
                    f.value(x.buffers.size());
                    for (Buffer b : x.buffers) handle(f, b);
                } else if constexpr (std::is_same_v<T, cmd::Dispatch>) {
                    f.value(x.x), f.value(x.y), f.value(x.z);
                } else if constexpr (std::is_same_v<T, cmd::PushUniforms>) {
                    f.value(x.stage), f.value(x.slot), f.span(x.data);
                }
            },
            c);
    }
    return f.h;
}

}  // namespace omui::rhi
