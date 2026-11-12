module xengine.audio.timeline;

import std;
import mlibs.trace;

namespace xengine::audio {
std::string_view gain_error_name(GainError error) noexcept {
    switch (error) {
        case GainError::sample_rate: return "sample-rate";
        case GainError::clip_time: return "clip-time";
        case GainError::value: return "value";
        case GainError::property: return "property";
        case GainError::keys: return "keys";
        case GainError::easing: return "easing";
    }
    return "unknown";
}

std::expected<std::shared_ptr<const GainAutomation>, GainError>
GainAutomation::prepare(const Clip& clip, GainOptions options) {
    mlibs::trace::Span span("timeline.audio.prepare", "audio");
    if (!exact_samples(options.sample_rate)) return std::unexpected(GainError::sample_rate);
    if (clip.at < 0 || clip.length <= 0 || clip.at > std::numeric_limits<Flicks>::max() - clip.length ||
        clip.fade.in < 0 || clip.fade.out < 0 || clip.fade.in > clip.length || clip.fade.out > clip.length - clip.fade.in)
        return std::unexpected(GainError::clip_time);
    const auto gain = [](double value) { return std::isfinite(value) && value >= 0 && value <= 8; };
    if (!gain(clip.level) || !gain(options.outside)) return std::unexpected(GainError::value);
    if (!clip.fade.ease.valid()) return std::unexpected(GainError::easing);
    const Property* selected = nullptr;
    for (const auto& p : clip.properties) if (p.name == options.property) {
        if (selected) return std::unexpected(GainError::property);
        selected = &p;
    }
    if (options.property.empty() || !selected || selected->keys.empty()) return std::unexpected(GainError::property);
    const double duration = to_seconds(clip.length);
    double previous = -1;
    for (const auto& key : selected->keys.keys()) {
        const double time = key.time.count();
        if (!std::isfinite(time) || time < 0 || time > duration || time < previous) return std::unexpected(GainError::keys);
        if (!gain(key.value)) return std::unexpected(GainError::value);
        if (!key.ease.valid()) return std::unexpected(GainError::easing);
        previous = time;
    }
    // Copies and allocation occur before publishing the immutable owner.
    auto result = std::shared_ptr<GainAutomation>(new GainAutomation);
    result->clip_.at = clip.at; result->clip_.length = clip.length;
    result->clip_.level = clip.level; result->clip_.fade = clip.fade;
    result->keys_ = selected->keys;
    result->rate_ = options.sample_rate;
    result->sample_length_ = flicks_per_second / options.sample_rate;
    result->outside_ = options.outside;
    return std::shared_ptr<const GainAutomation>(std::move(result));
}

float GainAutomation::at(Flicks time) const noexcept {
    if (!clip_.covers(time)) return outside_;
    const double value = keys_.value(hanim::seconds(to_seconds(time - clip_.at))) * clip_.level_at(time);
    return std::isfinite(value) ? static_cast<float>(std::clamp(value, 0.0, 8.0)) : 0.0f;
}

float GainAutomation::at_sample(std::int64_t sample) const noexcept {
    // Out-of-range queries cannot overflow the timeline's flick clock.
    if (sample < std::numeric_limits<Flicks>::min() / sample_length_ || sample > std::numeric_limits<Flicks>::max() / sample_length_)
        return outside_;
    return at(sample * sample_length_);
}

void GainAutomation::sample(std::int64_t first, std::span<float> values) const noexcept {
    mlibs::trace::Span span("timeline.audio.sample", "audio");
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = at_sample(first);
        if (first == std::numeric_limits<std::int64_t>::max()) {
            std::fill(values.begin() + static_cast<std::ptrdiff_t>(i + 1), values.end(), outside_);
            break;
        }
        ++first;
    }
}
}
