module openmui.showcase.widget_breakout;

import std;
import openmui.audio;
import openmui.showcase.widget_breakout.sim;

namespace omui::breakout {

namespace {

// A brick's sound when hit, by what it is.
std::string_view hit_sound(Kind k) {
    switch (k) {
        case Kind::toggle: return "flip";
        case Kind::slider: return "tick";
        case Kind::wall:
        case Kind::gate: return "wall";
        case Kind::button:
        case Kind::card:
        case Kind::hidden: break;
    }
    return "click";
}

std::function<std::shared_ptr<void>(Sounds&)>& hook() {
    static std::function<std::shared_ptr<void>(Sounds&)> h;
    return h;
}

}  // namespace

void hear_with(std::function<std::shared_ptr<void>(Sounds&)> open) { hook() = std::move(open); }
const std::function<std::shared_ptr<void>(Sounds&)>& hearing() { return hook(); }

Sounds::Sounds() : mixer_(std::make_unique<audio::Mixer>()) {
    auto sounds = audio::read_sounds(files::breakout_sound, "breakout.sound.omui.toml");
    if (sounds)
        for (const auto& s : *sounds) mixer_->add(s);
    mixer_->add_effect("sfx", audio::echo(0.08f, 0.18f, 0.15f, mixer_->rate()));  // a small room
}

void Sounds::restart() {
    base_ = mixer_->clock();
    played_ = 0;
}

void Sounds::play(std::string_view name, std::int64_t at, float gain, float pan, float pitch) {
    if (const std::int32_t id = mixer_->find(name); id >= 0) mixer_->play(id, at, gain, pan, pitch);
}

void Sounds::cue(std::string_view name, float pan) { play(name, mixer_->clock() + (live_ ? audio::block_frames : 0), 1, pan); }

void Sounds::heard(const State& s, const LevelDef& level) {
    ++played_;  // the steps played forward: after a rewind the sound goes on, it does not go back
    const std::int64_t at = live_ ? mixer_->clock() + audio::block_frames : base_ + played_ * step_samples();
    for (std::uint32_t k = 0; k < s.events; ++k) {
        const Event& e = s.event[k];
        const float pan = std::clamp(static_cast<float>(e.at.x / field_w) * 2 - 1, -1.0f, 1.0f) * 0.7f;
        const Kind kind = e.id < level.bricks.size() ? level.bricks[e.id].kind : Kind::button;
        switch (e.what) {
            case Happened::hit: play(hit_sound(kind), at, 1, pan); break;
            case Happened::broke: play("pop", at, 1, pan, 1 + 0.06f * static_cast<float>(std::min(s.combo, 12))); break;  // higher with the combo
            case Happened::paddle: play("paddle", at, 1, pan); break;
            case Happened::wall: play("wall", at, 0.8f, pan); break;
            case Happened::lost: play("lost", at, 1, pan); break;
            case Happened::gift: play("chime", at, 1, pan); break;
            case Happened::caught: play("catch", at, 1, pan); break;
            case Happened::cleared: play("cleared", at, 1, 0); break;
            case Happened::launched: play("launch", at, 1, pan); break;
            case Happened::life:
            case Happened::over: break;
        }
    }
}

}  // namespace omui::breakout
