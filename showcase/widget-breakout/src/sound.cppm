// openmui.showcase.widget_breakout:sound — what the breakout sounds like
// (sounds/breakout.sound.omui.toml). Each brick sounds as the component it
// is — a button's click, a switch's snap, a capacitor charging a step higher
// each hit and its beam's roar; a broken brick pops a note further up a
// pentatonic scale the longer the chain, so a chain is a run of notes; a
// banked chain is a chord, a lost one slides down; the core knocks and,
// broken, the panel powers down. The paddle knocks, the walls tap, kickers
// thump, a capsule chimes and its catch rises, a lost ball falls. They play
// on the step the simulation made them, panned by where across the field:
// offline (tests, a video's track) on the step's very sample, live a block
// ahead of the device. Seeking back is silent; played on from there, the new
// branch is heard after what was (the sound's time only goes forward).
export module openmui.showcase.widget_breakout:sound;

import std;
import openmui.audio;
import openmui.showcase.widget_breakout.sim;
import :rooms;

export namespace omui::breakout {

class Sounds {
public:
    Sounds();
    // After a step played forward: its events' sounds (`room`: what its bricks are).
    void heard(const State& s, const RoomDef& room);
    // A sound of the program's own (a gate opening, a rewind, a scan), now.
    void cue(std::string_view name, float pan = 0);
    // A new level: its step 0 is the mixer's clock now.
    void restart();
    // Heard live (a device pulls the mixer): sounds start just ahead of its clock instead of on their step's sample.
    void live(bool on) noexcept { live_ = on; }

    [[nodiscard]] audio::Mixer& mixer() noexcept { return *mixer_; }
    // Samples a step lasts (rate / 120).
    [[nodiscard]] std::int64_t step_samples() const noexcept { return mixer_->rate() / steps_per_second; }

private:
    void play(std::string_view name, std::int64_t at, float gain, float pan, float pitch = 1);

    std::unique_ptr<audio::Mixer> mixer_;
    std::int64_t                  base_ = 0;  // the mixer's sample of step 0
    std::int64_t                  played_ = 0;  // steps heard since step 0
    bool                          live_ = false;
};

// How a program hears the game: called with each field's sounds as it
// mounts; what it returns (a device) the field keeps, and lets go before its
// sounds. Tests and headless runs set none: they render the mixer themselves.
void hear_with(std::function<std::shared_ptr<void>(Sounds&)> open);
[[nodiscard]] const std::function<std::shared_ptr<void>(Sounds&)>& hearing();

}  // namespace omui::breakout
