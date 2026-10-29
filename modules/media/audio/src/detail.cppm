// openmui.audio:detail — what the readers and the sequencer share (not
// exported): the nearest right word for a misspelt one, a pattern's text
// read into hits with each problem's offset in that text, and the names of
// the format's choices.
module openmui.audio:detail;

import std;
import :mix;
import :music;

namespace omui::audio::detail {

// Edits between two words, two letters swapped counting as one (`ptich` is one from `pitch`).
[[nodiscard]] std::size_t distance(std::string_view a, std::string_view b);
// "did you mean `x`?" when one is close, else "one of `a`, `b`, …".
[[nodiscard]] std::string nearest(std::string_view name, std::span<const std::string_view> options);

// A problem at `offset` in a pattern's text.
struct TextProblem {
    std::size_t offset = 0;
    std::string message, hint;
};

// A pattern's notes or one of its lanes, `step` ticks a step, into `out`
// (lane `lane`); answers the bars it lasts (0 with problems).
std::int32_t read_notes(std::string_view text, std::int32_t step, std::int32_t bar, float swing, std::vector<Hit>& out, std::vector<TextProblem>& problems);
std::int32_t read_lane(std::string_view text, std::int32_t step, std::int32_t bar, float swing, std::int32_t lane, std::vector<Hit>& out,
                       std::vector<TextProblem>& problems);
// The ticks a pattern's step is (its default when 0).
[[nodiscard]] std::int32_t step_ticks(const Pattern& pattern) noexcept;

// A MIDI note's name ("C#4"), and a name's note (−1: not one).
[[nodiscard]] std::string note_name(std::int32_t key);
[[nodiscard]] std::int32_t note_key(std::string_view name) noexcept;

inline constexpr std::array<std::string_view, 5> waves{"sine", "triangle", "square", "saw", "noise"};
inline constexpr std::array<std::string_view, 4> filters{"none", "lowpass", "highpass", "bandpass"};
inline constexpr std::array<std::string_view, 4> engines{"analog", "fm", "pluck", "drum"};
inline constexpr std::array<std::string_view, 6> effects{"lowpass", "highpass", "bandpass", "echo", "reverb", "chorus"};

}  // namespace omui::audio::detail
