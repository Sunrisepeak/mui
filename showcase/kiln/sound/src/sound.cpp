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
    state.world->events(events_);
    return sounds_->hear(events_, {.width = state.world->shape().width, .at = state.cat.x, .reach = reach, .when = mixer_->clock()});
}

}  // namespace kiln
