// openmui.showcase.fighting:sound — what the fight sounds like. Each contact's
// impact profile (in the attacker's look; a pane's in its owner's) names up
// to three sounds — attack, body, tail — from sounds/fight.sound.omui.toml;
// they play on the frame the simulation made them, panned by where on the
// stage, louder for more damage, and a super's flash has its sweep. Kinds a
// look leaves silent have their own (a clash's clang, a deflect's ping, glass
// for panes). Offline (no device: tests, a video's track) a sound starts on
// its frame's very sample, so a match's sound is as determined as its pixels;
// heard live it starts a block ahead of the device.
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
    // Heard live (a device pulls the mixer): sounds start just ahead of its clock instead of on their frame's sample.
    void live(bool on) noexcept { live_ = on; }

    [[nodiscard]] audio::Mixer& mixer() noexcept { return *mixer_; }
    // Samples a frame of the match lasts (rate / 60).
    [[nodiscard]] std::int64_t frame_samples() const noexcept { return mixer_->rate() / 60; }

private:
    void play(std::string_view name, std::int64_t at, float gain, float pan);

    std::unique_ptr<audio::Mixer> mixer_;
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
