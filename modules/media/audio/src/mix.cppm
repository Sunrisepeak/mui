// openmui.audio:mix — sounds as data and the mixer: voices mixed into buses,
// each bus through its effects (and, routed, into another bus) into the
// master, a block at a time with nothing allocated; sources (a sequencer's
// music) add their own voices into the buses on the same clock.
//
// A sound (`*.sound.omui.toml`) is layers, each a source — a sine, triangle,
// square or saw oscillator, or noise — gliding in pitch, through a filter
// whose cutoff glides too, shaped by an envelope, driven, gained and panned.
// A punch is a burst of low-passed noise over a falling thump; a bell is a
// few sines and their overtones.
export module openmui.audio:mix;

import std;

export namespace omui::audio {

inline constexpr std::int32_t block_frames = 128;  // samples a block, each channel

enum class Wave : std::uint8_t { sine, triangle, square, saw, noise, recording };
enum class FilterKind : std::uint8_t { none, lowpass, highpass, bandpass };

// A layer's loudness over its length (seconds; straight lines between): up
// over `attack`, held for `hold`, down to `sustain` over `decay`, and over the
// last `release` down to silence.
struct Envelope {
    float attack = 0.002f;
    float hold = 0;
    float decay = 0.1f;
    float sustain = 0;
    float release = 0.02f;
};

struct Layer {
    Wave                 wave = Wave::sine;
    float                at = 0;            // seconds after the sound begins
    float                length = 0.2f;     // seconds
    std::array<float, 2> pitch{220, 220};   // Hz at its start and at its end (an exponential glide)
    Envelope             envelope;
    FilterKind           filter = FilterKind::none;
    std::array<float, 2> cutoff{2000, 2000};  // Hz, start → end (exponential)
    float                q = 0.707f;
    float                gain = 1;          // linear
    float                pan = 0;           // −1 left … 1 right
    float                drive = 0;         // soft clipping: 0 clean … 1 hard
    std::uint32_t        seed = 1;          // the noise's
    // wave = recording: the recording it plays (by its name, Mixer::add_recording)
    // at `speed` (2: twice as quick, an octave up; start → end, an exponential
    // glide) — a voice, a foley take; length 0 lasts as long as the recording
    // (a file's recording layer is 0 and held, sustain 1, unless it says).
    std::string          recording;
    std::array<float, 2> speed{1, 1};
};

struct Sound {
    std::string        name;
    std::vector<Layer> layers;
    float              gain = 1;
    std::string        bus = "sfx";
    [[nodiscard]] float length() const noexcept;  // seconds, its last layer's end
};

// A problem in a file, where it is, with the nearest right word when there is one.
struct Problem {
    std::string file;
    int         line = 0, column = 0;
    std::string message, hint;
    [[nodiscard]] std::string text() const;
};
// The sounds of a `*.sound.omui.toml` (`[sound.<name>]` tables), every problem at once.
[[nodiscard]] std::expected<std::vector<Sound>, std::vector<Problem>> read_sounds(std::string_view text, std::string_view file);
// Written so that it reads back as the same sounds.
[[nodiscard]] std::string write_sounds(std::span<const Sound> sounds);

// What a bus does to what reaches it, a block at a time (a plugin: the
// built-ins below, or a program's own). Called on the thread that renders;
// it must not allocate.
class Effect {
public:
    virtual ~Effect() = default;
    // `left` and `right`, `frames` each, in place.
    virtual void process(std::span<float> left, std::span<float> right, std::int32_t rate) noexcept = 0;
};
// A filter over both channels (its cutoff fixed).
[[nodiscard]] std::unique_ptr<Effect> filter(FilterKind kind, float cutoff, float q = 0.707f);
// An echo: `time` seconds later, `feedback` of it again, `wet` of it mixed in.
[[nodiscard]] std::unique_ptr<Effect> echo(float time, float feedback, float wet, std::int32_t rate);

// An effect as data (a piece's buses carry them): a filter, an echo, a
// reverb or a chorus, with what each kind reads.
enum class EffectKind : std::uint8_t { lowpass, highpass, bandpass, echo, reverb, chorus };
struct EffectSpec {
    EffectKind   kind = EffectKind::reverb;
    float        wet = 0.3f, dry = 1;        // echo, reverb, chorus: what of the effect and of the input is heard
    float        cutoff = 2000, q = 0.707f;  // the filters
    float        time = 0.25f;               // echo: seconds later …
    float        beats = 0;                  // … or beats of a piece's tempo, when not 0
    float        feedback = 0.3f;            // echo: of it again
    bool         pingpong = false;           // echo: from side to side
    float        size = 0.75f;               // reverb: the room (0 small … 1 a hall)
    float        damping = 0.4f;             // reverb: the highs its walls take (0–1)
    float        width = 1;                  // reverb: stereo (0 mono … 1 wide)
    float        predelay = 0.01f;           // reverb: seconds before it answers
    float        delay = 0.012f;             // chorus: its voices' delay (seconds) …
    float        depth = 0.003f;             // … swinging by this much (seconds) …
    float        rate = 0.5f;                // … this often (Hz)
    std::int32_t voices = 2;                 // chorus: 1–4
    bool operator==(const EffectSpec&) const = default;
};
// The effect `spec` describes; `beat` is a beat's seconds (an echo in beats).
[[nodiscard]] std::unique_ptr<Effect> effect(const EffectSpec& spec, std::int32_t rate, double beat = 0.5);

// A bus pushed down while another sounds (sidechain ducking: the music under
// the hits, a pad pumping under its kick).
struct Duck {
    std::string key;               // the bus whose sound pushes it down (its level after its effects, before its gain)
    float       depth = 0.5f;      // the most it takes off (0–1 of the level)
    float       threshold = 0.25f; // the key's level (peak) that takes all of `depth`
    float       attack = 0.005f;   // seconds to go down …
    float       release = 0.2f;    // … and to come back
    bool operator==(const Duck&) const = default;
};

// A bus's block, `block_frames` samples a channel, for a source to add into.
struct BusBlock {
    std::span<float> left, right;
};

// What adds its own voices into the mixer's buses, a block at a time on the
// thread that renders, before the buses' effects (a plugin: a sequencer's
// music, a program's own synthesis). It must not allocate.
class Source {
public:
    virtual ~Source() = default;
    // Samples [clock, clock + frames) of the mixer's clock, added into
    // `buses` (by the indices `Mixer::bus` answers).
    virtual void render(std::int64_t clock, std::int32_t frames, std::span<const BusBlock> buses) noexcept = 0;
};

// How hard the master's limiter has worked so far.
struct LimiterStats {
    float        deepest = 1;  // the lowest gain it applied (1: never)
    std::int64_t frames = 0;   // frames it turned down
};

struct Options {
    std::int32_t rate = 48000;
    std::int32_t voices = 48;   // sounding at once; a new one past that takes the oldest's place
    std::int32_t queue = 256;   // plays waiting between the thread that plays and the one that renders
    float        ceiling = 0.89f;  // the master's limiter: peaks held under this (−1 dBFS)
};

class Mixer {
public:
    explicit Mixer(Options options = {});
    ~Mixer();
    Mixer(const Mixer&) = delete;
    Mixer& operator=(const Mixer&) = delete;

