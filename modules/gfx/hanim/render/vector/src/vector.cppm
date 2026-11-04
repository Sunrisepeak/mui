// A renderer adapter, not another animation or rendering implementation.
export module hanim.render.vector;

import std;
export import hanim.library;
export import hanim.effect;
export import openmui.render.vector;
export import openmui.geom;

namespace hanim::detail { struct VectorData; }

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
};
[[nodiscard]] std::string_view vector_error_name(VectorError code) noexcept;
struct MaterialBinding {
    std::string part;
    omui::render::Paint from, to;
};
struct VectorRequirements {
    std::size_t world_transforms = 0, backend_parameters = 0, effect_layers = 0;
};
struct VectorScratch {
    // Reused native recording arena ensures failed recording never appends
    // partial commands to the destination. Must differ from destination.
    omui::render::DisplayList* recording = nullptr;
    std::span<omui::geom::Affine> world;
    std::span<float> backend_parameters;
    std::span<omui::geom::Rect> effect_layers;
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
        omui::render::DisplayList& destination, VectorScratch scratch) const;
private:
    std::shared_ptr<const detail::VectorData> data_;
};
} // namespace hanim::vector_render
