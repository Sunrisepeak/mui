// openmui.rhi:commands — a frame's GPU work, recorded as data.
//
// A CommandList is what the renderer hands a device: uploads, render passes
// with their draws, compute passes with their dispatches, copies. Recording
// is plain appends (no virtual call per draw: AGENTS.md "热路径没有虚函数");
// the backend translates the whole list in one Device::submit. The bytes a
// command carries (uploads, uniforms) are copied into the list's arena, so a
// list of the same shape as the last allocates nothing, and two lists can be
// compared and hashed — what tests and the trace read.
export module openmui.rhi:commands;

import std;
import openmui.memory;
import :types;

export namespace omui::rhi {

enum class LoadOp : std::uint8_t { clear, load, dont_care };

struct ColorTarget {
    Texture              texture;
    LoadOp               load = LoadOp::clear;
    std::array<float, 4> clear{0, 0, 0, 0};  // premultiplied RGBA, 0–1
};

namespace cmd {

// Outside passes.
struct UploadBuffer  { Buffer buffer; std::uint32_t offset = 0; std::span<const std::byte> data; };
// Rows of `region` from `data`, `row_bytes` apart (0: tightly packed).
struct UploadTexture { Texture texture; Region region; std::span<const std::byte> data; std::uint32_t row_bytes = 0; };
struct CopyTexture   { Texture from; Region region; Texture to; std::uint32_t x = 0, y = 0; };

// Render passes.
struct BeginRender   { ColorTarget target; };
struct EndRender     {};
struct SetPipeline   { GraphicsPipeline pipeline; };
struct SetViewport   { float x = 0, y = 0, width = 0, height = 0; };
struct SetScissor    { Region rect; };
struct BindVertexBuffer { std::uint32_t slot = 0; Buffer buffer; std::uint32_t offset = 0; };
struct BindIndexBuffer  { Buffer buffer; std::uint32_t offset = 0; bool wide = false; };  // wide: 32-bit indices
// A sampled texture at `slot` of the stage (vertex, fragment).
struct BindTexture   { Stage stage = Stage::fragment; std::uint32_t slot = 0; Texture texture; Sampler sampler; };
// Read-only storage at `slot` of the stage (any stage).
struct BindStorageTexture { Stage stage = Stage::fragment; std::uint32_t slot = 0; Texture texture; };
struct BindStorageBuffer  { Stage stage = Stage::fragment; std::uint32_t slot = 0; Buffer buffer; };
struct Draw          { std::uint32_t vertices = 0, instances = 1, first_vertex = 0, first_instance = 0; };
struct DrawIndexed   { std::uint32_t indices = 0, instances = 1, first_index = 0; std::int32_t vertex_offset = 0; std::uint32_t first_instance = 0; };

// Compute passes: what they write is named when they begin.
struct BeginCompute  { std::span<const Texture> textures; std::span<const Buffer> buffers; };
struct EndCompute    {};
struct SetComputePipeline { ComputePipeline pipeline; };
struct Dispatch      { std::uint32_t x = 1, y = 1, z = 1; };

// Anywhere: uniform data for `slot` of the stage, from here on.
struct PushUniforms  { Stage stage = Stage::fragment; std::uint32_t slot = 0; std::span<const std::byte> data; };

}  // namespace cmd

using Command = std::variant<cmd::UploadBuffer, cmd::UploadTexture, cmd::CopyTexture, cmd::BeginRender, cmd::EndRender,
                             cmd::SetPipeline, cmd::SetViewport, cmd::SetScissor, cmd::BindVertexBuffer, cmd::BindIndexBuffer,
                             cmd::BindTexture, cmd::BindStorageTexture, cmd::BindStorageBuffer, cmd::Draw, cmd::DrawIndexed,
                             cmd::BeginCompute, cmd::EndCompute, cmd::SetComputePipeline, cmd::Dispatch, cmd::PushUniforms>;

// "upload_buffer", "draw" …: a command's kind, as the trace and errors say it.
[[nodiscard]] std::string_view command_name(const Command& c) noexcept;

class CommandList {
public:
    CommandList() = default;
    CommandList(const CommandList&) = delete;
    CommandList& operator=(const CommandList&) = delete;
    CommandList(CommandList&&) noexcept = default;
    CommandList& operator=(CommandList&&) noexcept = default;

    void upload(Buffer buffer, std::uint32_t offset, std::span<const std::byte> data);
    void upload(Texture texture, Region region, std::span<const std::byte> data, std::uint32_t row_bytes = 0);
    // As upload, the bytes not copied into the list: read where they are when
    // the list is submitted, so they must stay as they are until then (an
    // image's pixels the caller holds through the submission — megabytes a
    // frame not copied twice).
    void upload_borrowed(Texture texture, Region region, std::span<const std::byte> data, std::uint32_t row_bytes = 0);
    template <class T>
        requires std::is_trivially_copyable_v<T>
    void upload(Buffer buffer, std::uint32_t offset, std::span<const T> data) {
        upload(buffer, offset, std::as_bytes(data));
    }
    void copy(Texture from, Region region, Texture to, std::uint32_t x = 0, std::uint32_t y = 0);

    void begin_render(const ColorTarget& target);
    void end_render();
    void set_pipeline(GraphicsPipeline pipeline);
    void set_viewport(float x, float y, float width, float height);
    void set_scissor(Region rect);
    void bind_vertex_buffer(std::uint32_t slot, Buffer buffer, std::uint32_t offset = 0);
    void bind_index_buffer(Buffer buffer, bool wide, std::uint32_t offset = 0);
    void bind_texture(Stage stage, std::uint32_t slot, Texture texture, Sampler sampler);
    void bind_storage(Stage stage, std::uint32_t slot, Texture texture);
    void bind_storage(Stage stage, std::uint32_t slot, Buffer buffer);
    void draw(std::uint32_t vertices, std::uint32_t instances = 1, std::uint32_t first_vertex = 0, std::uint32_t first_instance = 0);
    void draw_indexed(std::uint32_t indices, std::uint32_t instances = 1, std::uint32_t first_index = 0, std::int32_t vertex_offset = 0,
                      std::uint32_t first_instance = 0);

    void begin_compute(std::span<const Texture> writes_textures, std::span<const Buffer> writes_buffers = {});
    void end_compute();
    void set_pipeline(ComputePipeline pipeline);
    void dispatch(std::uint32_t x, std::uint32_t y = 1, std::uint32_t z = 1);

    void push_uniforms(Stage stage, std::uint32_t slot, std::span<const std::byte> data);
    template <class T>
        requires std::is_trivially_copyable_v<T>
    void push_uniforms(Stage stage, std::uint32_t slot, const T& value) {
        push_uniforms(stage, slot, std::as_bytes(std::span<const T>(&value, 1)));
    }

    [[nodiscard]] std::span<const Command> commands() const noexcept { return commands_; }
    [[nodiscard]] std::size_t size() const noexcept { return commands_.size(); }
    [[nodiscard]] bool empty() const noexcept { return commands_.empty(); }
    // FNV-1a of the content (payload bytes included, never addresses).
    [[nodiscard]] std::uint64_t hash() const noexcept;
    // Forgets the commands; keeps the arena's blocks.
    void clear() noexcept;

private:
    std::span<const std::byte> own(std::span<const std::byte> bytes);

    std::vector<Command> commands_;
    Arena                arena_{16 * 1024};
};

}  // namespace omui::rhi
