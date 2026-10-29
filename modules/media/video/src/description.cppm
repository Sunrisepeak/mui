// openmui.video:description — a video as data (`*.video.omui.toml`): its
// size, rate and length, where its frames come from and what its sound
// track is; read with every problem at its line and column, written back as
// itself.
//
//     doc = "Three seconds of the sync card."
//
//     [video]
//     width = 1280
//     height = 720
//     fps = 30                     # or "30000/1001"
//     seconds = 3                  # or frames = 90
//
//     [frames]
//     card = "sync"                # or png = "shots/{:05}.png", or view = "intro.view.omui.json"
//
//     [audio]                      # none: no sound track
//     rate = 48000
//     music = "theme.music.omui.toml"
//     intensity = [{ at = 0, value = 0.3 }, { at = 2, value = 1 }]   # or one number
//     sounds = "ui.sound.omui.toml"
//     recordings = { voice = "voice.wav" }    # for the sounds' recording layers
//     cues = [{ sound = "click", frame = 30 }, { sound = "whoosh", at = 1.25, gain = 0.8, pan = -0.5 }]
//     beeps = true                 # the sync card's
//
// Files are relative to the description. A view's frames are a host's to
// render (the omui command line); this package draws the card and reads PNGs.
export module openmui.video:description;

import std;
import hanim;
import openmui.audio;
import :timing;
import :encode;

export namespace omui::video {

using Problem = audio::Problem;

// A sound played at a time of the video: `frame`'s first sample when it is
// given (≥ 0), else `at` seconds.
struct SoundCue {
    std::string  sound;
    double       at = 0;
    std::int64_t frame = -1;
    float        gain = 1, pan = 0, pitch = 1;
    bool operator==(const SoundCue&) const = default;
};

// The music's intensity from a time on (seconds).
struct Intensity {
    double at = 0;
    float  value = 1;
    bool operator==(const Intensity&) const = default;
};

enum class FramesFrom : std::uint8_t { card, png, view };

struct Description {
    std::string      doc;
    std::int32_t     width = 1280, height = 720;
    hanim::FrameRate fps{30, 1};
    double           seconds = 0;   // as written (0: `frames` was)
    std::int64_t     frames = 0;    // how many (from `seconds` when it was written)

    FramesFrom  from = FramesFrom::card;
    std::string source = "sync";    // the card's name, the PNGs' pattern, the view's file

    bool                               audio = false;  // an [audio] table: a sound track
    std::int32_t                       rate = 48000;
    std::string                        music;
    std::vector<Intensity>             intensity;
    std::string                        sounds;
    std::map<std::string, std::string> recordings;
    std::vector<SoundCue>              cues;
    bool                               beeps = false;

    [[nodiscard]] Format format() const noexcept {
        return {.width = width, .height = height, .timing = {fps, rate}, .frames = frames, .channels = audio ? 2 : 0};
    }
    bool operator==(const Description&) const = default;
};

[[nodiscard]] std::expected<Description, std::vector<Problem>> read_description(std::string_view text, std::string_view file);
// Written so that it reads back as the same description (the one way it is written).
[[nodiscard]] std::string write_description(const Description& description);

// The sound track, offline (the same bits everywhere): the music (its
// intensity cued), each cue's sound on its exact sample, the recordings the
// sounds play, through openmui.audio's mixer and its limiter; then the
// card's beeps. Stereo, format().samples() long; empty without [audio].
// Files are read relative to `base`.
[[nodiscard]] std::expected<std::vector<float>, std::vector<Problem>> soundtrack(const Description& description, const std::filesystem::path& base);

// The card, or the PNG files (each the video's size; relative to `base`).
// A view is a host's: a problem here.
[[nodiscard]] std::expected<std::unique_ptr<FrameSource>, std::vector<Problem>> frame_source(const Description& description,
                                                                                            const std::filesystem::path& base);

// A file's name from a frame pattern ("shots/{:05}.png" and 7 → "shots/00007.png"):
// one {} with an optional zero-padded width; nothing when the pattern is not one.
[[nodiscard]] std::optional<std::string> frame_name(std::string_view pattern, std::int64_t index);

}  // namespace omui::video
