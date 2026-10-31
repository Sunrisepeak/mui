// openmui.audio:music — music as data, played sample-accurately.
//
// A piece (`*.music.omui.toml`) is a tempo and a metre; patches (how an
// instrument sounds: an analog-style oscillator stack, two-operator FM, a
// plucked string, a drum); instruments (a patch with a level, a pan, sends
// to the piece's buses, a voice count); buses with effects and ducking;
// patterns (notes as text, `"D5 . F5 G5 | A5:2 ."`, or drum lanes as steps,
// `"x...x...x..x.x.."`); sections of bars whose layers play patterns over
// an intensity range; and an arrangement: an intro, a loop, an outro.
//
// The sequencer is a source in the mixer: it starts each note on its very
// sample of the mixer's clock, with nothing allocated while it plays. A game
// sets the intensity (0–1) from any thread and the layers whose range holds
// it play from the next bar. Every sample is a function of the piece, the
// clock and what was asked when, so a render is the same bits everywhere.
export module openmui.audio:music;

import std;
import :mix;

export namespace omui::audio {

inline constexpr std::int32_t whole_ticks = 3840;  // a whole note's ticks (thirds and fifths of a beat land on ticks)

// ── Patches: how an instrument sounds ───────────────────────────────────

enum class Engine : std::uint8_t {
    analog,  // oscillators (unison saws, pulses with PWM, a sub, noise) through a filter
    fm,      // two operators: a sine modulating a sine (bells, e-pianos, plucked basses)
    pluck,   // a plucked string (Karplus–Strong): koto, guzheng, harp, guitar
    drum,    // a membrane and its shell: a falling tone, its modes, noise, metal, a click
};

// Seconds: up over `attack` (a line), then down to `sustain` and, once the
// note is let go, to silence — each falling 60 dB in its time (curves).
struct Adsr {
    float attack = 0.005f, decay = 0.3f, sustain = 0.8f, release = 0.2f;
    bool operator==(const Adsr&) const = default;
};

struct Osc {
    Wave         wave = Wave::saw;  // saw, square (a pulse `width` wide), triangle, sine or noise
    std::int32_t unison = 1;        // voices a note (1–7) …
    float        detune = 0;        // … spread over this many cents …
    float        spread = 0.5f;     // … and across the stereo field (0–1)
    float        width = 0.5f;      // a pulse's (0.05–0.95) …
    float        pwm = 0;           // … swinging by this much …
    float        pwm_rate = 0.5f;   // … this often (Hz)
    float        sub = 0;           // a sine an octave down
    float        noise = 0;         // white noise mixed in
    bool operator==(const Osc&) const = default;
};

struct Fm {
    float ratio = 1;      // the modulator's frequency, of the note's
    float index = 2;      // how much it modulates at the start (radians) …
    float sustain = 0;    // … falling to this …
    float decay = 0.5f;   // … over this (seconds, to 60 dB of the way): the brightness of the attack
    float feedback = 0;   // the modulator on itself (0–1: towards a saw)
    bool operator==(const Fm&) const = default;
};

struct Pluck {
    float brightness = 0.6f;  // the pluck and the string's loss of highs (0 dark … 1 metallic)
    float decay = 2;          // seconds the string rings, to 60 dB
    float pick = 0.15f;       // where along the string it is plucked (0–0.5)
    bool operator==(const Pluck&) const = default;
};

struct Drum {
    float        tone = 1;           // the membrane's level
    float        pitch = 60;         // the membrane's Hz (a note played on it gives its own)
    float        from = 3;           // the pitch at the hit, times `pitch` …
    float        sweep = 0.04f;      // … falling in this (seconds)
    float        decay = 0.4f;       // the tone's seconds to 60 dB
    float        modes = 0;          // the membrane's overtones (1.59, 2.14 ×), 0–1
    float        noise = 0;          // the shell: noise through the patch's filter …
    float        noise_decay = 0.1f; // … for this long
    float        metal = 0;          // six square waves at a cymbal's ratios, with the noise
    float        click = 0;          // the stick's click
    std::int32_t bursts = 1;         // the noise's hits, 10 ms apart (a clap's 3)
    bool operator==(const Drum&) const = default;
};

struct PatchFilter {
    FilterKind kind = FilterKind::none;
    float      cutoff = 8000, q = 0.707f;
    float      envelope = 0;   // octaves its envelope opens it by
    float      keytrack = 0;   // 0 fixed … 1 following the note
    Adsr       adsr{0.005f, 0.4f, 0, 0.3f};
    bool operator==(const PatchFilter&) const = default;
};

struct Vibrato {
    float depth = 0;      // cents
    float rate = 5;       // Hz
    float delay = 0.3f;   // seconds before it is all there
    bool operator==(const Vibrato&) const = default;
};

struct Patch {
    std::string name;
    std::string base;              // a built-in it starts from ("": none)
    Engine      engine = Engine::analog;
    Adsr        amp;
    Osc         osc;
    Fm          fm;
    Pluck       pluck;
    Drum        drum;
    PatchFilter filter;
    Vibrato     vibrato;
    float       glide = 0.06f;     // seconds a slide (`A4>C5`) takes
    float       drive = 0;         // soft clipping, 0 clean … 1 hard
    float       gain = 1;
    bool operator==(const Patch&) const = default;
};

// The patches every piece may name without defining them: "koto", "taiko",
// "kick", "snare", "hat", "pad", "supersaw", "epiano", "bell", "fm_bass", ….
[[nodiscard]] std::span<const Patch> builtin_patches();

// ── A piece ──────────────────────────────────────────────────────────────

struct Send {
    std::string bus;
    float       level = 0;
    bool operator==(const Send&) const = default;
};

struct Instrument {
    std::string       name, patch;
    std::string       bus;            // one of the piece's ("": the piece's own)
    float             gain = 1, pan = 0;
    float             legato = 0.9f;  // of a note's written length held before it is let go
    float             humanize = 0;   // a player's touch: notes up to 30% softer and 6 ms later by this (0–1), by each note's place in the piece
    std::int32_t      transpose = 0;  // semitones
    std::int32_t      voices = 8;     // notes at once; past that the oldest is played again
    std::vector<Send> sends;          // to the piece's buses, after its gain and pan
    std::string       gate;           // a rhythm its sound is cut to, a 1/16 a step (`x` open, `.` shut), from each bar
    std::string       choke;          // a group: an instrument's note stops the others' (open and closed hats)
    bool operator==(const Instrument&) const = default;
};

struct MusicBus {
    std::string             name;
    std::string             to;       // another of the piece's buses ("": the piece's own)
    float                   gain = 1;
    std::vector<EffectSpec> effects;
    std::optional<Duck>     duck;     // its key one of the piece's buses
    bool operator==(const MusicBus&) const = default;
};

struct Lane {
    std::string instrument;
    std::string steps;  // a character a step: `x` a hit, `X` an accent, `o` a ghost, `.` none; `|` a bar line
    bool operator==(const Lane&) const = default;
};

// Notes as text, a token a step: `C4` (a note: A–G, # or b, an octave),
// `.` a rest, `C4:3` three steps long, `C4+E4+G4` a chord, `A4>C5` sliding
// up to C5, `!` an accent and `?` a soft one (`D5:2!`); `|` a bar line
// (what lies between two is whole bars). Or drum lanes, an instrument each.
struct Pattern {
    std::string       name;
    std::string       instrument;     // the notes'
    std::string       notes;
    std::vector<Lane> lanes;
    std::int32_t      step = 0;       // a step is 1/step of a whole note (0: an eighth for notes, a sixteenth for lanes)
    float             swing = 0;      // every other step late by this of a step (0–0.5)
    bool operator==(const Pattern&) const = default;
};

// A pattern played in a section: from bar `at`, `times` over (0: to the
// section's end), when the intensity is in [from, to) (to 1 holds 1); its
// notes by another instrument than the pattern's when `instrument` is set
// (a melody doubled).
struct MusicLayer {
    std::string          pattern;
    std::string          instrument;
    std::array<float, 2> intensity{0, 1};
    std::int32_t         at = 0, times = 0, transpose = 0;
    float                gain = 1;
    bool operator==(const MusicLayer&) const = default;
};

struct Section {
    std::string        name;
    std::int32_t       bars = 4;
    std::vector<MusicLayer> layers;
    bool operator==(const Section&) const = default;
};

struct Arrangement {
    std::vector<std::string> intro, loop, outro;  // sections: the intro once, the loop over and over, the outro when finished
    bool operator==(const Arrangement&) const = default;
};

struct Music {
    std::string                 name = "music";
    std::string                 doc;
    float                       tempo = 120;      // beats a minute (a beat: the metre's note)
    std::array<std::int32_t, 2> meter{4, 4};
    float                       gain = 1;
    std::vector<Patch>          patches;
    std::vector<Instrument>     instruments;
    std::vector<MusicBus>       buses;
    std::vector<Pattern>        patterns;
    std::vector<Section>        sections;
    Arrangement                 arrangement;

