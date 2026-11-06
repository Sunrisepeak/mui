module gongfu.ai;

import std;
import gongfu;

namespace gongfu::ai {

namespace {

// How far in front a move's first hit reaches (its boxes' far edge).
std::int32_t reach_of(const MoveDef& m) {
    std::int32_t far = 0;
    if (!m.hits.empty())
        for (const Box& b : m.hits.front().boxes) far = std::max(far, static_cast<std::int32_t>((b.x + b.w).to_double()));
    if (m.throw_) far = std::max(far, static_cast<std::int32_t>((m.throw_->range.x + m.throw_->range.w).to_double()));
    if (m.projectile) far = 10000;
    return far;
}

bool hurt(Mode m) {
    return m == Mode::hitstun || m == Mode::blockstun || m == Mode::knockdown || m == Mode::wakeup || m == Mode::thrown || m == Mode::broken ||
           m == Mode::cinematic || m == Mode::ko || m == Mode::reset || m == Mode::clash;
}

// A relative numpad direction (6 forward) on the screen, facing `facing`.
int screen(int relative, std::int32_t facing) {
    if (facing >= 0) return relative;
    switch (relative) {
        case 1: return 3;
        case 3: return 1;
        case 4: return 6;
        case 6: return 4;
        case 7: return 9;
        case 9: return 7;
        default: return relative;
    }
}

}  // namespace

Kit read_kit(const Fighter& fighter) {
    Kit k;
    const auto& moves = fighter.def.moves;
    for (std::size_t i = 0; i < moves.size(); ++i) {
        const MoveDef& md = moves[i];
        const auto m = static_cast<std::int32_t>(i);
        if (!md.asked) continue;
        const Command& c = md.command;
        if (md.level > 0) {
            k.supers[static_cast<std::size_t>(std::min(md.level, 3))].push_back(m);
            continue;
        }
        if (c.buttons == 0) {  // a dash
            if (c.motion.length > 0 && c.motion.steps[0] == 6) k.dash = m;
            if (c.motion.length > 0 && c.motion.steps[0] == 4) k.backdash = m;
            continue;
        }
        const std::int32_t reach = reach_of(md);
        if (md.throw_) {
            if (md.throw_->air) k.air_throw = m;
            else if (c.motion.length > 0 || c.motion.rotation > 0) {
                if (k.command_throw < 0) k.command_throw = m, k.command_reach = reach;
            } else if (c.direction != 4 && k.throw_ < 0) {
                k.throw_ = m, k.throw_reach = reach;
            }
            continue;
        }
        const bool special = c.motion.length > 0 || c.charge.from != 0;
        if (md.projectile) {
            k.projectiles.push_back(m);
            continue;
        }
        if (md.hits.empty()) {
            if (!md.pieces.empty() || !md.time.empty()) k.setups.push_back(m);
            continue;
        }
        const std::int32_t startup = fighter.startup(m);
        const bool anti_air = std::ranges::any_of(md.invulnerable, [&](const Invulnerable& v) {
            return (v.to & to_air) && v.frames.from <= 2 && v.frames.to >= startup - 1;
        });
        if (anti_air && md.stance != Stance::air) {
            if (k.anti_air < 0 || startup < fighter.startup(k.anti_air)) k.anti_air = m;
            continue;
        }
        if (md.stance == Stance::air) {
            if (!special) k.jump_ins.push_back(m);
            continue;
        }
        if (special) {
            k.specials.push_back(m);
            continue;
        }
        k.pokes.push_back({m, reach, startup, md.stance == Stance::crouching});
    }
    std::ranges::sort(k.pokes, [](const Kit::Poke& a, const Kit::Poke& b) { return a.reach != b.reach ? a.reach < b.reach : a.move < b.move; });
    return k;
}

std::vector<Pad> pads_for(const Command& c, std::int32_t facing) {
    std::vector<Pad> out;
    const std::uint16_t press = c.together ? c.buttons : static_cast<std::uint16_t>(c.buttons & -c.buttons);  // one of them, or all
    auto at = [&](int relative, std::uint16_t buttons = 0) { out.push_back(pad(screen(relative, facing), buttons)); };
    if (c.charge.from != 0) {
        const int hold = c.charge.from == 4 ? 4 : 2, release = c.charge.from == 4 ? 6 : 8;
        for (int f = 0; f < c.charge.frames + 2; ++f) at(hold);
        at(release, press);
    } else if (c.motion.rotation > 0) {
        // Round from forward through down and back to up, lap after lap; pressed on the last.
        constexpr std::array<int, 8> lap{6, 3, 2, 1, 4, 7, 8, 9};
        for (int l = 0; l < c.motion.rotation; ++l)
            for (const int d : lap) at(d);
        out.back().buttons = press;
    } else if (c.motion.length > 0) {
        for (std::size_t s = 0; s < c.motion.length; ++s) at(c.motion.steps[s], s + 1 == c.motion.length ? press : std::uint16_t{0});
    } else {
        at(c.direction != 0 ? c.direction : 5, press);
    }
    return out;
}

Bot::Bot(const Fighter& self, std::int32_t side, std::uint64_t seed, Style style)
    : self_(&self), side_(side), rng_(seed * 0x9e3779b97f4a7c15ull + 0x2545f4914f6cdd1dull), style_(style), kit_(read_kit(self)) {
    plan_.reserve(32);
}

std::uint32_t Bot::roll() noexcept {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 7;
    rng_ ^= rng_ << 17;
    return static_cast<std::uint32_t>(rng_ >> 11);
}

void Bot::plan_move(std::int32_t move, std::int32_t facing) {
    loose_ = false;
    const MoveDef& md = self_->def.moves[static_cast<std::size_t>(move)];
    Command c = md.command;
    // A move for any of a kind's buttons (236P): one of them, at random.
    if (!c.together && (c.buttons == punches || c.buttons == kicks)) {
        const std::uint16_t first = c.buttons == punches ? bit(Button::lp) : bit(Button::lk);
        c.buttons = static_cast<std::uint16_t>(first << (roll() % 3));
    }
    for (const Pad p : pads_for(c, facing)) plan_.push_back(p);
}

void Bot::plan_hold(int numpad, std::int32_t frames, std::int32_t facing) {
    if (plan_.empty()) loose_ = true;
    for (std::int32_t f = 0; f < frames; ++f) plan_.push_back(pad(screen(numpad, facing)));
}

Pad Bot::guard_pad(const Match& match, const Seen& other) const {
    const FighterState& me = match.state().f[static_cast<std::size_t>(side_)];
    bool low = false;
    if (other.mode == Mode::move && other.move >= 0) {
        const MoveDef& om = match.fighter(1 - side_).def.moves[static_cast<std::size_t>(other.move)];
        for (const HitDef& h : om.hits)
            if (h.active.to >= other.move_frame) {
                low = h.level == Level::low;
                break;
            }
    }
    return pad(screen(low ? 1 : 4, me.facing));
}

Pad Bot::next(const Match& match) {
    const State& st = match.state();
    const FighterState& me = st.f[static_cast<std::size_t>(side_)];
    const FighterState& op = st.f[static_cast<std::size_t>(1 - side_)];
    seen_[static_cast<std::size_t>(st.frame & 63)] = {op.mode, op.move, op.move_frame, op.pos.x, op.pos.y, op.vel.y};
    const Seen& other = seen_[static_cast<std::size_t>(std::max(0, st.frame - style_.reaction) & 63)];
    const std::int32_t facing = me.facing;
    const double dist = std::abs((other.x - me.pos.x).to_double());

    // Struck, thrown, down: what was planned is gone; it holds its guard.
    if (hurt(me.mode)) {
        plan_.clear();
        at_ = 0;
        return guard_pad(match, other);
    }
    // A move being asked for goes on; a hold or a walk goes on unless something is seen below.
    if (at_ < plan_.size() && !loose_) return plan_[at_++];
    if (!match.actionable(side_)) return at_ < plan_.size() ? plan_[at_++] : pad(5);
    const bool holding = at_ < plan_.size();
    auto replan = [&] {
        plan_.clear();
        at_ = 0;
    };

    const Fighter& them = match.fighter(1 - side_);
    auto meter_for = [&](std::int32_t m) { return self_->def.moves[static_cast<std::size_t>(m)].meter_cost <= me.meter; };
    auto in_reach = [&](std::int32_t m, double slack = 0) { return reach_of(self_->def.moves[static_cast<std::size_t>(m)]) + slack >= dist; };
    auto pick = [&](const std::vector<std::int32_t>& from) { return from.empty() ? -1 : from[roll() % from.size()]; };

    // A hit landed and the move cancels: confirmed into a special (or a super), if one is open.
    if (me.mode == Mode::move && me.contact) {
        const auto& targets = self_->cancel_targets[static_cast<std::size_t>(me.move)];
        std::vector<std::int32_t> open;
        for (const auto& t : targets)
            for (const std::int32_t m : t)
                if (meter_for(m) && self_->def.moves[static_cast<std::size_t>(m)].asked && !self_->def.moves[static_cast<std::size_t>(m)].hits.empty())
                    open.push_back(m);
        if (!open.empty() && chance(80)) {
            replan();
            plan_move(pick(open), facing);
            return plan_[at_++];
        }
    }
    if (me.mode == Mode::move) return holding ? plan_[at_++] : pad(5);  // a move under way

    // What it saw coming: a strike in reach, or a projectile on its way — guarded (decided once a move).
    bool threat = false;
    if (other.mode == Mode::move && other.move >= 0) {
        const MoveDef& om = them.def.moves[static_cast<std::size_t>(other.move)];
        for (const HitDef& h : om.hits)
            if (h.active.to >= other.move_frame && reach_of(om) + 40 >= dist) threat = true;
        const std::int32_t instance = other.move * 4096 + ((st.frame - other.move_frame) & 4095);
        if (threat && guarded_move_ != instance) {
            guarded_move_ = instance;
            guarding_ = chance(style_.guard);
        }
    }
    // A projectile on its way: from far enough, jumped over (and come down on with an attack); else guarded.
    for (const ProjectileState& p : st.p) {
        if (!p.alive || p.owner == side_ || (p.pos.x < me.pos.x) != (p.facing > 0)) continue;
        const double gap = std::abs((p.pos.x - me.pos.x).to_double());
        if (gap < 260) threat = true, guarding_ = true;
        if (gap > 90 && gap < 170 && dist > 200 && me.pos.y <= Fixed{} && chance(style_.jump_over)) {
            replan();
            plan_.push_back(pad(screen(9, facing)));
            for (int f = 0; f < 18; ++f) plan_.push_back(pad(5));
            if (!kit_.jump_ins.empty()) plan_move(pick(kit_.jump_ins), facing);
            loose_ = false;
            return plan_[at_++];
        }
    }
    if (threat && guarding_) {
        replan();
        plan_hold(guard_pad(match, other) == pad(screen(1, facing)) ? 1 : 4, 6, facing);
        return plan_[at_++];
    }

    // A jump coming down at it: an anti-air (or a guard).
    if (other.y > Fixed{} && other.vy < Fixed{} && dist < 170 && other.y.to_double() < 170) {
        replan();
        if (kit_.anti_air >= 0 && chance(style_.anti_air)) {
            plan_move(kit_.anti_air, facing);
            return plan_[at_++];
        }
        if (kit_.air_throw >= 0 && chance(style_.anti_air)) {
            plan_move(kit_.air_throw, facing);
            return plan_[at_++];
        }
        plan_hold(4, 8, facing);
        return plan_[at_++];
    }

    // A whiff in reach, recovering longer than its best answer takes: punished.
    if (other.mode == Mode::move && other.move >= 0) {
        const MoveDef& om = them.def.moves[static_cast<std::size_t>(other.move)];
        std::int32_t last = 0;
        for (const HitDef& h : om.hits) last = std::max(last, h.active.to);
        const std::int32_t left = om.total - other.move_frame;
        if (other.move_frame > last && left > 8 && op.contact == 0 && chance(style_.punish)) {
            std::int32_t best = -1;
            for (std::int32_t level = 3; level >= 1 && best < 0; --level)
                for (const std::int32_t m : kit_.supers[static_cast<std::size_t>(level)])
                    if (meter_for(m) && in_reach(m) && self_->startup(m) < left && !self_->def.moves[static_cast<std::size_t>(m)].hits.empty()) best = m;
            for (const std::int32_t m : kit_.specials)
                if (best < 0 && in_reach(m) && self_->startup(m) < left) best = m;
            for (auto it = kit_.pokes.rbegin(); it != kit_.pokes.rend() && best < 0; ++it)
                if (it->reach >= dist && it->startup < left) best = it->move;
            if (best >= 0) {
                replan();
                plan_move(best, facing);
                return plan_[at_++];
            }
            // Out of reach of a long whiff: stepped in, then struck, if its recovery leaves the time.
            const double walk = std::max(0.5, self_->def.walk_forward.to_double());
            for (auto it = kit_.pokes.rbegin(); it != kit_.pokes.rend(); ++it) {
                const auto step = static_cast<std::int32_t>(std::ceil((dist - it->reach + 5) / walk));
                if (step > 0 && step + it->startup + 2 < left) {
                    replan();
                    plan_hold(6, step, facing);
                    plan_move(it->move, facing);
                    return plan_[at_++];
                }
            }
        }
    }

    if (holding) return plan_[at_++];  // nothing seen: the hold or the walk goes on
    replan();
    // Its own game, by distance (or, not attacking this turn, standing: it guards only what it sees).
    if (!chance(style_.offense)) {
        plan_hold(5, 6, facing);
        return plan_[at_++];
    }
    std::vector<std::int32_t> poke_in_reach;
    for (const Kit::Poke& p : kit_.pokes)
        if (p.reach + 5 >= dist) poke_in_reach.push_back(p.move);
    auto super_in_reach = [&] {
        for (std::int32_t level = 3; level >= 1; --level)
            for (const std::int32_t m : kit_.supers[static_cast<std::size_t>(level)]) {
                const MoveDef& md = self_->def.moves[static_cast<std::size_t>(m)];
                const bool strikes = !md.hits.empty() || md.throw_ || md.projectile;
                if (meter_for(m) && (strikes ? in_reach(m) : dist > 200)) return m;
            }
        return -1;
    };
    if (dist > 330) {
        if (!kit_.projectiles.empty() && chance(40)) plan_move(pick(kit_.projectiles), facing);
        else if (!kit_.setups.empty() && chance(15)) plan_move(pick(kit_.setups), facing);
        else if (const std::int32_t s = super_in_reach(); s >= 0 && chance(10)) plan_move(s, facing);
        else if (chance(style_.aggression)) {
            if (kit_.dash >= 0 && chance(30)) plan_move(kit_.dash, facing);
            else plan_hold(6, 12 + static_cast<std::int32_t>(roll() % 16), facing);
        } else {
            plan_hold(roll() % 2 ? 4 : 5, 10, facing);
        }
    } else if (dist > 120) {
        if (!poke_in_reach.empty() && chance(40)) plan_move(pick(poke_in_reach), facing);
        else if (!kit_.projectiles.empty() && chance(20)) plan_move(pick(kit_.projectiles), facing);
        else if (const std::int32_t s = super_in_reach(); s >= 0 && chance(15)) plan_move(s, facing);
        else if (!kit_.jump_ins.empty() && chance(10)) {
            plan_.push_back(pad(screen(9, facing)));
            for (int f = 0; f < 18; ++f) plan_.push_back(pad(5));
            plan_move(pick(kit_.jump_ins), facing);
        } else if (chance(style_.aggression)) {
            plan_hold(6, 8 + static_cast<std::int32_t>(roll() % 10), facing);
        } else {
            plan_hold(roll() % 2 ? 4 : 1, 8, facing);
        }
    } else {
        if (const std::int32_t s = super_in_reach(); s >= 0 && chance(25)) plan_move(s, facing);
        else if (kit_.command_throw >= 0 && dist <= kit_.command_reach && chance(20)) plan_move(kit_.command_throw, facing);
        else if (kit_.throw_ >= 0 && dist <= kit_.throw_reach && chance(15)) plan_move(kit_.throw_, facing);
        else if (!poke_in_reach.empty() && chance(55)) plan_move(pick(poke_in_reach), facing);
        else if (!kit_.specials.empty() && chance(15)) plan_move(pick(kit_.specials), facing);
        else if (kit_.backdash >= 0 && chance(10)) plan_move(kit_.backdash, facing);
        else plan_hold(roll() % 3 == 0 ? 1 : 4, 6, facing);
    }
    return plan_.empty() ? pad(5) : plan_[at_++];
}

Round play(const SystemDef& system, const Fighter& left, const Fighter& right, std::uint64_t seed, Style style) {
    Match m(system, left, right);
    Bot a(left, 0, seed * 2 + 1, style), b(right, 1, seed * 2 + 2, style);
    Round r;
    r.by_move[0].assign(left.def.moves.size(), 0);
    r.by_move[1].assign(right.def.moves.size(), 0);
    std::array<std::int32_t, 2> was{-1, -1};
    while (!m.state().over && m.state().frame < system.round_frames + 600) {
        const Pad pa = a.next(m), pb = b.next(m);
        m.tick(pa, pb);
        for (const Event& e : m.events())
            if ((e.kind == Event::Kind::hit || e.kind == Event::Kind::counter || e.kind == Event::Kind::throw_ || e.kind == Event::Kind::block ||
                 e.kind == Event::Kind::armor) &&
                e.attacker >= 0 && e.attacker < 2) {
                r.damage[static_cast<std::size_t>(e.attacker)] += e.damage;
                auto& moves = r.by_move[static_cast<std::size_t>(e.attacker)];
                if (e.move >= 0 && static_cast<std::size_t>(e.move) < moves.size()) moves[static_cast<std::size_t>(e.move)] += e.damage;
            }
        for (std::size_t i = 0; i < 2; ++i) {
            const FighterState& f = m.state().f[i];
            const std::int32_t now = f.mode == Mode::move ? f.move : -1;
            if (now >= 0 && now != was[i] && m.fighter(static_cast<std::int32_t>(i)).def.moves[static_cast<std::size_t>(now)].level > 0) ++r.supers[i];
            was[i] = now;
        }
    }
    r.winner = m.state().over ? m.state().winner : 2;
    r.frames = m.state().frame;
    return r;
}

Record versus(const SystemDef& system, const Fighter& a, const Fighter& b, std::int32_t rounds, std::uint64_t seed, Style style) {
    Record rec;
    rec.by_move[0].assign(a.def.moves.size(), 0);
    rec.by_move[1].assign(b.def.moves.size(), 0);
    for (std::int32_t k = 0; k < rounds; ++k) {
        const bool swapped = k % 2 == 1;
        const Round r = swapped ? play(system, b, a, seed + static_cast<std::uint64_t>(k), style) : play(system, a, b, seed + static_cast<std::uint64_t>(k), style);
        ++rec.rounds;
        rec.frames += r.frames;
        const std::size_t ai = swapped ? 1 : 0, bi = swapped ? 0 : 1;  // where a and b stood
        rec.damage[0] += r.damage[ai];
        rec.damage[1] += r.damage[bi];
        rec.supers[0] += r.supers[ai];
        rec.supers[1] += r.supers[bi];
        for (std::size_t m = 0; m < rec.by_move[0].size(); ++m) rec.by_move[0][m] += r.by_move[ai][m];
        for (std::size_t m = 0; m < rec.by_move[1].size(); ++m) rec.by_move[1][m] += r.by_move[bi][m];
        if (r.winner == 2) ++rec.draws;
        else ++rec.wins[static_cast<std::size_t>(r.winner) == ai ? 0 : 1];
    }
    return rec;
}

}  // namespace gongfu::ai
