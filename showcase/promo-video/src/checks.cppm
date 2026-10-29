// openmui.showcase.promo_video:checks — the film's automatic acceptance
// (promo-video design §5.1), each a verdict with what was measured:
//
//   duration    the main cut 60 ± 5 s, the short 15 ± 1 s
//   spec        60 frames a second; 1920 × 1080 (4K at scale 2); the short 16:9 and 9:16
//   lines       every line in Chinese and English, each up long enough to read
//               (one second, 9 Chinese or 17 English characters a second)
//   beats       every cut and every `hit:` marker within a frame of a beat of
//               the music as it plays there; every cue on its frame's first sample;
//               and in the sound itself, an onset within a frame of each hit
//   frames      each frame's typography at rest inside the title-safe area and
//               apart; no blank frame
//   determinism the same bytes drawn twice
//   seek        frames drawn directly the same as played to
export module openmui.showcase.promo_video:checks;

import std;
import openmui;
import :film;
import :player;
import :sound;

export namespace omui::promo {

struct Verdict {
    std::string name;
    bool        ok = true;
    std::string detail;  // what was measured
};

// A problem, at a frame when it is a frame's.
struct Finding {
    std::string  check;
    std::string  what;
    std::int64_t frame = -1;
};

struct Acceptance {
    std::vector<Verdict> verdicts;
    std::vector<Finding> findings;
    [[nodiscard]] bool ok() const noexcept;
    [[nodiscard]] Value json() const;
    void add(Verdict v) { verdicts.push_back(std::move(v)); }
};

// The least a line may stay up.
inline constexpr double least_line_seconds = 1.0;
inline constexpr double chinese_per_second = 9, english_per_second = 17;
[[nodiscard]] double reading_seconds(const Line& line) noexcept;

// What needs no picture: duration, spec, lines, beats (and the sound's onsets).
[[nodiscard]] Acceptance check_data(const Film& film, const Cut& cut, const Soundtrack& track, Size size);

// One frame as drawn: its typography and its pixels.
[[nodiscard]] std::vector<Finding> check_frame(Player& player, std::int64_t frame, const raster::Surface& pixels);

// The frames to look at: every `step`th, and the first and last of each clip.
[[nodiscard]] std::vector<std::int64_t> sampled_frames(const Cut& cut, std::int64_t step);

}  // namespace omui::promo