    [[nodiscard]] std::int32_t bar_ticks() const noexcept { return whole_ticks / meter[1] * meter[0]; }
    [[nodiscard]] double beat_seconds() const noexcept { return 60.0 / tempo; }
    [[nodiscard]] double bar_seconds() const noexcept { return beat_seconds() * meter[0]; }
    // Bars of the intro, of one pass of the loop, of the outro.
    [[nodiscard]] std::int32_t bars(std::span<const std::string> sections) const noexcept;
    bool operator==(const Music&) const = default;
};

// A pattern's hits, in its ticks (a whole note is `whole_ticks`).
struct Hit {
    std::int32_t tick = 0, length = 0;
    std::int32_t key = -1, to = -1;  // MIDI notes: played and slid to (−1: none; a lane's hits have no key)
    float        velocity = 0.8f;
    std::int32_t lane = -1;          // the lane's index, −1 for the pattern's notes
    bool operator==(const Hit&) const = default;
};
// Its hits and how many bars it is, or its problems (their columns in its text).
struct Hits {
    std::vector<Hit> hits;
    std::int32_t     bars = 0;
};
[[nodiscard]] std::expected<Hits, std::vector<Problem>> hits(const Pattern& pattern, const Music& music);

// A `*.music.omui.toml`, every problem at once, at its line and column with
// the nearest right word (its notes' problems too, at their very token).
[[nodiscard]] std::expected<Music, std::vector<Problem>> read_music(std::string_view text, std::string_view file);
// Written so that it reads back as the same piece.
[[nodiscard]] std::string write_music(const Music& music);

// ── Playing ─────────────────────────────────────────────────────────────

class Sequencer final : public Source {
public:
    // The piece's instruments made, its buses (`music.<name>`, under the
    // mixer's "music") and their effects added to `mixer`, and the sequencer
    // added to it as a source; nothing plays until `start`. A piece that does
    // not hold together answers its problems.
    [[nodiscard]] static std::expected<std::shared_ptr<Sequencer>, std::vector<Problem>> attach(const Music& music, Mixer& mixer);
    ~Sequencer() override;
    Sequencer(const Sequencer&) = delete;
    Sequencer& operator=(const Sequencer&) = delete;

