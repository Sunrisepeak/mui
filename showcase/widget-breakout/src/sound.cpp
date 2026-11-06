module omui.showcase.widget_breakout;

import std;
import xengine.audio;
import omui.showcase.widget_breakout.sim;

namespace omui::breakout {

namespace {

// A brick's sound when hit, by what it is.
std::string_view hit_sound(Kind k) {
    switch (k) {
        case Kind::toggle: return "flip";
        case Kind::slider: return "tick";
        case Kind::wall:
        case Kind::gate:
        case Kind::shutter: return "wall";
        case Kind::kicker: return "kick";
        case Kind::core: return "core";
        case Kind::button:
        case Kind::card:
        case Kind::hidden: break;
    }
    return "click";
}

// A chain's notes: a major pentatonic scale up from the pop's own pitch (exact ratios: the same everywhere).
constexpr std::array<float, 15> scale{1.0f, 1.125f, 1.25f, 1.5f, 1.6667f, 2.0f, 2.25f, 2.5f, 3.0f, 3.3333f, 4.0f, 4.5f, 5.0f, 6.0f, 6.6667f};
float note_of(std::int32_t chain) { return scale[static_cast<std::size_t>(std::clamp(chain, 0, static_cast<std::int32_t>(scale.size()) - 1))] * 0.5f; }

std::function<std::shared_ptr<void>(Sounds&)>& hook() {
    static std::function<std::shared_ptr<void>(Sounds&)> h;
    return h;
}

}  // namespace

void hear_with(std::function<std::shared_ptr<void>(Sounds&)> open) { hook() = std::move(open); }
const std::function<std::shared_ptr<void>(Sounds&)>& hearing() { return hook(); }

Sounds::Sounds() : mixer_(std::make_unique<xengine::audio::Mixer>()) {
    auto sounds = xengine::audio::read_sounds(files::breakout_sound, "breakout.sound.omui.toml");
    if (sounds)
        for (const auto& s : *sounds) mixer_->add(s);
    mixer_->add_effect("sfx", xengine::audio::echo(0.08f, 0.18f, 0.15f, mixer_->rate()));  // a small room
    if (auto music = xengine::audio::read_music(files::haunt_music, "haunt.music.omui.toml"))
        if (auto seq = xengine::audio::Sequencer::attach(*music, *mixer_)) theme_ = std::move(*seq);
}

void Sounds::restart() {
    base_ = mixer_->clock();
    played_ = 0;
    if (theme_ && !theme_->playing()) {
        theme_->set_intensity(0.2f);
        theme_->start(base_);
    }
}

float Sounds::intensity() const noexcept { return theme_ ? theme_->intensity() : 0.0f; }

void Sounds::play(std::string_view name, std::int64_t at, float gain, float pan, float pitch) {
    if (const std::int32_t id = mixer_->find(name); id >= 0) mixer_->play(id, at, gain, pan, pitch);
}

void Sounds::cue(std::string_view name, float pan) { play(name, mixer_->clock() + (live_ ? xengine::audio::block_frames : 0), 1, pan); }

void Sounds::heard(const State& s, const RoomDef& room) {
    ++played_;  // the steps played forward: after a rewind the sound goes on, it does not go back
    // The theme rises with the chain (a part every few bricks), on the last ball, and with several in the air.
    if (theme_) {
        std::int32_t flying = 0;
        s.world.each<Ball>([&](xengine::Entity, const Ball& b) { flying += b.on == 0 ? 1 : 0; });
        const float chain = 0.06f * static_cast<float>(std::min(s.combo, 10));
        const float last = s.lives <= 1 ? 0.2f : 0.0f;
        const float many = flying > 1 ? 0.25f : 0.0f;
        theme_->set_intensity(s.over ? 0.2f : std::clamp(0.2f + chain + last + many, 0.0f, 1.0f));
    }
    const std::int64_t at = live_ ? mixer_->clock() + xengine::audio::block_frames : base_ + played_ * step_samples();
    for (std::uint32_t k = 0; k < s.events; ++k) {
        const Event& e = s.event[k];
        const float pan = std::clamp(static_cast<float>(e.at.x / field_w) * 2 - 1, -1.0f, 1.0f) * 0.7f;
        const Kind kind = e.id < room.bricks.size() ? room.bricks[e.id].kind : Kind::button;
        switch (e.what) {
            case Happened::hit: play(hit_sound(kind), at, 1, pan); break;
            case Happened::broke:
                if (e.n < 0) play("pop", at, 0.35f, pan, note_of(static_cast<std::int32_t>(k % 8)));  // the shutdown's wave: soft
                else play("pop", at, 1, pan, note_of(e.n));                                           // a chain's next note
                break;
            case Happened::paddle: play("paddle", at, 1, pan); break;
            case Happened::wall: play("wall", at, 0.8f, pan); break;
            case Happened::lost: play("lost", at, 1, pan); break;
            case Happened::gift: play("chime", at, 1, pan); break;
            case Happened::caught: play("catch", at, 1, pan); break;
            case Happened::cleared: play("cleared", at, 1, 0); break;
            case Happened::launched: play("launch", at, 1, pan); break;
            case Happened::flipped: play("snap", at, 1, pan); break;
            case Happened::wired: play("wire", at, 0.6f, std::clamp(static_cast<float>(e.to.x / field_w) * 2 - 1, -1.0f, 1.0f) * 0.7f); break;
            case Happened::charged: play("charge", at, 1, pan, 0.8f + 0.25f * static_cast<float>(e.n)); break;
            case Happened::armed: play("armed", at, 1, pan); break;
            case Happened::beam: play("beam", at, 1, 0, 1 + 0.08f * static_cast<float>(e.n)); break;
            case Happened::kicked: play("kick", at, 1, pan); break;
            case Happened::core_hit: play("core", at, 1, pan); break;
            case Happened::shutdown: play("shutdown", at, 1, 0); break;
            case Happened::breach: play(e.n ? "breach" : "whoosh", at, 1, pan); break;
            case Happened::banked: play("bank", at, 1, pan, e.n >= 10 ? 1.5f : 1.0f); break;
            case Happened::chain_lost: play("chain_lost", at, 1, pan); break;
            case Happened::magnet: play("magnet", at, 1, pan); break;
            case Happened::duet: play("duet", at, 0.7f, pan, 1 + 0.12f * static_cast<float>(std::min(e.n, duet_full))); break;
            case Happened::finisher: play("armed", at, 1, pan, 1.5f); break;
            case Happened::cache: play("chime", at, 1, pan, 1.5f); break;
            case Happened::life:
            case Happened::over: break;
        }
    }
}

}  // namespace omui::breakout
