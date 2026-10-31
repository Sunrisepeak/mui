// openmui.showcase.promo_video:sound — a cut's sound track, offline (the
// same bits everywhere): its music clips (the score rendered once, each clip
// a stretch of it, from its `in`, with its fades), every sound cue — the
// sfx track's `sound:<name>` clips and the cues each shot makes of what it
// does (a key typed, an agent's touch, a brick) — on its frame's first
// sample, mixed through openmui.audio's buses (the music ducked under them)
// and its limiter. Under a `film` clip, the cut's own sound at the clip's
// source times (backwards, five times over: the tape rewinding), and the
// cues inside the clip over it.
export module openmui.showcase.promo_video:sound;

import std;
import openmui.audio;
import openmui.video;
import :film;
import :player;

export namespace omui::promo {

// A cue as placed on the cut: its sound, on a frame.
struct Placed {
    std::string  sound;
    std::int64_t frame = 0;
    float        gain = 1, pan = 0, pitch = 1;
    std::string  from;  // the clip that made it
};

struct Soundtrack {
    std::int32_t        rate = 48000;
    std::vector<float>  samples;  // stereo, interleaved: the cut's length exactly
    std::vector<Placed> cues;
    audio::LimiterStats limiter;
    // Where each music clip's bars begin on the cut, in samples (the beats are a quarter of one apart).
    std::vector<std::int64_t> bars;
    double              tempo = 120;
};

// The cut's sound track; `player` makes the shots whose cues it places.
[[nodiscard]] std::expected<Soundtrack, std::string> soundtrack(const Film& film, const Cut& cut, const Player& player);

}  // namespace omui::promo
