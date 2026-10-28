module gongfu.training;

import std;
import gongfu;

namespace gongfu::training {

namespace {

// Appends to a fixed ring kept oldest-first: when full, the oldest goes.
template <class T, std::size_t N>
void push(std::array<T, N>& ring, std::size_t& count, const T& value) noexcept {
    if (count == N) {
        std::shift_left(ring.begin(), ring.end(), 1);
        ring[N - 1] = value;
    } else {
        ring[count++] = value;
    }
}

bool active_now(const Fighter& fighter, const FighterState& s) noexcept {
    const MoveDef& m = fighter.def.moves[static_cast<std::size_t>(s.move)];
    if (std::ranges::any_of(m.hits, [&](const HitDef& h) { return h.active.contains(s.move_frame); })) return true;
    if (m.throw_ && m.throw_->active.contains(s.move_frame)) return true;
    return m.projectile && m.projectile->spawn == s.move_frame;
}

}  // namespace

Phase phase(const Match& match, std::int32_t i) noexcept {
    const FighterState& s = match.state().f[static_cast<std::size_t>(i)];
    switch (s.mode) {
        case Mode::idle:
        case Mode::walk_forward:
        case Mode::walk_back:
        case Mode::crouch:
        case Mode::air: return Phase::free;
        case Mode::prejump:
        case Mode::landing: return Phase::recovery;
        case Mode::move: {
            const Fighter& f = match.fighter(i);
            if (s.move < 0) return Phase::free;
            if (active_now(f, s)) return Phase::active;
            return s.move_frame < f.startup(s.move) ? Phase::startup : Phase::recovery;
        }
        case Mode::hitstun: return Phase::hitstun;
        case Mode::blockstun: return Phase::blockstun;
        case Mode::thrown: return Phase::thrown;
        case Mode::clash: return Phase::recovery;
        case Mode::broken: return Phase::hitstun;
        case Mode::knockdown:
        case Mode::wakeup:
        case Mode::reset:
        case Mode::ko: return Phase::down;
    }
    return Phase::free;
}

int direction(Pad p) noexcept {
    const bool up = (p.directions & Pad::up) != 0, down = (p.directions & Pad::down) != 0;
    const bool left = (p.directions & Pad::left) != 0, right = (p.directions & Pad::right) != 0;
    const int x = left == right ? 0 : (right ? 1 : -1);
    const int y = up == down ? 0 : (up ? 1 : -1);
    return 5 + x + 3 * y;
}

void Readout::record(const Match& match, Pad left, Pad right) noexcept {
    const State& st = match.state();
    // A frame a fighter stood still (its hitstop, a super's flash): its meter
    // skips it, as the frames of its move do.
    const std::array<bool, 2> frozen = frozen_next_;
    for (std::size_t i = 0; i < 2; ++i) frozen_next_[i] = st.f[i].stop > 0 || st.freeze > 0;
    for (std::int32_t i = 0; i < 2; ++i) {
        Side& side = sides_[static_cast<std::size_t>(i)];
        const Pad p = i == 0 ? left : right;
        if (side.input_count > 0 && side.inputs[side.input_count - 1].pad == p) {
            auto& n = side.inputs[side.input_count - 1].frames;
            n = std::min(n + 1, 999);
        } else {
            push(side.inputs, side.input_count, Input{p, 1});
        }
        const Phase ph = phase(match, i);
        if (!frozen[static_cast<std::size_t>(i)]) push(side.meter, side.meter_count, ph);
        // A move begun: its startup is the first frame it is active.
        const FighterState& s = st.f[static_cast<std::size_t>(i)];
        if (s.mode == Mode::move && (s.move != side.move || s.move_frame == 1)) {
            side.move = s.move;
            side.startup.reset();
        }
        if (s.mode != Mode::move) side.move = -1;
        if (ph == Phase::active && side.move >= 0 && !side.startup) side.startup = s.move_frame;
    }
    for (const Event& e : match.events()) {
        if (e.kind == Event::Kind::clash) {
            exchange_ = {e.kind, e.attacker, st.frame};
            for (auto& t : tally_) ++t.clashes;
            if (e.attacker >= 0) ++tally_[static_cast<std::size_t>(e.attacker)].clashes_won;
            else
                for (auto& t : tally_) ++t.perfect;
        } else if (e.kind == Event::Kind::deflect) {
            const std::int32_t by = 1 - e.attacker;
            exchange_ = {e.kind, by, st.frame};
            ++tally_[static_cast<std::size_t>(by)].deflects;
        } else if (e.kind == Event::Kind::guard_break) {
            exchange_ = {e.kind, e.attacker, st.frame};
            ++tally_[static_cast<std::size_t>(e.attacker)].breaks;
        }
        const bool strike = e.kind == Event::Kind::hit || e.kind == Event::Kind::counter;
        if (strike || e.kind == Event::Kind::throw_) {
            if (!combo_.going || combo_.attacker != e.attacker) combo_ = {e.attacker, 0, 0, true};
            ++combo_.hits;
            combo_.damage += e.damage;
        }
        if (strike || e.kind == Event::Kind::block || e.kind == Event::Kind::deflect) {
            pending_attacker_ = e.attacker;
            waited_ = 0;
            free_at_ = {-1, -1};
        }
    }
    if (combo_.going && st.f[static_cast<std::size_t>(1 - combo_.attacker)].combo == 0 &&
        st.f[static_cast<std::size_t>(1 - combo_.attacker)].mode != Mode::thrown)
        combo_.going = false;
    if (pending_attacker_ >= 0) {
        ++waited_;
        for (std::int32_t i = 0; i < 2; ++i)
            if (free_at_[static_cast<std::size_t>(i)] < 0 && match.recovered(i)) free_at_[static_cast<std::size_t>(i)] = waited_;
        const auto a = static_cast<std::size_t>(pending_attacker_);
        if (free_at_[0] >= 0 && free_at_[1] >= 0) {
            advantage_ = free_at_[1 - a] - free_at_[a];
            adv_attacker_ = pending_attacker_;
            pending_attacker_ = -1;
        } else if (waited_ > 600) {
            pending_attacker_ = -1;  // a round ended, or someone never recovers: nothing to say
        }
    }
}

void Readout::clear() noexcept { *this = Readout{}; }

std::span<const Readout::Input> Readout::inputs(std::int32_t i) const noexcept {
    const Side& s = sides_[static_cast<std::size_t>(i)];
    return {s.inputs.data(), s.input_count};
}

std::span<const Phase> Readout::meter(std::int32_t i) const noexcept {
    const Side& s = sides_[static_cast<std::size_t>(i)];
    return {s.meter.data(), s.meter_count};
}

std::optional<std::int32_t> Readout::startup(std::int32_t i) const noexcept { return sides_[static_cast<std::size_t>(i)].startup; }

void Recorder::start_recording() noexcept {
    length_ = at_ = 0;
    mode_ = Mode::recording;
}

void Recorder::start_playback() noexcept {
    at_ = 0;
    mode_ = length_ > 0 ? Mode::playing : Mode::idle;
}

void Recorder::stop() noexcept { mode_ = Mode::idle; }

Pad Recorder::step(Pad given) noexcept {
    switch (mode_) {
        case Mode::recording:
            if (length_ < capacity) pads_[length_++] = given;
            else mode_ = Mode::idle;
            return given;
        case Mode::playing: {
            const Pad p = pads_[at_];
            at_ = (at_ + 1) % length_;
            return p;
        }
        case Mode::idle: break;
    }
    return given;
}

}  // namespace gongfu::training