    // A sound to play: its tables are built now, so nothing is allocated
    // while it plays. Answers its index (a sound of the same name replaces it).
    std::int32_t add(const Sound& sound);
    [[nodiscard]] std::int32_t find(std::string_view name) const noexcept;  // −1: none
    // A recording (mono samples at `rate` a second) for the layers that name
    // it to play; added before the sounds that do (a recording of the same
    // name replaces it, for sounds added after).
    void add_recording(std::string_view name, std::span<const float> samples, std::int32_t rate);
    // A bus by name (made on first use; "sfx" and "music" exist): its gain
    // and its effects, in order, before the master.
    void bus_gain(std::string_view bus, float gain);
    void add_effect(std::string_view bus, std::unique_ptr<Effect> effect);
    // Its index (made on first use), what a source adds into.
    std::int32_t bus(std::string_view name);
    // Into another bus (after its effects and gain) instead of the master.
    void route(std::string_view bus, std::string_view to);
    // Pushed down while `duck.key` sounds.
    void duck(std::string_view bus, const Duck& duck);
    // A source, rendered with every block from now on. Like buses and effects,
    // set up before rendering (not while another thread renders).
    void add_source(std::shared_ptr<Source> source);

    // Sound `sound` from sample `at` of the mixer's clock (one already past
    // starts at once), with `gain` and `pan` on top of its own and its pitch
    // times `pitch`. One thread may play while another renders; false when
    // the queue between them is full.
    bool play(std::int32_t sound, std::int64_t at, float gain = 1, float pan = 0, float pitch = 1) noexcept;
    // The next frames, stereo interleaved (out.size() / 2 of them); the clock moves on by as many.
    void render(std::span<float> out) noexcept;

    [[nodiscard]] std::int64_t clock() const noexcept;
    [[nodiscard]] std::int32_t rate() const noexcept;
    [[nodiscard]] std::int32_t sounding() const noexcept;  // voices playing now
    [[nodiscard]] std::int64_t begun() const noexcept;     // plays begun so far
    [[nodiscard]] LimiterStats limiter() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// `frames` of a mixer rendered at once (offline: a video's track, a test's).
[[nodiscard]] std::vector<float> render(Mixer& mixer, std::int64_t frames);
// A WAV file of interleaved stereo: 16-bit PCM, or 32-bit float.
[[nodiscard]] std::vector<std::byte> wav(std::span<const float> interleaved, std::int32_t rate, bool float32 = false);
// A recording read from a WAV file: 8-, 16-, 24-bit PCM or 32-bit float, its
// channels mixed down to one.
struct Recording {
    std::vector<float> samples;
    std::int32_t       rate = 48000;
};
[[nodiscard]] std::expected<Recording, std::string> read_wav(std::span<const std::byte> file);
// FNV-1a over the samples' bits: what a test pins.
[[nodiscard]] std::uint64_t hash(std::span<const float> samples) noexcept;
// The loudest sample's magnitude.
[[nodiscard]] float peak(std::span<const float> samples) noexcept;

// How loud stereo interleaved samples are, and where their energy is (what
// a mix is checked by: a tool's numbers, a test's bounds).
struct Levels {
    float                 peak = 0;          // the loudest sample (linear)
    double                rms = -200;        // dBFS, both channels
    double                loudness = -200;   // LUFS, integrated (ITU-R BS.1770: K-weighted, gated)
    // The energy of each band as dB of the whole: below 80 Hz, 80–250,
    // 250–2000, 2000–6000 and above 6000.
    std::array<double, 5> bands{-200, -200, -200, -200, -200};
    double                correlation = 1;   // left against right (1 mono … 0 unrelated … −1 opposed)
};
[[nodiscard]] Levels levels(std::span<const float> interleaved, std::int32_t rate);

}  // namespace omui::audio
