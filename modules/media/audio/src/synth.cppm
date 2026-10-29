// openmui.audio:synth — an instrument's voices (not exported): a patch made
// ready for a rate (its curves as factors a sample), a note begun on a
// voice, and its samples added into a block. Nothing here allocates: a
// plucked string's line is the instrument's, given to the voice.
module openmui.audio:synth;

import std;
import :mix;
import :music;
import :dsp;

namespace omui::audio::synth {

using dsp::Biquad;
using dsp::BiquadState;

// An envelope as factors a sample.
struct Curve {
    double attack = 1;   // added a sample while rising
    double decay = 0;    // the distance to `sustain` kept a sample
    double sustain = 1;
    double release = 0;  // the level kept a sample once let go
};

// A patch made ready for a rate.
struct Tone {
    Patch                 patch;
    double                rate = 48000;
    Curve                 amp, filter;
    std::int32_t          unison = 1;
    std::array<double, 7> ratio{}, left{}, right{}, phase{};  // each unison voice's pitch, place and start
    bool                  stereo = false;                     // its voices spread (else one channel, copied)
    double                pwm_step = 0, vibrato_step = 0, vibrato_delay = 1;
    double                index_fall = 0;                     // FM's index towards its sustain
    double                pole = 0.5;                         // a string's loss of highs (one-pole lowpass)
    double                string_decay = 2;                   // its seconds to 60 dB
    double                sweep = 0, tone_fall = 0, noise_fall = 0, click_fall = 0;
    std::array<double, 2> mode_fall{};
    std::int64_t          burst_gap = 480;
    std::int64_t          glide = 1;                          // samples a slide takes
    double                drive_k = 1, drive_norm = 1;
    double                choke_fall = 0;                     // the level kept a sample when choked
    Biquad                shell;                              // a drum's noise and metal (its filter fixed)
};
[[nodiscard]] Tone prepare(const Patch& patch, std::int32_t rate);

struct Voice {
    bool          active = false, released = false;
    std::uint64_t serial = 0;
    std::int64_t  start = 0, off = 0, age = 0;  // the clock's sample it began on, and was let go on; samples since
    double        freq = 0, slide = 1;          // Hz now, and a slide's factor a sample …
    std::int64_t  sliding = 0;                  // … for this many more
    double        velocity = 1;
    std::int32_t  stage = 0, filter_stage = 0;  // 0 rising, 1 falling to sustain, 2 let go
    double        env = 0, filter_env = 0, fall = 0;
    Biquad        coefficients;
    BiquadState   fl, fr;
    double        vibrato = 0, wobble = 1;      // the vibrato's phase, and its factor now
    std::array<double, 7> phase{};
    double        sub = 0, pwm = 0;
    std::uint32_t noise = 1;
    double        mod = 0, carrier = 0, last = 0, index = 0;  // FM
    std::span<float> line;                      // a string's
    std::size_t   at = 0;
    double        lowpass = 0, loss = 1, tau = 0, dc_in = 0, dc_out = 0;
    double        excess = 0, tone = 0, shell = 0, click = 0, tone_phase = 0;  // a drum's
    std::array<double, 2> mode_phase{}, mode{};
    std::array<double, 6> metal{};
    BiquadState   shell_state;
    std::int32_t  bursts = 0;
};

// A note on `voice` (taking its place when it sounds already: what it is
// rings into the new one): `freq` Hz (0: a drum's own pitch × `pitch`),
// sliding to `to` (0: none), from sample `start` of the clock, let go
// `length` samples later; `serial` orders the voices (the oldest is taken
// first), `seed` its noise.
void begin(Voice& voice, const Tone& tone, double freq, double to, double pitch, double velocity, std::int64_t start, std::int64_t length,
           std::uint64_t serial, std::uint32_t seed) noexcept;
// Its samples of [clock, clock + frames) added into `left` and `right`.
void sound(Voice& voice, const Tone& tone, std::int64_t clock, std::int32_t frames, float* left, float* right) noexcept;
// Stopped in a few milliseconds (another of its choke group began).
void choke(Voice& voice, const Tone& tone) noexcept;
// The samples a string's line needs for its lowest note.
[[nodiscard]] std::size_t line_size(double lowest, std::int32_t rate) noexcept;

}  // namespace omui::audio::synth
