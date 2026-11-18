// Quay heard.
//
//   auto sound = *quay::Sound::make(state);
//   quay::step(state, input);
//   sound.hear(state);                    // this step's events, as sounds in a place, at the watchkeeper's ears
//   sound.mixer().render(block);          // a host's device, or a test's buffer
//
// Each step's events become sounds where they happened: the noises the step made (the watchkeeper's steps and
// shots, the wardens' shots), what changed since the last step heard (a warden fallen, a hit taken, the beacon
// lit), and each walking warden's servos, which go with it. They are heard by meng.audio.space at the
// watchkeeper's ears, through the containers' steel — the rule the wardens hear by (`reach`). It is of the
// presentation, as the picture is: the state is read, and the game is the same game with or without it.
export module omui.showcase.quay.sound;

import std;
export import omui.showcase.quay.sim;
export import meng.audio.space;
export import :quay_sound;  // the sounds' data (sounds/quay.sound.xdef.ts, embedded)

export namespace quay {

class Sound {
public:
    // For a game begun as `state` (its walls: what sound passes through).
    [[nodiscard]] static std::expected<Sound, std::string> make(const State& state);
    // After a step: its events, heard from where the watchkeeper stands; how many sounds began. A state not
    // stepped since the last call is not heard again.
    std::uint32_t hear(const State& state);
    [[nodiscard]] meng::audio::Mixer& mixer() noexcept { return *mixer_; }
    [[nodiscard]] meng::audio::space::Space& space() noexcept { return *space_; }
    // A sound of the set by name (`rifle`, `warden-shot`, `step`, `servo`, `warden-down`, `hit`, `beacon`), as the
    // place numbers it (from 0); none: not in the set.
    [[nodiscard]] std::optional<std::uint32_t> sound(std::string_view name) const noexcept;

private:
    std::unique_ptr<meng::audio::Mixer>         mixer_;
    std::shared_ptr<meng::audio::space::Space>  space_;
    std::shared_ptr<const Statics>                 statics_;  // what the walls are of, kept
    std::vector<std::pair<std::string, std::uint32_t>> sounds_;
    std::vector<std::int32_t>                      health_;   // each warden's, the last step heard
    std::vector<meng::audio::space::EmitterId>  servos_;   // each warden's servos sounding (0: none)
    std::int32_t                                   keeper_health_ = 0;
    bool                                           lit_ = false;
    std::uint64_t                                  heard_ = std::numeric_limits<std::uint64_t>::max();
};

}  // namespace quay
