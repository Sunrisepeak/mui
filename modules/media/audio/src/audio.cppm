// openmui.audio — sound for openmui and its games (D-86): sounds as data,
// voices mixed into buses, each bus through its effects into the master,
// rendered a block at a time with nothing allocated, every sample a function
// of what was played and when — so an offline render is the same bits on
// every platform (a match's audio pinned like its pixels). Devices and
// codecs are plugins (`openmui.audio.device`).
//
// A sound (`*.sound.omui.toml`) is layers, each a source — a sine, triangle,
// square or saw oscillator, or noise — gliding in pitch, through a filter
// whose cutoff glides too, shaped by an envelope, driven, gained and panned.
// A punch is a burst of low-passed noise over a falling thump; a bell is a
// few sines and their overtones.
export module openmui.audio;

import std;

export namespace omui::audio {

inline constexpr std::int32_t block_frames = 128;  // samples a block, each channel

enum class Wave : std::uint8_t { sine, triangle, square, saw, noise };
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
    // A bus by name (made on first use; "sfx" and "music" exist): its gain
    // and its effects, in order, before the master.
    void bus_gain(std::string_view bus, float gain);
    void add_effect(std::string_view bus, std::unique_ptr<Effect> effect);

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

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// `frames` of a mixer rendered at once (offline: a video's track, a test's).
[[nodiscard]] std::vector<float> render(Mixer& mixer, std::int64_t frames);
// A WAV file of interleaved stereo: 16-bit PCM, or 32-bit float.
[[nodiscard]] std::vector<std::byte> wav(std::span<const float> interleaved, std::int32_t rate, bool float32 = false);
// FNV-1a over the samples' bits: what a test pins.
[[nodiscard]] std::uint64_t hash(std::span<const float> samples) noexcept;
// The loudest sample's magnitude.
[[nodiscard]] float peak(std::span<const float> samples) noexcept;

}  // namespace omui::audio
