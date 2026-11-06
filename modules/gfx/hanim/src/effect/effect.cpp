module hanim.effect;

import std;
import openmui.log;
import openmui.trace;

namespace hanim::detail {
struct EffectsData { std::vector<EffectEntry> effects; std::vector<EffectParameter> parameters; };
}

namespace hanim {
namespace {
constexpr omui::Log log{"hanim.effect"};
bool valid_key(std::string_view key) noexcept {
    return !key.empty() && key.size() <= 128 &&
        std::ranges::none_of(key, [](unsigned char c) { return c < 32 || c == 127; });
}
}
std::string_view effect_error_name(EffectError error) noexcept {
    switch (error) {
        case EffectError::invalid_limits: return "invalid-limits";
        case EffectError::invalid_id: return "invalid-id";
        case EffectError::duplicate_parameter: return "duplicate-parameter";
        case EffectError::nonfinite_value: return "nonfinite-value";
        case EffectError::capacity: return "capacity";
        case EffectError::nonfinite_progress: return "nonfinite-progress";
        case EffectError::buffer_too_small: return "buffer-too-small";
    }
    return "unknown";
}
std::expected<PreparedEffects, EffectError> PreparedEffects::prepare(std::span<const EffectSpec> input, EffectLimits limits) {
    omui::trace::Span span("prepare", "hanim.effect");
    auto refuse = [](EffectError error) -> std::expected<PreparedEffects, EffectError> {
        log.warn("prepare refused: {}", effect_error_name(error));
        omui::trace::mark("refused", "hanim.effect");
        return std::unexpected(error);
    };
    if (!limits.max_effects || limits.max_effects > 4096 || !limits.max_parameters || limits.max_parameters > 65536)
        return refuse(EffectError::invalid_limits);
    if (input.size() > limits.max_effects) return refuse(EffectError::capacity);
    std::size_t count = 0;
    for (const auto& effect : input) {
        if (!valid_key(effect.id)) return refuse(EffectError::invalid_id);
        if (effect.parameters.size() > limits.max_parameters - count) return refuse(EffectError::capacity);
        count += effect.parameters.size();
        std::set<std::string_view> names;
        for (const auto& parameter : effect.parameters) {
            if (!valid_key(parameter.name)) return refuse(EffectError::invalid_id);
            if (!std::isfinite(parameter.from) || !std::isfinite(parameter.to)) return refuse(EffectError::nonfinite_value);
            if (!names.insert(parameter.name).second) return refuse(EffectError::duplicate_parameter);
        }
    }
    PreparedEffects result;
    if (!input.empty()) {
        auto data = std::make_shared<detail::EffectsData>();
        data->effects.reserve(input.size()); data->parameters.reserve(count);
        for (const auto& effect : input) {
            data->effects.push_back({effect.id, data->parameters.size(), effect.parameters.size()});
            data->parameters.insert(data->parameters.end(), effect.parameters.begin(), effect.parameters.end());
        }
        result.data_ = std::move(data);
    }
    omui::trace::count("effects", static_cast<double>(input.size()), "hanim.effect");
    omui::trace::count("parameters", static_cast<double>(count), "hanim.effect");
    log.info("prepared {} ordered effects and {} parameters", input.size(), count);
    return result;
}
std::size_t PreparedEffects::parameter_count() const noexcept { return data_ ? data_->parameters.size() : 0; }
std::span<const EffectEntry> PreparedEffects::effects() const & noexcept {
    return data_ ? std::span<const EffectEntry>(data_->effects) : std::span<const EffectEntry>{};
}
std::span<const EffectParameter> PreparedEffects::parameters() const & noexcept {
    return data_ ? std::span<const EffectParameter>(data_->parameters) : std::span<const EffectParameter>{};
}
std::expected<void, EffectError> PreparedEffects::sample(double progress, std::span<float> output) const noexcept {
    omui::trace::Span span("sample", "hanim.effect");
    if (!std::isfinite(progress)) return std::unexpected(EffectError::nonfinite_progress);
    if (output.size() < parameter_count()) return std::unexpected(EffectError::buffer_too_small);
    const auto t = dm::clamp(progress, 0.0, 1.0);
    for (std::size_t i = 0; i < parameter_count(); ++i)
        output[i] = hanim::lerp(data_->parameters[i].from, data_->parameters[i].to, t);
    return {};
}
namespace fx {
EffectSpec blur(float from, float to) { return {"blur", {{"radius", from, to}}}; }
EffectSpec opacity(float from, float to) { return {"opacity", {{"amount", from, to}}}; }
EffectSpec tone(Tone from, Tone to) {
    return {"tone", {{"color_r", from.r, to.r}, {"color_g", from.g, to.g},
        {"color_b", from.b, to.b}, {"amount", from.amount, to.amount}}};
}
EffectSpec pixelate(Pixelate from, Pixelate to, PixelGrid grid) {
    const float unit = grid.unit == PixelUnit::device ? 1.0f : 0.0f;
    return {"pixelate", {{"size", from.size, to.size}, {"amount", from.amount, to.amount},
        {"device_pixels", unit, unit}, {"origin_x", grid.origin_x, grid.origin_x}, {"origin_y", grid.origin_y, grid.origin_y}}};
}
Effects chain(std::initializer_list<EffectSpec> effects) { return {effects}; }
}
} // namespace hanim