    // From any thread, taking effect on the thread that renders:
    void start(std::int64_t at) noexcept;           // its intro, then its loop, from sample `at` of the mixer's clock
    void finish() noexcept;                         // its outro from the next bar, then nothing new (what sounds rings out)
    void set_intensity(float intensity) noexcept;   // 0–1 (1 at first): which layers play, from the next bar
    void set_gain(float gain) noexcept;             // over a block
    // Before rendering: an instrument kept silent (a tool hearing one alone).
    void mute(std::string_view instrument, bool muted = true);

    [[nodiscard]] float intensity() const noexcept;
    [[nodiscard]] std::int64_t bars() const noexcept;      // bars begun since the start
    [[nodiscard]] bool playing() const noexcept;
    [[nodiscard]] std::int32_t sounding() const noexcept;  // its voices sounding now
    // The sample of the mixer's clock bar `bar` (counted from the start) begins on.
    [[nodiscard]] std::int64_t bar_sample(std::int64_t bar) const noexcept;
    [[nodiscard]] const Music& music() const noexcept;

    void render(std::int64_t clock, std::int32_t frames, std::span<const BusBlock> buses) noexcept override;

private:
    struct Impl;
    explicit Sequencer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// An intensity from a time on (seconds from the start).
struct Cue {
    double at = 0;
    float  intensity = 1;
};
struct MusicRender {
    std::vector<float> samples;      // stereo interleaved
    LimiterStats       limiter;
};
// `seconds` of a piece rendered at once (offline: a tool's WAV, a test's
// bits), its intensity set by `cues`, the instruments in `solo` alone (all
// when empty).
[[nodiscard]] std::expected<MusicRender, std::vector<Problem>> render_music(const Music& music, double seconds, std::span<const Cue> cues = {},
                                                                           std::span<const std::string> solo = {}, Options options = {});

}  // namespace omui::audio
