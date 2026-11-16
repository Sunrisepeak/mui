module omui.showcase.quay.sound;

import std;
import mlibs.math;

namespace quay {

namespace {
namespace audio = xengine::audio;
namespace space = xengine::audio::space;
namespace math = mlibs::math;

constexpr std::uint64_t servo_every = 38;  // steps between a walking warden's servo cycles
}  // namespace

std::expected<Sound, std::string> Sound::make(const State& state) {
    Sound made;
    made.mixer_ = std::make_unique<audio::Mixer>();
    auto place = space::Space::make(*made.mixer_);
    if (!place) return std::unexpected(place.error());
    made.space_ = *place;
    made.mixer_->add_source(made.space_);
    const auto sounds = audio::read_sounds(files::quay_sound, "quay.sound.xdef.json");
    if (!sounds) return std::unexpected(sounds.error().empty() ? std::string("quay.sound.xdef.json") : sounds.error().front().text());
    for (const audio::Sound& one : *sounds) made.sounds_.emplace_back(one.name, made.space_->add(one));
    made.statics_ = state.statics;
    if (made.statics_) made.space_->walls(made.statics_->walls);
    for (const Warden& warden : state.wardens) made.health_.push_back(warden.health);
    made.servos_.assign(state.wardens.size(), 0);
    made.keeper_health_ = state.keeper.health;
    // A small hard place: the quay's concrete and the containers' steel throw a shot back.
    made.mixer_->add_effect("sfx", audio::echo(0.11f, 0.22f, 0.2f, made.mixer_->rate()));
    return made;
}

std::optional<std::uint32_t> Sound::sound(std::string_view name) const noexcept {
    for (const auto& [known, id] : sounds_)
        if (known == name) return id;
    return std::nullopt;
}

std::uint32_t Sound::hear(const State& state) {
    if (state.frame == heard_) return 0;
    heard_ = state.frame;
    const Vec3 head = eye(state), facing = looking(state);
    space_->listen({.at = head, .forward = math::normalized(Vec3{facing.x, 0, facing.z})});
    std::uint32_t began = 0;
    const auto play = [&](std::string_view name, Vec3 at, float gain, std::string cause) {
        if (const auto id = sound(name)) {
            if (const auto emitted = space_->emit(*id, at, gain, std::move(cause))) {
                ++began;
                return *emitted;
            }
        }
        return space::EmitterId{0};
    };
    for (const Noise& noise : state.noises) {
        if (noise.what == "footstep") (void)play("step", noise.at + Vec3{0, 0.05f, 0}, noise.loudness > -35 ? 1.0f : 0.6f, "the watchkeeper's step");
        else if (noise.what == "shot") (void)play("rifle", noise.at, 1, "the watchkeeper's rifle");
        else if (noise.what == "horn") (void)play("horn", noise.at, 1, std::format("lookout {} called", noise.by - 1000 + 1));
        else if (noise.what == "fuse") (void)play("fuse", noise.at, 1, "a fuse box switched on");
        else if (noise.what == "warden shot") (void)play("warden-shot", noise.at, 1, std::format("warden {} fired", noise.by));
    }
    for (std::size_t index = 0; index < state.wardens.size() && index < health_.size(); ++index) {
        const Warden& warden = state.wardens[index];
        const Vec3 chest = warden.at + Vec3{0, 1.2f, 0};
        if (warden.health <= 0 && health_[index] > 0) (void)play("warden-down", chest, 1, std::format("warden {} fell", index + 1));
        health_[index] = warden.health;
        // Its servos while it walks, going with it.
        const bool walking = warden.health > 0 && !(warden.mind == Mind::search && math::length(Vec3{warden.heading_for.x - warden.at.x, 0, warden.heading_for.z - warden.at.z}) < 1.5f);
        if (servos_[index] && !space_->move(servos_[index], chest)) servos_[index] = 0;
        if (walking && (state.frame + index * 11) % servo_every == 0) servos_[index] = play("servo", chest, warden.mind == Mind::chase ? 1.0f : 0.7f, std::format("warden {} walking", index + 1));
    }
    if (state.keeper.health < keeper_health_) (void)play("hit", head + facing * 0.3f, 1, "the watchkeeper hit");
    keeper_health_ = state.keeper.health;
    if (state.outcome == Outcome::won && !lit_) lit_ = true, (void)play("beacon", state.beacon + Vec3{0, beacon_height, 0}, 1, "the beacon lit");
    return began;
}

}  // namespace quay
