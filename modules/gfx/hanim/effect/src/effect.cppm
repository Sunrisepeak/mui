// Ordered parameter plans; the backend binds ids and implements effects.
export module hanim.effect;

import std;
export import hanim.core;

namespace hanim::detail { struct EffectsData; }

export namespace hanim {

inline constexpr std::uint32_t effect_contract_version = 1;
struct EffectParameter { std::string name; float from = 0, to = 0; };
struct EffectSpec { std::string id; std::vector<EffectParameter> parameters; };
using Effects = std::vector<EffectSpec>;
struct EffectLimits { std::size_t max_effects = 64, max_parameters = 1024; };
enum class EffectError : std::uint8_t {
    invalid_limits, invalid_id, duplicate_parameter, nonfinite_value,
    capacity, nonfinite_progress, buffer_too_small
};
[[nodiscard]] std::string_view effect_error_name(EffectError error) noexcept;
// Parameters occupy [offset, offset + count) in parameters()/sample output.
// Identical effect ids can occur repeatedly; order is never sorted/deduped.
struct EffectEntry { std::string id; std::size_t offset = 0, count = 0; };

class PreparedEffects {
public:
    // The default is a valid, allocation-free empty effect chain.
    PreparedEffects() noexcept = default;
    [[nodiscard]] static std::expected<PreparedEffects, EffectError>
        prepare(std::span<const EffectSpec> input, EffectLimits limits = {});
    [[nodiscard]] std::size_t parameter_count() const noexcept;
    [[nodiscard]] std::span<const EffectEntry> effects() const & noexcept;
    std::span<const EffectEntry> effects() const && = delete;
    [[nodiscard]] std::span<const EffectParameter> parameters() const & noexcept;
    std::span<const EffectParameter> parameters() const && = delete;
    // Finite progress clamps to [0,1]; invalid progress/buffer never writes.
    // Output beyond parameter_count() is untouched. No scratch or clocks.
    [[nodiscard]] std::expected<void, EffectError>
        sample(double progress, std::span<float> output) const noexcept;
private:
    std::shared_ptr<const detail::EffectsData> data_;
};

namespace fx {
// Typed process-stage effects, evaluated after matching and before rendering.
// Their targets cannot be confused with image effects' string ids. Repeating
// one target adds displacements in its endpoint frame, checked at prepare.
struct VertexPath { PointPath path; };
struct TranslationPath { PointPath path; };
[[nodiscard]] constexpr VertexPath vertex_path(PointPath path) noexcept { return {path}; }
[[nodiscard]] constexpr TranslationPath translation_path(PointPath path) noexcept { return {path}; }
// Logical pixel radius; backend binding checks its supported range.
[[nodiscard]] EffectSpec blur(float from, float to);
struct Tone { float r = 0.79f, g = 0.76f, b = 0.71f, amount = 1; };
// Tint, not a brightness/contrast operation; alpha is retained by the backend.
[[nodiscard]] EffectSpec tone(Tone from, Tone to);
[[nodiscard]] Effects chain(std::initializer_list<EffectSpec> effects);
}
} // namespace hanim
