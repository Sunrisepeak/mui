module openmui.showcase.fighting;

import std;
import openmui.audio;
import gongfu;
import gongfu.present2d;

namespace omui::fighting {

namespace p2 = gongfu::present2d;

namespace {

// A kind's sounds when its profile names none.
std::array<std::string_view, 3> fallback(gongfu::Event::Kind k) {
    using K = gongfu::Event::Kind;
    switch (k) {
        case K::hit:
        case K::counter: return {"snap", "thud_medium", ""};
        case K::armor: return {"", "thud_heavy", ""};
        case K::block: return {"block_tick", "", ""};
        case K::throw_: return {"", "thud_heavy", "slam_tail"};
        case K::ko: return {"crack", "boom", "tail_long"};
        case K::clash: return {"clang", "", ""};
        case K::deflect: return {"ping", "", ""};
        case K::guard_break: return {"crack", "glass_shatter", "tail_short"};
        case K::piece_hit: return {"glass_tink", "", ""};
        case K::shatter: return {"glass_shatter", "", ""};
        case K::reflect: return {"ping", "", ""};
        default: return {"", "", ""};
    }
}

bool audible(gongfu::Event::Kind k) {
    using K = gongfu::Event::Kind;
    return k == K::hit || k == K::counter || k == K::armor || k == K::block || k == K::throw_ || k == K::ko || k == K::clash || k == K::deflect ||
           k == K::guard_break || k == K::piece_hit || k == K::shatter || k == K::reflect;
}

}  // namespace

Sounds::Sounds() : mixer_(std::make_unique<audio::Mixer>()) {
    for (const auto& [text, file] : {std::pair{files::fight_sound, "fight.sound.omui.toml"}, std::pair{files::menu_sound, "menu.sound.omui.toml"}})
        if (auto sounds = audio::read_sounds(text, file))
            for (const auto& s : *sounds) mixer_->add(s);
    mixer_->add_effect("sfx", audio::echo(0.11f, 0.22f, 0.18f, mixer_->rate()));  // the hall the stage stands in
    for (const auto& [file, text] : files::music_files) {  // each stage's theme, by the stage's name
        const std::string_view name = file.substr(0, file.find('.'));
        auto music = audio::read_music(text, file);
        if (!music) continue;
        if (auto seq = audio::Sequencer::attach(*music, *mixer_)) themes_.emplace(std::string(name), std::move(*seq));
    }
}

namespace {
std::function<std::shared_ptr<void>(Sounds&)>& hook() {
    static std::function<std::shared_ptr<void>(Sounds&)> h;
    return h;
}
}  // namespace

void hear_with(std::function<std::shared_ptr<void>(Sounds&)> open) { hook() = std::move(open); }
const std::function<std::shared_ptr<void>(Sounds&)>& hearing() { return hook(); }

void Sounds::restart(std::string_view theme) {
    base_ = mixer_->clock();
    freeze_ = 0;
    if (theme == theme_) return;  // the same stage: its music goes on
    if (const auto old = themes_.find(theme_); old != themes_.end()) old->second->finish();
    theme_ = theme;
    if (const auto now = themes_.find(theme_); now != themes_.end()) {
        now->second->set_intensity(0.25f);
        now->second->set_gain(1);
        now->second->start(base_);
    }
}

void Sounds::finish() {
    if (const auto it = themes_.find(theme_); it != themes_.end()) it->second->finish();
    theme_.clear();
}

void Sounds::theme(float intensity, float gain) {
    if (const auto it = themes_.find(theme_); it != themes_.end()) {
        it->second->set_intensity(std::clamp(intensity, 0.0f, 1.0f));
        it->second->set_gain(std::clamp(gain, 0.0f, 1.0f));
    }
}

void Sounds::cue(std::string_view name, std::int32_t frame, float gain, float pan) {
    play(name, live_ ? mixer_->clock() + audio::block_frames : base_ + static_cast<std::int64_t>(frame) * frame_samples(), gain, pan);
}

void Sounds::play(std::string_view name, std::int64_t at, float gain, float pan) {
    if (name.empty()) return;
    if (const std::int32_t id = mixer_->find(name); id >= 0) mixer_->play(id, at, gain, pan);
}

void Sounds::heard(const gongfu::Match& match, const p2::Look& left, const p2::Look& right) {
    const gongfu::State& st = match.state();
    // The music rises as the round gets close: with the lower health, and all of it under a super.
    if (const auto it = themes_.find(theme_); it != themes_.end()) {
        float low = 1;
        for (std::int32_t i = 0; i < 2; ++i)
            low = std::min(low, static_cast<float>(st.f[static_cast<std::size_t>(i)].health) / static_cast<float>(std::max(1, match.fighter(i).def.health)));
        it->second->set_intensity(st.freeze > 0 || st.lock.owner >= 0 ? 1.0f : std::clamp(0.25f + 0.75f * (1 - low), 0.0f, 1.0f));
    }
    const std::int64_t at = live_ ? mixer_->clock() + audio::block_frames : base_ + static_cast<std::int64_t>(st.frame) * frame_samples();
    const double half = match.system().stage_width.to_double() / 2;
    for (const gongfu::Event& e : match.events()) {
        if (!audible(e.kind)) continue;
        // The look that draws it: the attacker's, a pane's owner's.
        const std::int32_t by = e.kind == gongfu::Event::Kind::piece_hit ? 1 - e.attacker : e.attacker;
        const p2::Look& look = by == 1 ? right : left;
        const p2::Impact& im = p2::impact_for(look, e);
        const bool named = std::ranges::any_of(im.sound, [](const std::string& s) { return !s.empty(); });
        const auto kind = fallback(e.kind);
        const float pan = static_cast<float>(std::clamp((e.at.x.to_double() - half) / half, -1.0, 1.0) * 0.7);
        const float gain = e.kind == gongfu::Event::Kind::block ? 0.55f
                           : e.kind == gongfu::Event::Kind::ko ? 1.0f
                                                              : static_cast<float>(std::clamp(0.55 + e.damage / 2400.0, 0.4, 1.0));
        for (std::size_t k = 0; k < 3; ++k) play(named ? std::string_view(im.sound[k]) : kind[k], at, gain, pan);
    }
    // A super's flash: its sweep, from the side that flashed.
    if (st.freeze > 0 && freeze_ == 0) play("super_flash", at, 0.8f, 0.0f);
    freeze_ = st.freeze;
}

}  // namespace omui::fighting
