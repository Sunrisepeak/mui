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
    for (const auto& [file, data] : files::voice_files)  // the announcer's recordings, by their files' names, before the sounds that play them
        if (auto r = audio::read_wav(std::as_bytes(std::span(data.data(), data.size()))))
            mixer_->add_recording(file.substr(0, file.find('.')), r->samples, r->rate);
    for (const auto& [text, file] : {std::pair{files::fight_sound, "fight.sound.omui.toml"}, std::pair{files::menu_sound, "menu.sound.omui.toml"},
                                     std::pair{files::voice_sound, "voice.sound.omui.toml"}, std::pair{files::fighters_sound, "fighters.sound.omui.toml"}})
        if (std::string_view(file) == "voice.sound.omui.toml" && files::voice_files.empty()) continue;  // no announcer without the arenas
        else if (auto sounds = audio::read_sounds(text, file))
            for (const auto& s : *sounds) {
                const std::int32_t id = mixer_->add(s);
                // A fighter's voice by its name: <fighter>_hurt_<n>, <fighter>_ko, <fighter>_effort_<n>.
                const std::string_view n = s.name;
                const auto cut = n.find('_');
                if (std::string_view(file) != "fighters.sound.omui.toml" || cut == std::string_view::npos) continue;
                Voice& v = voices_[std::string(n.substr(0, cut))];
                const std::string_view kind = n.substr(cut + 1);
                if (kind == "ko") v.ko = id;
                else if (kind.starts_with("hurt_")) v.hurt.push_back(id);
                else if (kind.starts_with("effort_")) v.effort.push_back(id);
            }
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
    hurt_at_ = effort_at_ = {-1000, -1000};
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

void Sounds::cue(std::string_view name, std::int32_t frame, std::int32_t later, float gain, float pan) {
    const std::int64_t now = live_ ? mixer_->clock() + audio::block_frames : base_ + static_cast<std::int64_t>(frame) * frame_samples();
    play(name, now + static_cast<std::int64_t>(later) * frame_samples(), gain, pan);
}

void Sounds::voice(const Voice* v, std::span<const std::int32_t> takes, std::int32_t pick, std::int64_t at, float pan) {
    if (!v || takes.empty()) return;
    if (mixer_->play(takes[static_cast<std::size_t>(pick) % takes.size()], at, 1, pan)) ++spoken_;
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
    // The fighters' voices: a cry from whom a heavy blow or a counter lands on
    // (not every blow of a combo: a breath between), a strain with one's own
    // heavy blows, and the fall at a K.O. Which take, from the frame: the same
    // fight says the same.
    std::array<bool, 2> falls{};  // a K.O. this frame: the fall, not a cry as well
    for (const gongfu::Event& e : match.events())
        if (e.kind == gongfu::Event::Kind::ko && (e.attacker == 0 || e.attacker == 1)) falls[static_cast<std::size_t>(1 - e.attacker)] = true;
    for (const gongfu::Event& e : match.events()) {
        using K = gongfu::Event::Kind;
        if (e.kind != K::hit && e.kind != K::counter && e.kind != K::ko) continue;
        if (e.attacker != 0 && e.attacker != 1) continue;
        const auto a = static_cast<std::size_t>(e.attacker), d = 1 - a;
        const auto by = [&](std::size_t side) {
            const auto it = voices_.find((side == 1 ? right : left).name);
            return it == voices_.end() ? nullptr : &it->second;
        };
        const float pan = static_cast<float>(std::clamp((e.at.x.to_double() - half) / half, -1.0, 1.0) * 0.7);
        const std::int64_t later = 3 * frame_samples();  // the voice just after the blow's crack
        if (e.kind == K::ko) {
            if (const Voice* v = by(d); v && v->ko >= 0 && mixer_->play(v->ko, at + later, 1, pan)) ++spoken_;
            hurt_at_[d] = st.frame;
            continue;
        }
        const bool heavy = e.damage >= 800;
        if ((heavy || e.kind == K::counter) && !falls[d] && (st.frame - hurt_at_[d] >= 36 || st.frame < hurt_at_[d])) {  // (a state loaded: earlier again)
            voice(by(d), by(d) ? std::span(by(d)->hurt) : std::span<const std::int32_t>{}, st.frame, at + later, pan);
            hurt_at_[d] = st.frame;
        }
        if (heavy && (st.frame - effort_at_[a] >= 30 || st.frame < effort_at_[a])) {
            voice(by(a), by(a) ? std::span(by(a)->effort) : std::span<const std::int32_t>{}, st.frame, at, pan);
            effort_at_[a] = st.frame;
        }
    }
    // A super's flash: its sweep, from the side that flashed.
    if (st.freeze > 0 && freeze_ == 0) play("super_flash", at, 0.8f, 0.0f);
    freeze_ = st.freeze;
}

}  // namespace omui::fighting
