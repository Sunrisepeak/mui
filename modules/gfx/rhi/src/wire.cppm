// openmui.rhi:wire — a command list as 32-bit words, for a device on the
// other side of a boundary: a page's WebGPU or WebGL2 (rhi.webgpu,
// rhi.webgl), where one call carries a whole submission out of WebAssembly
// instead of one call a draw.
//
// Each command is a header word — its Op in the low 8 bits, the number of
// words that follow in the rest — then its fields in the order below.
// Handles are their slot index (none: 0xffffffff), floats their IEEE bits,
// a stage its Stage, a load its LoadOp, flags 0 or 1. A payload (an upload's
// or the uniforms' bytes) is where it lies in this process's memory: its
// address as two words (low, high), then its length; it is read in place,
// while the list lives.
//
//   upload_buffer         buffer offset addr.lo addr.hi length
//   upload_texture        texture x y width height addr.lo addr.hi length row_bytes (0: tight)
//   copy_texture          from x y width height to to.x to.y
//   begin_render          texture load clear.r clear.g clear.b clear.a resolve
//                         depth depth.load clear_depth clear_stencil store
//   end_render
//   set_pipeline          pipeline
//   set_viewport          x y width height (floats)
//   set_scissor           x y width height
//   bind_vertex_buffer    slot buffer offset
//   bind_index_buffer     buffer offset wide
//   bind_texture          stage slot texture sampler
//   bind_storage_texture  stage slot texture
//   bind_storage_buffer   stage slot buffer
//   draw                  vertices instances first_vertex first_instance
//   draw_indexed          indices instances first_index vertex_offset (int) first_instance
//   begin_compute         n textures… m buffers…
//   end_compute
//   set_compute_pipeline  pipeline
//   dispatch              x y z
//   push_uniforms         stage slot addr.lo addr.hi length
//
// A pipeline crosses the same way, its state as words (the shaders' code
// beside them):
//
//   graphics  0–3   the vertex shader's samplers, storage textures, storage buffers, uniform buffers
//             4–7   the fragment shader's, the same
//             8     the target's Format       9–15  Blend: enabled, src/dst colour, colour op, src/dst alpha, alpha op
//             16    Topology                  17    the depth target's Format (none: no depth)
//             18–20 DepthState: test, write, Compare
//             21    samples                   22    vertex buffers, then each: stride, per instance,
//                                                   attributes, then each: location, VertexFormat, offset
//   compute   0–5   samplers, storage textures, storage buffers, uniform buffers, read-write textures, read-write buffers
export module openmui.rhi:wire;

import std;
import :types;
import :commands;

export namespace omui::rhi::wire {

// A command's kind on the wire: its index in rhi::Command.
enum class Op : std::uint8_t {
    upload_buffer, upload_texture, copy_texture, begin_render, end_render, set_pipeline, set_viewport, set_scissor,
    bind_vertex_buffer, bind_index_buffer, bind_texture, bind_storage_texture, bind_storage_buffer, draw, draw_indexed,
    begin_compute, end_compute, set_compute_pipeline, dispatch, push_uniforms,
};
inline constexpr std::size_t op_count = 20;

// No handle.
inline constexpr std::uint32_t none = 0xffffffffu;

[[nodiscard]] constexpr Op op(std::uint32_t header) noexcept { return static_cast<Op>(header & 0xffu); }
[[nodiscard]] constexpr std::uint32_t length(std::uint32_t header) noexcept { return header >> 8; }

// The words of `list`'s commands, appended to `out` (which keeps its
// capacity: a list of the same shape as the last allocates nothing).
void encode(const CommandList& list, std::vector<std::uint32_t>& out);

// A pipeline's words, appended to `out`.
void encode(const GraphicsPipelineDesc& d, std::vector<std::uint32_t>& out);
void encode(const ComputePipelineDesc& d, std::vector<std::uint32_t>& out);

}  // namespace omui::rhi::wire
