// Optional adapter: the audio engine is an ordinary consumer of a prepared
// control signal, and observers query that same immutable signal without
// advancing an audio device, mixer, sequencer or any live event cursor.
export module openmui.timeline.audio;

import std;
import openmui.timeline;
import openmui.audio;
import hanim.core;

export namespace omui::timeline {

enum class GainError : std::uint8_t { sample_rate, clip_time, value, property, keys, easing };
[[nodiscard]] std::string_view gain_error_name(GainError error) noexcept;

struct GainOptions {
    std::int32_t sample_rate = 48000;
    std::string_view property = "gain";
    float outside = 1; // before the clip and at/after its exclusive end
};

class GainAutomation final {
public:
    // Own a frozen copy of the chosen property and clip fades. Property keys
    // are in clip-local presentation time, exactly as Clip::value; source
    // trim/speed do not retime them. Missing/duplicate/empty property is an error.
    [[nodiscard]] static std::expected<std::shared_ptr<const GainAutomation>, GainError>
        prepare(const Clip& clip, GainOptions options = {});

    [[nodiscard]] std::int32_t rate() const noexcept { return rate_; }
    void sample(std::int64_t first_sample, std::span<float> values) const noexcept;
    // Read-only, arbitrary-order visual/Agent queries. Same source as DSP;
    // clip level/fades multiply the keys and the result clamps to [0,8].
    [[nodiscard]] float at(Flicks time) const noexcept;
    [[nodiscard]] float at_sample(std::int64_t sample) const noexcept;
    [[nodiscard]] Flicks begin() const noexcept { return clip_.at; }
    [[nodiscard]] Flicks end() const noexcept { return clip_.end(); }

private:
    GainAutomation() = default;
    Clip clip_; // geometry of time/level/fades only; no source or properties
    hanim::Track<double> keys_;
    std::int32_t rate_ = 48000;
    Flicks sample_length_ = flicks_per_second / 48000;
    float outside_ = 1;
};
}
