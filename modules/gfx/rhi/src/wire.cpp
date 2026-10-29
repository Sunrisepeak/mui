module openmui.rhi;

import std;

namespace omui::rhi::wire {

namespace {

static_assert(std::variant_size_v<Command> == op_count, "an Op for every command");
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(Op::push_uniforms), Command>, cmd::PushUniforms>);
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(Op::begin_compute), Command>, cmd::BeginCompute>);
static_assert(std::is_same_v<std::variant_alternative_t<static_cast<std::size_t>(Op::draw_indexed), Command>, cmd::DrawIndexed>);

struct Writer {
    std::vector<std::uint32_t>& out;
    std::size_t                 header = 0;

    void begin(Op o) {
        header = out.size();
        out.push_back(static_cast<std::uint32_t>(o));
    }
    void end() { out[header] |= static_cast<std::uint32_t>(out.size() - header - 1) << 8; }
    void u(std::uint32_t v) { out.push_back(v); }
    void i(std::int32_t v) { out.push_back(std::bit_cast<std::uint32_t>(v)); }
    void f(float v) { out.push_back(std::bit_cast<std::uint32_t>(v)); }
    template <class Tag>
    void h(Handle<Tag> handle) {
        out.push_back(handle ? handle.index : none);
    }
    void payload(std::span<const std::byte> bytes) {
        const auto address = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(bytes.data()));
        u(static_cast<std::uint32_t>(address));
        u(static_cast<std::uint32_t>(address >> 32));
        u(static_cast<std::uint32_t>(bytes.size()));
    }
    void region(const Region& r) {
        u(r.x);
        u(r.y);
        u(r.width);
        u(r.height);
    }
};

}  // namespace

void encode(const CommandList& list, std::vector<std::uint32_t>& out) {
    Writer w{out};
    for (const Command& c : list.commands()) {
        w.begin(static_cast<Op>(c.index()));
        std::visit(
            [&]<class T>(const T& x) {
                if constexpr (std::is_same_v<T, cmd::UploadBuffer>) {
                    w.h(x.buffer), w.u(x.offset), w.payload(x.data);
                } else if constexpr (std::is_same_v<T, cmd::UploadTexture>) {
                    w.h(x.texture), w.region(x.region), w.payload(x.data), w.u(x.row_bytes);
                } else if constexpr (std::is_same_v<T, cmd::CopyTexture>) {
                    w.h(x.from), w.region(x.region), w.h(x.to), w.u(x.x), w.u(x.y);
                } else if constexpr (std::is_same_v<T, cmd::BeginRender>) {
                    w.h(x.target.texture), w.u(static_cast<std::uint32_t>(x.target.load));
                    for (float v : x.target.clear) w.f(v);
                    w.h(x.target.resolve);
                    w.h(x.depth.texture), w.u(static_cast<std::uint32_t>(x.depth.load)), w.f(x.depth.clear_depth);
                    w.u(x.depth.clear_stencil), w.u(x.depth.store ? 1u : 0u);
                } else if constexpr (std::is_same_v<T, cmd::SetPipeline>) {
                    w.h(x.pipeline);
                } else if constexpr (std::is_same_v<T, cmd::SetViewport>) {
                    w.f(x.x), w.f(x.y), w.f(x.width), w.f(x.height);
                } else if constexpr (std::is_same_v<T, cmd::SetScissor>) {
                    w.region(x.rect);
                } else if constexpr (std::is_same_v<T, cmd::BindVertexBuffer>) {
                    w.u(x.slot), w.h(x.buffer), w.u(x.offset);
                } else if constexpr (std::is_same_v<T, cmd::BindIndexBuffer>) {
                    w.h(x.buffer), w.u(x.offset), w.u(x.wide ? 1u : 0u);
                } else if constexpr (std::is_same_v<T, cmd::BindTexture>) {
                    w.u(static_cast<std::uint32_t>(x.stage)), w.u(x.slot), w.h(x.texture), w.h(x.sampler);
                } else if constexpr (std::is_same_v<T, cmd::BindStorageTexture>) {
                    w.u(static_cast<std::uint32_t>(x.stage)), w.u(x.slot), w.h(x.texture);
                } else if constexpr (std::is_same_v<T, cmd::BindStorageBuffer>) {
                    w.u(static_cast<std::uint32_t>(x.stage)), w.u(x.slot), w.h(x.buffer);
                } else if constexpr (std::is_same_v<T, cmd::Draw>) {
                    w.u(x.vertices), w.u(x.instances), w.u(x.first_vertex), w.u(x.first_instance);
                } else if constexpr (std::is_same_v<T, cmd::DrawIndexed>) {
                    w.u(x.indices), w.u(x.instances), w.u(x.first_index), w.i(x.vertex_offset), w.u(x.first_instance);
                } else if constexpr (std::is_same_v<T, cmd::BeginCompute>) {
                    w.u(static_cast<std::uint32_t>(x.textures.size()));
                    for (Texture t : x.textures) w.h(t);
                    w.u(static_cast<std::uint32_t>(x.buffers.size()));
                    for (Buffer b : x.buffers) w.h(b);
                } else if constexpr (std::is_same_v<T, cmd::SetComputePipeline>) {
                    w.h(x.pipeline);
                } else if constexpr (std::is_same_v<T, cmd::Dispatch>) {
                    w.u(x.x), w.u(x.y), w.u(x.z);
                } else if constexpr (std::is_same_v<T, cmd::PushUniforms>) {
                    w.u(static_cast<std::uint32_t>(x.stage)), w.u(x.slot), w.payload(x.data);
                }
            },
            c);
        w.end();
    }
}

void encode(const GraphicsPipelineDesc& d, std::vector<std::uint32_t>& w) {
    auto u = [](auto e) { return static_cast<std::uint32_t>(e); };
    for (const ShaderDesc* s : {d.vertex, d.fragment}) w.insert(w.end(), {s->samplers, s->storage_textures, s->storage_buffers, s->uniform_buffers});
    w.insert(w.end(), {u(d.target), d.blend.enabled ? 1u : 0u, u(d.blend.src_color), u(d.blend.dst_color), u(d.blend.color_op),
                       u(d.blend.src_alpha), u(d.blend.dst_alpha), u(d.blend.alpha_op)});
    w.insert(w.end(), {u(d.topology), d.depth_target ? u(*d.depth_target) : none, d.depth.test ? 1u : 0u, d.depth.write ? 1u : 0u,
                       u(d.depth.compare), d.samples});
    w.push_back(static_cast<std::uint32_t>(d.vertex_buffers.size()));
    for (const VertexBufferLayout& b : d.vertex_buffers) {
        w.insert(w.end(), {b.stride, b.per_instance ? 1u : 0u, static_cast<std::uint32_t>(b.attributes.size())});
        for (const VertexAttribute& a : b.attributes) w.insert(w.end(), {a.location, u(a.format), a.offset});
    }
}

void encode(const ComputePipelineDesc& d, std::vector<std::uint32_t>& w) {
    const ShaderDesc& s = *d.shader;
    w.insert(w.end(), {s.samplers, s.storage_textures, s.storage_buffers, s.uniform_buffers, s.rw_storage_textures, s.rw_storage_buffers});
}

}  // namespace omui::rhi::wire
