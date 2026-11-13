module omui.showcase.kiln.sound;

import std;

namespace kiln {

std::expected<Sound, std::string> Sound::make() {
    Sound sound;
    sound.mixer_ = std::make_unique<xengine::audio::Mixer>();
    auto sounds = xengine::audio::matter::Sounds::make(matter(), *sound.mixer_);
    if (!sounds) return std::unexpected(sounds.error());
    sound.sounds_.emplace(std::move(*sounds));
    sound.events_.reserve(256);
    return sound;
}

std::uint32_t Sound::hear(const State& state) {
    events_.clear();
    // A world that did not step since (the game is over, or paused) has nothing new to say.
    if (state.world->frames() == heard_) return 0;
    heard_ = state.world->frames();
    state.world->events(events_);
    const xengine::audio::matter::Ear ear{.width = state.world->shape().width, .at = state.cat.x, .reach = reach, .when = mixer_->clock()};
    return sounds_->hear(events_, ear) + sounds_->hear(state.world->impacts(), ear);
}

}  // namespace kiln
