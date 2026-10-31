// openmui.showcase.fighting:sound — what the fight sounds like. Each contact's
// impact profile (in the attacker's look; a pane's in its owner's) names up
// to three sounds — attack, body, tail — from sounds/fight.sound.omui.toml;
// they play on the frame the simulation made them, panned by where on the
// stage, louder for more damage, and a super's flash has its sweep. Kinds a
// look leaves silent have their own (a clash's clang, a deflect's ping, glass
// for panes). Offline (no device: tests, a video's track) a sound starts on
// its frame's very sample, so a match's sound is as determined as its pixels;
// heard live it starts a block ahead of the device. The menus use it too:
// their cues (sounds/menu.sound.omui.toml) and the main theme.
export module openmui.showcase.fighting:sound;

import std;
import openmui.audio;
import gongfu;
import gongfu.present2d;

export namespace omui::fighting {

class Sounds {
public:
    Sounds();
    // After each frame of the match: its events' sounds, and a flash's.
    void heard(const gongfu::Match& match, const gongfu::present2d::Look& left, const gongfu::present2d::Look& right);
    // A new round: frame 0 is the mixer's clock now; `theme` the stage's music
    // (its name: "study", "street"; empty: none), from its intro if it is not playing already.
    void restart(std::string_view theme = {});
    // The theme playing (its name; empty: none).
    [[nodiscard]] std::string_view playing() const noexcept { return theme_; }
    // The theme ends: its outro from the next bar (a K.O.'s), then nothing new; frame 0 stays where it was.
    void finish();
    // The theme's intensity (0–1, its layers changing at the next bar) and its level (0–1, at once), for music no match drives.
    void theme(float intensity, float gain = 1);
    // A sound by its name (a menu's cue, an announcer's call) `later` frames after
    // frame `frame` since the last restart (live: after just ahead of the device).
    void cue(std::string_view name, std::int32_t frame, std::int32_t later = 0, float gain = 1, float pan = 0);
    // Heard live (a device pulls the mixer): sounds start just ahead of its clock instead of on their frame's sample.
    void live(bool on) noexcept { live_ = on; }

    [[nodiscard]] audio::Mixer& mixer() noexcept { return *mixer_; }
    // The fighters' voices played so far (a cry, a strain, a fall): what a test or an agent counts.
    [[nodiscard]] std::int64_t spoken() const noexcept { return spoken_; }
    // Samples a frame of the match lasts (rate / 60).
    [[nodiscard]] std::int64_t frame_samples() const noexcept { return mixer_->rate() / 60; }

private:
    void play(std::string_view name, std::int64_t at, float gain, float pan);

    // A fighter's voice (sounds/fighters.sound.omui.toml, by its look's name): what it says hurt, falling, striking.
    struct Voice {
        std::vector<std::int32_t> hurt, effort;
        std::int32_t              ko = -1;
    };
    void voice(const Voice* v, std::span<const std::int32_t> takes, std::int32_t pick, std::int64_t at, float pan);

    std::unique_ptr<audio::Mixer> mixer_;
    std::map<std::string, Voice, std::less<>> voices_;
    std::array<std::int32_t, 2>   hurt_at_{-1000, -1000}, effort_at_{-1000, -1000};
    std::int64_t                  spoken_ = 0;  // the frame each side last cried out, last strained
    // The stages' music, attached before anything plays; the one playing.
    std::map<std::string, std::shared_ptr<audio::Sequencer>, std::less<>> themes_;
    std::string                   theme_;
    std::int64_t                  base_ = 0;   // the mixer's sample of frame 0
    std::int32_t                  freeze_ = 0; // the last frame's
    bool                          live_ = false;
};

// How a program hears the fight: called with each stage's sounds as it
// mounts; what it returns (a device) the stage keeps, and lets go before its
// sounds. Tests and headless runs set none: they render the mixer themselves.
void hear_with(std::function<std::shared_ptr<void>(Sounds&)> open);
[[nodiscard]] const std::function<std::shared_ptr<void>(Sounds&)>& hearing();

}  // namespace omui::fighting
