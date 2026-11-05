// A renderer adapter, not another animation or rendering implementation.
export module hanim.render.vector;

import std;
export import hanim.library;
export import hanim.effect;
export import hanim.program;
export import xengine.render.vector;
export import mlibs.geom;

namespace hanim::detail { struct VectorData; struct VectorProgramData; }

export namespace hanim::vector_render {
enum class VectorError : std::uint8_t {
    unprepared, foreign_frame, invalid_geometry, invalid_progress,
    buffer_too_small, recording_alias, capacity, unknown_effect,
    unsupported_effect, unknown_parameter, invalid_parameter, stale_effect,
    unknown_part, duplicate_material, invalid_material, recording_failed
};
// entry/detail identify the relevant part, effect or parameter; no borrowed
// failure strings or per-frame diagnostic allocations.
struct VectorFailure {
    VectorError code;
    std::size_t entry = 0, detail = 0;
    std::size_t track = invalid_program_index, segment = invalid_program_index, scope = invalid_program_index;
};
[[nodiscard]] std::string_view vector_error_name(VectorError code) noexcept;
struct MaterialBinding {
    std::string part;
    xengine::render::Paint from, to;
};
struct VectorRequirements {
    std::size_t world_transforms = 0, backend_parameters = 0, effect_layers = 0;
};
struct VectorScratch {
    // Reused native recording arena ensures failed recording never appends
    // partial commands to the destination. Must differ from destination.
    xengine::render::DisplayList* recording = nullptr;
    std::span<mlibs::geom::Affine> world;
    std::span<float> backend_parameters;
    std::span<mlibs::geom::Rect> effect_layers;
};
class PreparedVector {
public:
    PreparedVector() noexcept = default;
    // Strict CPU binding, with deep-owned solid/gradient material endpoints.
    // No dependency on an Engine, UI, device or host.
    [[nodiscard]] static std::expected<PreparedVector, VectorFailure> bind(
        const PreparedObjectMorph& geometry, const PreparedEffects& effects = {},
        std::span<const MaterialBinding> materials = {});
    [[nodiscard]] VectorRequirements requirements() const noexcept;
    // Uses this binding's effects metadata and already-sampled float values.
    // All expected failures preserve destination. Scratch is disposable on
    // failure and exclusive per concurrent caller, as are destination/buffers.
    [[nodiscard]] std::expected<void, VectorFailure> record(
        FrameView frame, std::span<const float> effect_values,
        xengine::render::DisplayList& destination, VectorScratch scratch) const;
private:
    friend class PreparedVectorProgram;
    std::shared_ptr<const detail::VectorData> data_;
};
struct ProgramVectorLimits {
    std::size_t max_prepared_backend_parameters = 65536, max_prepared_effect_layers = 65536;
};
struct ProgramVectorRequirements {
    VectorRequirements buffers;
    std::size_t tracks = 0, scopes = 0, scope_depth = 0;
};
struct ProgramVectorScratch {
    VectorScratch buffers;
    // bounds: tracks followed by scopes. Index buffers must not overlap.
    std::span<mlibs::geom::Rect> bounds;
    std::span<std::size_t> scope_members, order, scope_stack;
};
class PreparedVectorProgram {
public:
    PreparedVectorProgram() noexcept = default;
    [[nodiscard]] static std::expected<PreparedVectorProgram, VectorFailure> bind(
        const PreparedProgram&, ProgramVectorLimits limits = {});
    [[nodiscard]] ProgramVectorRequirements requirements() const noexcept;
    // Draws selected bindings in author preorder; nested effects apply once
    // after their subtree. No Engine dependency and no private rasterizer.
    [[nodiscard]] std::expected<void, VectorFailure> record(
        ProgramFrameView frame, xengine::render::DisplayList& destination, ProgramVectorScratch scratch) const;
private:
    std::shared_ptr<const detail::VectorProgramData> data_;
};
} // namespace hanim::vector_render
