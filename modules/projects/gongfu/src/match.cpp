module gongfu;

import std;

namespace gongfu {

namespace {

constexpr std::int32_t throw_bit = 1 << 30;

// The command every fighter breaks a throw with: light punch and light kick together.
constexpr Command tech_command{.buttons = static_cast<std::uint16_t>(bit(Button::lp) | bit(Button::lk)), .together = true, .buffer = 2};

bool neutral(Mode m) { return m == Mode::idle || m == Mode::walk_forward || m == Mode::walk_back || m == Mode::crouch; }

// The middle of where two boxes meet: where a spark goes.
Vec meeting(const Rect& a, const Rect& b) {
    const Fixed left = max(a.left, b.left), right = min(a.right, b.right), bottom = max(a.bottom, b.bottom), top = min(a.top, b.top);
    return {(left + right) / 2, (bottom + top) / 2};
}

// Away from `from` along the stage: +1 to the right of it, −1 to the left.
std::int32_t away(const FighterState& from, const FighterState& of) { return of.pos.x >= from.pos.x ? 1 : -1; }

}  // namespace

std::uint64_t checksum(const State& s) noexcept {
    std::uint64_t h = 1469598103934665603ULL;
    for (std::byte b : std::as_bytes(std::span(&s, 1))) {
        h ^= std::to_integer<std::uint64_t>(b);
        h *= 1099511628211ULL;
    }
    return h;
}

Match::Match(SystemDef system, const Fighter& left, const Fighter& right) : system_(std::move(system)), fighters_{&left, &right} {
    state_.timer = system_.round_frames;
    const Fixed middle = system_.stage_width / 2, half = system_.start_distance / 2;
    for (std::int32_t i = 0; i < 2; ++i) {
        FighterState& f = state_.f[static_cast<std::size_t>(i)];
        f.health = fighters_[static_cast<std::size_t>(i)]->def.health;
        f.facing = i == 0 ? 1 : -1;
        f.pos = {i == 0 ? middle - half : middle + half, Fixed{}};
        auto& order = order_[static_cast<std::size_t>(i)];
        const auto& moves = fighters_[static_cast<std::size_t>(i)]->def.moves;
        for (std::size_t m = 0; m < moves.size(); ++m) order.push_back(static_cast<std::int32_t>(m));
        // By priority; then the more particular command first (a motion, a
        // charge, a direction held, then a button alone), so 2HP is not read
        // as 5HP; then in the order defined.
        auto particular = [&](std::int32_t m) {
            const Command& c = moves[static_cast<std::size_t>(m)].command;
            return c.motion.rotation > 0 ? 4 : c.motion.length > 0 ? 3 : c.charge.from != 0 ? 2 : c.direction != 0 ? 1 : 0;
        };
        std::ranges::stable_sort(order, [&](std::int32_t a, std::int32_t b) {
            const auto& ma = moves[static_cast<std::size_t>(a)];
            const auto& mb = moves[static_cast<std::size_t>(b)];
            if (ma.priority != mb.priority) return ma.priority > mb.priority;
            return particular(a) > particular(b);
        });
    }
    events_.reserve(16);
}

bool Match::recovered(std::int32_t i) const noexcept {
    const FighterState& s = state_.f[static_cast<std::size_t>(i)];
    if (neutral(s.mode) || s.mode == Mode::air) return true;
    return s.mode == Mode::move && s.move_frame >= fighter(i).def.moves[static_cast<std::size_t>(s.move)].total;
}

bool Match::actionable(std::int32_t i) const noexcept {
    if (recovered(i)) return true;
    const FighterState& s = state_.f[static_cast<std::size_t>(i)];
    if (s.mode != Mode::move) return false;
    const MoveDef& m = fighter(i).def.moves[static_cast<std::size_t>(s.move)];
    for (const CancelDef& c : m.cancels)
        if (c.frames.contains(s.move_frame) && (!c.on_contact || s.contact)) return true;
    return false;
}

// The last frame of fighter i's current move on which anything of it is
// active (its hits, its throw, the spawn of its projectile).
std::int32_t Match::last_active(std::int32_t i) const noexcept {
    const FighterState& s = state_.f[static_cast<std::size_t>(i)];
    if (s.move < 0) return 0;
    const MoveDef& m = fighter(i).def.moves[static_cast<std::size_t>(s.move)];
    std::int32_t last = 0;
    for (const HitDef& h : m.hits) last = std::max(last, h.active.to);
    if (m.throw_) last = std::max(last, m.throw_->active.to);
    if (m.projectile) last = std::max(last, m.projectile->spawn);
    return last;
}

void Match::start(std::int32_t i, std::int32_t move) {
    FighterState& s = body(i);
    const MoveDef& m = fighter(side(i)).def.moves[static_cast<std::size_t>(move)];
    s.mode = Mode::move;
    s.move = move;
    s.move_frame = 0;
    s.landed = 0;
    s.contact = 0;
    s.thrower = 0;
    s.super_scale = 0;
    s.clash_cancel = 0;
    s.armored = 0;
    s.meter -= m.meter_cost;
    if (s.pos.y <= Fixed{}) s.vel = {};
    if (m.freeze > 0) state_.freeze = std::max(state_.freeze, m.freeze);
}

void Match::decide(std::int32_t i) {
    FighterState& s = body(i);
    FighterState& o = state_.f[static_cast<std::size_t>(1 - side(i))];
    const FighterDef& def = fighter(side(i)).def;

    // A throw broken: both let go, pushed apart.
    if (s.mode == Mode::thrown) {
        if (s.tech > 0 && recognise(s.log, tech_command)) {
            for (FighterState* f : {&s, &o}) {
                f->mode = Mode::blockstun;
                f->stun = 12;
                f->move = -1;
                f->push = Fixed::of(4) * away(f == &s ? o : s, *f);
            }
            events_.push_back({Event::Kind::tech, 1 - side(i), o.move, 0, 0, 0, s.pos});
        }
        return;
    }
    // A move whose last frame has passed is over: the fighter acts on this one.
    if (s.mode == Mode::move && s.move_frame >= def.moves[static_cast<std::size_t>(s.move)].total) {
        s.move = -1;
        s.thrower = 0;
        if (s.pos.y > Fixed{}) s.mode = Mode::air, s.vel.y = std::min(s.vel.y, Fixed{});
        else s.mode = Mode::idle, s.vel = {}, s.combo = 0, s.stalled = 0;
    }
    const bool ground = s.pos.y <= Fixed{};
    const bool free = neutral(s.mode) || s.mode == Mode::air;
    std::array<std::int32_t, 32> open{};
    const std::size_t open_count = open_cancels(i, open);
    // A perfect clash's recovery, for a move that allows it: a special (not a super) may start.
    const bool clash_open = s.mode == Mode::clash && s.clash_cancel;
    // A rotation passes up: its move may start from the frames before a jump leaves the ground (G-17).
    const bool rotating = s.mode == Mode::prejump;
    auto is_open = [&](std::int32_t m) {
        if (rotating && def.moves[static_cast<std::size_t>(m)].command.motion.rotation > 0) return true;
        if (clash_open) {
            const MoveDef& md = def.moves[static_cast<std::size_t>(m)];
            if (md.level == 0 && (md.command.motion.length > 0 || md.command.charge.from != 0)) return true;
        }
        return std::find(open.begin(), open.begin() + static_cast<std::ptrdiff_t>(open_count), m) != open.begin() + static_cast<std::ptrdiff_t>(open_count);
    };
    // A move asked for while standing still starts now, if it still may.
    if (s.queued >= 0) {
        const std::int32_t m = s.queued;
        s.queued = -1;
        if ((free || ((s.mode == Mode::move || clash_open || rotating) && is_open(m))) && may_start(i, m)) {
            start(i, m);
            return;
        }
    }
    if (free || open_count > 0 || clash_open || rotating) {
        for (std::int32_t m : order_[static_cast<std::size_t>(side(i))]) {
            if (!free && !is_open(m)) continue;
            if (!may_start(i, m)) continue;
            if (recognise(s.log, def.moves[static_cast<std::size_t>(m)].command)) {
                start(i, m);
                return;
            }
        }
    }
    if (!neutral(s.mode) || !ground) return;
    const int d = s.log.direction(0);
    if (d >= 7) {
        s.mode = Mode::prejump;
        s.stun = def.prejump;
        s.jump = d == 9 ? 1 : d == 7 ? -1 : 0;
    } else if (d <= 3) {
        s.mode = Mode::crouch;
    } else if (d == 6) {
        s.mode = Mode::walk_forward;
    } else if (d == 4) {
        const bool threatened = o.mode == Mode::move && o.move_frame <= last_active(1 - side(i)) &&
                                abs(o.pos.x - s.pos.x) <= system_.proximity_guard;
        s.mode = threatened ? Mode::idle : Mode::walk_back;
    } else {
        s.mode = Mode::idle;
    }
}

std::size_t Match::open_cancels(std::int32_t i, std::array<std::int32_t, 32>& out) const noexcept {
    const FighterState& s = body(i);
    if (s.mode != Mode::move) return 0;
    std::size_t n = 0;
    const MoveDef& m = fighter(side(i)).def.moves[static_cast<std::size_t>(s.move)];
    const auto& targets = fighter(side(i)).cancel_targets[static_cast<std::size_t>(s.move)];
    for (std::size_t c = 0; c < m.cancels.size(); ++c)
        if (m.cancels[c].frames.contains(s.move_frame) && (!m.cancels[c].on_contact || s.contact))
            for (std::int32_t t : targets[c])
                if (n < out.size()) out[n++] = t;
    return n;
}

// During a hitstop or a super's flash a fighter stands still, but a move is
// read as it is asked for and starts on the first frame after: standing
// still does not eat the input (how fighting games let a combo be timed by
// its hits, and a reversal be ready when a flash ends).
void Match::queue(std::int32_t i) {
    FighterState& s = body(i);
    if (s.queued >= 0) return;
    const bool free = neutral(s.mode) || s.mode == Mode::air;
    if (!free && s.mode != Mode::move) return;
    std::array<std::int32_t, 32> open{};
    const std::size_t n = open_cancels(i, open);
    const FighterDef& def = fighter(side(i)).def;
    for (std::int32_t m : order_[static_cast<std::size_t>(side(i))]) {
        if (!free && std::find(open.begin(), open.begin() + static_cast<std::ptrdiff_t>(n), m) == open.begin() + static_cast<std::ptrdiff_t>(n)) continue;
        if (!may_start(i, m)) continue;
        if (recognise(s.log, def.moves[static_cast<std::size_t>(m)].command)) {
            s.queued = m;
            return;
        }
    }
}

bool Match::may_start(std::int32_t i, std::int32_t m) const noexcept {
    const FighterState& s = body(i);
    const MoveDef& md = fighter(side(i)).def.moves[static_cast<std::size_t>(m)];
    // A clone (G-21) throws nothing, uses no meter, does no super and nothing with time or pieces.
    if (i >= 2 && (md.throw_ || md.meter_cost > 0 || md.level > 0 || !md.time.empty() || !md.pieces.empty())) return false;
    if ((md.stance == Stance::air) != (s.pos.y > Fixed{})) return false;
    if (md.meter_cost > s.meter) return false;
    if (md.needs_piece && !has_piece(side(i))) return false;
    if (md.projectile && md.projectile->limit > 0) {
        std::int32_t alive = 0;
        for (const ProjectileState& p : state_.p)
            if (p.alive && p.from == side(i) && !p.reflected && fighter(side(i)).def.moves[static_cast<std::size_t>(p.move)].projectile->limit > 0) ++alive;
        if (alive >= md.projectile->limit) return false;
    }
    return true;
}

void Match::advance(std::int32_t i) {
    FighterState& s = body(i);
    const FighterDef& def = fighter(side(i)).def;
    auto fall = [&] {
        s.vel.y -= def.gravity;
        s.pos = s.pos + s.vel;
    };
    auto to_neutral = [&] {
        s.mode = Mode::idle;
        s.move = -1;
        s.vel = {};
        s.combo = 0;
        s.stalled = 0;
        s.juggle = 0;
        s.relaunched = 0;
        s.scale_cap = 100;
        s.clash_cancel = 0;
    };
    switch (s.mode) {
        case Mode::idle:
        case Mode::crouch: s.vel = {}; break;
        case Mode::walk_forward:
            s.vel = {def.walk_forward * s.facing, Fixed{}};
            s.pos.x += s.vel.x;
            break;
        case Mode::walk_back:
            s.vel = {-(def.walk_back * s.facing), Fixed{}};
            s.pos.x += s.vel.x;
            break;
        case Mode::prejump:
            if (--s.stun <= 0) {
                s.mode = Mode::air;
                s.vel = {def.jump_forward * (s.jump * s.facing), def.jump_up};
            }
            break;
        case Mode::air:
            fall();
            if (s.pos.y <= Fixed{}) {
                s.pos.y = Fixed{};
                s.vel = {};
                s.mode = def.landing > 0 ? Mode::landing : Mode::idle;
                s.stun = def.landing;
            }
            break;
        case Mode::landing:
            if (--s.stun <= 0) to_neutral();
            break;
        case Mode::move: {
            const MoveDef& m = def.moves[static_cast<std::size_t>(s.move)];
            ++s.move_frame;
            const bool airborne_start = s.pos.y > Fixed{};
            const bool air_move = m.stance == Stance::air;  // started in the air (a ground move may rise and come down: it goes on)
            // The move's own movement, or, started in the air, the jump's arc.
            const StepDef* step = nullptr;
            for (const StepDef& st : m.steps)
                if (st.frame <= s.move_frame) step = &st;
            if (step) s.vel = {step->velocity.x * s.facing, step->velocity.y};
            if (airborne_start || (step && step->velocity.y != Fixed{})) {
                if (!step) s.vel.y -= def.gravity;
                s.pos = s.pos + s.vel;
            } else {
                s.pos.x += s.vel.x;
            }
            if (m.projectile && s.move_frame == m.projectile->spawn) {
                for (ProjectileState& p : state_.p)
                    if (!p.alive) {
                        p = {1, side(i), side(i), s.move, 0, s.facing, {s.pos.x + m.projectile->offset.x * s.facing, s.pos.y + m.projectile->offset.y},
                             m.projectile->hits, m.projectile->strength, 0, 0, 0, 0, 0};
                        break;
                    }
            }
            for (const PieceDef& pd : m.pieces)
                if (s.move_frame == pd.frame && i < 2) act(i, pd);
            for (const TimeDef& td : m.time)
                if (s.move_frame == td.frame && i < 2) act(i, td);
            if (s.mode != Mode::move) break;  // a rewind ended it
            if (s.pos.y < Fixed{}) s.pos.y = Fixed{};
            if (air_move && airborne_start && s.pos.y <= Fixed{}) {  // an air move ends where it lands (at the ground or past it)
                s.move = -1;
                s.vel = {};
                s.mode = def.landing > 0 ? Mode::landing : Mode::idle;
                s.stun = def.landing;
            }
            break;
        }
        case Mode::hitstun:
            if (s.pos.y > Fixed{} || s.vel.y > Fixed{}) {
                fall();
                if (s.pos.y <= Fixed{}) {
                    s.pos.y = Fixed{};
                    s.vel = {};
                    s.mode = Mode::knockdown;
                    s.stun = system_.knockdown;
                    s.juggle = 0;
                    s.relaunched = 0;
                }
                break;
            }
            s.pos.x += s.push;
            if (--s.stun <= 0) to_neutral(), s.protect = 1 + 2;  // the 2 frames after
            break;
        case Mode::blockstun:
            s.pos.x += s.push;
            if (--s.stun <= 0) to_neutral(), s.protect = 1 + 2;  // the 2 frames after
            break;
        case Mode::clash:
            s.pos.x += s.push;
            if (--s.stun <= 0) to_neutral();
            break;
        case Mode::broken:
            if (--s.stun <= 0) to_neutral(), s.protect = 1 + 2;
            break;
        case Mode::reset:
            fall();
            if (s.pos.y <= Fixed{}) {
                s.pos.y = Fixed{};
                to_neutral();
            }
            break;
        case Mode::knockdown:
            if (--s.stun <= 0) {
                s.mode = system_.wakeup > 0 ? Mode::wakeup : Mode::idle;
                s.stun = system_.wakeup;
                if (s.mode == Mode::idle) to_neutral();
            }
            break;
        case Mode::wakeup:
            if (--s.stun <= 0) to_neutral(), s.protect = 1 + 3;  // the 3 frames after
            break;
        case Mode::thrown: {
            --s.tech;
            if (--s.stun <= 0) {
                const FighterState& a = state_.f[static_cast<std::size_t>(1 - side(i))];
                const std::int32_t damage = s.throw_damage;
                s.throw_damage = 0;
                s.health -= damage;
                s.mode = Mode::knockdown;
                s.stun = system_.knockdown;
                events_.push_back({Event::Kind::throw_, 1 - side(i), a.move, 0, damage, 0, s.pos});
                if (s.health <= 0) {
                    s.health = 0;
                    s.mode = Mode::ko;
                    state_.over = 1;
                    state_.winner = 1 - side(i);
                    events_.push_back({Event::Kind::ko, 1 - side(i), a.move, 0, damage, 0, s.pos});
                }
            }
            break;
        }
        case Mode::ko:
            if (s.pos.y > Fixed{}) {
                fall();
                if (s.pos.y < Fixed{}) s.pos.y = Fixed{}, s.vel = {};
            }
            break;
    }
}

void Match::separate() {
    FighterState& a = state_.f[0];
    FighterState& b = state_.f[1];
    // A move passing through the other (a cross-under, a teleport) is not pushed apart on its frames.
    auto passing = [&](std::int32_t i) {
        const FighterState& f = state_.f[static_cast<std::size_t>(i)];
        if (f.mode != Mode::move) return false;
        const auto& p = fighter(i).def.moves[static_cast<std::size_t>(f.move)].pass;
        return p && p->contains(f.move_frame);
    };
    const bool through = passing(0) || passing(1);
    // Left and right: who stands where (by position; the first on a tie).
    FighterState& l = a.pos.x <= b.pos.x ? a : b;
    FighterState& r = &l == &a ? b : a;
    const std::int32_t li = &l == &a ? 0 : 1;
    const Box& lp = fighter(li).def.push;
    const Box& rp = fighter(1 - li).def.push;
    auto overlap = [&] {
        const Rect rl = place(lp, l.pos, l.facing), rr = place(rp, r.pos, r.facing);
        return rl.overlaps(rr) ? rl.right - rr.left : Fixed{};
    };
    if (const Fixed o = overlap(); o > Fixed{} && !through) {
        const Fixed half = o / 2;
        l.pos.x -= half;
        r.pos.x += o - half;
    }
    // On the stage; one pushed into a wall pushes the other out instead.
    auto keep = [&](FighterState& f, const Box& push) {
        const Rect at = place(push, f.pos, f.facing);
        if (at.left < Fixed{}) f.pos.x -= at.left;
        if (at.right > system_.stage_width) f.pos.x -= at.right - system_.stage_width;
    };
    keep(l, lp);
    keep(r, rp);
    if (const Fixed o = overlap(); o > Fixed{} && !through) {
        if (place(lp, l.pos, l.facing).left <= Fixed{}) r.pos.x += o;
        else l.pos.x -= o;
        keep(l, lp);
        keep(r, rp);
    }
}

void Match::face() {
    for (std::int32_t i = 0; i < 4; ++i) {
        if (i >= 2 && !state_.clones[static_cast<std::size_t>(i - 2)].alive) continue;
        FighterState& s = body(i);
        const FighterState& o = state_.f[static_cast<std::size_t>(1 - side(i))];
        if (neutral(s.mode) && s.pos.y <= Fixed{} && o.pos.x != s.pos.x) s.facing = o.pos.x > s.pos.x ? 1 : -1;
    }
}

std::size_t Match::hurtboxes(std::int32_t i, std::array<Rect, 4>& out) const noexcept {
    const FighterState& s = body(i);
    const FighterDef& def = fighter(side(i)).def;
    if (s.mode == Mode::move)
        for (const HurtDef& h : def.moves[static_cast<std::size_t>(s.move)].hurt)
            if (h.frames.contains(s.move_frame)) {
                std::size_t n = 0;
                for (const Box& b : h.boxes)
                    if (n < out.size()) out[n++] = place(b, s.pos, s.facing);
                return n;
            }
    const int d = s.log.direction(0);
    const bool low = s.mode == Mode::crouch ||
                     ((s.mode == Mode::blockstun || s.mode == Mode::hitstun) && d <= 3) ||
                     (s.mode == Mode::move && def.moves[static_cast<std::size_t>(s.move)].stance == Stance::crouching);
    const Box& b = s.pos.y > Fixed{} ? def.hurt_air : low ? def.hurt_crouching : def.hurt_standing;
    out[0] = place(b, s.pos, s.facing);
    return 1;
}

bool Match::invulnerable(std::int32_t i, std::uint8_t to) const noexcept {
    const FighterState& s = state_.f[static_cast<std::size_t>(i)];
    if (s.mode == Mode::knockdown || s.mode == Mode::wakeup || s.mode == Mode::ko || s.mode == Mode::thrown || s.mode == Mode::reset ||
        s.mode == Mode::cinematic)
        return true;
    if (s.mode != Mode::move) return false;
    for (const Invulnerable& v : fighter(i).def.moves[static_cast<std::size_t>(s.move)].invulnerable)
        if ((v.to & to) && v.frames.contains(s.move_frame)) return true;
    return false;
}

bool Match::blocking(std::int32_t i, Level level) const noexcept {
    const FighterState& s = state_.f[static_cast<std::size_t>(i)];
    if (s.pos.y > Fixed{}) return false;
    if (!(s.mode == Mode::idle || s.mode == Mode::walk_back || s.mode == Mode::crouch || s.mode == Mode::blockstun)) return false;
    const int d = s.log.direction(0);
    if (d == 1) return level != Level::overhead;  // crouching, back
    if (d == 4 || d == 7) return level != Level::low;  // standing, back
    return false;
}

void Match::resolve() {
    std::array<Strike, 16> strikes{};
    std::size_t n = 0;
    std::array<bool, 2> throwing{false, false};
    std::array<std::array<Rect, 4>, 2> hurt{};
    std::array<std::size_t, 2> parts{hurtboxes(0, hurt[0]), hurtboxes(1, hurt[1])};
    // Where a box meets fighter d's body: the middle of the first overlap.
    auto meets = [&](const Rect& r, std::int32_t d) -> std::optional<Vec> {
        for (std::size_t k = 0; k < parts[static_cast<std::size_t>(d)]; ++k)
            if (r.overlaps(hurt[static_cast<std::size_t>(d)][k])) return meeting(r, hurt[static_cast<std::size_t>(d)][k]);
        return std::nullopt;
    };
    clash();
    pieces();
    for (std::int32_t a = 0; a < 2; ++a) {
        const std::int32_t d = 1 - a;
        const FighterState& sa = state_.f[static_cast<std::size_t>(a)];
        const FighterState& sd = state_.f[static_cast<std::size_t>(d)];
        if (sa.mode != Mode::move || state_.over) continue;
        const MoveDef& m = fighter(a).def.moves[static_cast<std::size_t>(sa.move)];
        const std::uint8_t kind = sa.pos.y > Fixed{} ? to_air : to_strike;
        for (std::size_t h = 0; h < m.hits.size() && h < 30; ++h) {
            const HitDef& hd = m.hits[h];
            if (!hd.active.contains(sa.move_frame) || (sa.landed & (1 << h)) || invulnerable(d, kind)) continue;
            for (const Box& b : hd.boxes)
                if (auto at = meets(place(b, sa.pos, sa.facing), d)) {
                    if (n < strikes.size()) strikes[n++] = {a, sa.move, static_cast<std::int32_t>(h), false, -1, *at, a, a};
                    break;
                }
        }
        if (m.throw_ && m.throw_->active.contains(sa.move_frame) && !(sa.landed & throw_bit)) {
            const bool grounded = sd.pos.y <= Fixed{} && (neutral(sd.mode) || sd.mode == Mode::landing || sd.mode == Mode::prejump ||
                                                         sd.mode == Mode::move || sd.mode == Mode::broken);
            // An anti-air throw takes only a fighter in the air: jumping, or in an air move (G-18).
            const bool airborne = sd.pos.y > Fixed{} && (sd.mode == Mode::air || sd.mode == Mode::move);
            const bool takes = m.throw_->air ? airborne : grounded;
            if (takes && sd.protect == 0 && !invulnerable(d, to_throw) && meets(place(m.throw_->range, sa.pos, sa.facing), d))
                throwing[static_cast<std::size_t>(a)] = true;
        }
    }
    // Clones (G-21): their strikes land as their side's; any of the other side's strikes touching one, it is gone.
    for (std::int32_t b = 2; b < 4; ++b) {
        if (!state_.clones[static_cast<std::size_t>(b - 2)].alive) continue;
        const std::int32_t a = side(b), d = 1 - a;
        const FighterState& sa = body(b);
        if (sa.mode != Mode::move || state_.over) continue;
        const MoveDef& m = fighter(a).def.moves[static_cast<std::size_t>(sa.move)];
        const std::uint8_t kind = sa.pos.y > Fixed{} ? to_air : to_strike;
        for (std::size_t h = 0; h < m.hits.size() && h < 30; ++h) {
            const HitDef& hd = m.hits[h];
            if (!hd.active.contains(sa.move_frame) || (sa.landed & (1 << h)) || invulnerable(d, kind)) continue;
            for (const Box& bx : hd.boxes)
                if (auto at = meets(place(bx, sa.pos, sa.facing), d)) {
                    if (n < strikes.size()) strikes[n++] = {a, sa.move, static_cast<std::int32_t>(h), false, -1, *at, a, b};
                    break;
                }
        }
    }
    for (std::int32_t a = 0; a < 2; ++a) {
        const std::int32_t c = 1 - a;  // the other side's clone
        if (!state_.clones[static_cast<std::size_t>(c)].alive) continue;
        std::array<Rect, 4> shape{};
        const std::size_t pieces_of = hurtboxes(2 + c, shape);
        auto touches = [&](const Rect& r) { return std::ranges::any_of(std::span(shape).first(pieces_of), [&](const Rect& q) { return r.overlaps(q); }); };
        bool touched = false;
        const FighterState& sa = state_.f[static_cast<std::size_t>(a)];
        if (sa.mode == Mode::move) {
            const MoveDef& m = fighter(a).def.moves[static_cast<std::size_t>(sa.move)];
            for (const HitDef& hd : m.hits)
                if (hd.active.contains(sa.move_frame))
                    for (const Box& bx : hd.boxes) touched = touched || touches(place(bx, sa.pos, sa.facing));
        }
        for (const ProjectileState& p : state_.p)
            if (p.alive && p.owner == a)
                for (const Box& bx : fighter(p.from).def.moves[static_cast<std::size_t>(p.move)].projectile->hit.boxes)
                    touched = touched || touches(place(bx, p.pos, p.facing));
        if (touched) vanish(c);
    }
    // Projectiles: against each other, then against the other fighter.
    for (std::size_t k = 0; k < state_.p.size(); ++k) {
        ProjectileState& p = state_.p[k];
        if (!p.alive) continue;
        const HitDef& hd = fighter(p.from).def.moves[static_cast<std::size_t>(p.move)].projectile->hit;
        if (!hd.active.contains(p.frame)) continue;
        for (std::size_t q = k + 1; q < state_.p.size() && p.alive; ++q) {
            ProjectileState& r = state_.p[q];
            if (!r.alive || r.owner == p.owner) continue;
            const HitDef& rh = fighter(r.from).def.moves[static_cast<std::size_t>(r.move)].projectile->hit;
            if (!rh.active.contains(r.frame)) continue;
            bool clash = false;
            for (const Box& b : hd.boxes)
                for (const Box& c : rh.boxes) clash = clash || place(b, p.pos, p.facing).overlaps(place(c, r.pos, r.facing));
            if (!clash) continue;
            // The weaker is gone; the stronger goes on, weakened by it.
            const std::int32_t spent = std::min(p.strength, r.strength);
            p.strength -= spent;
            r.strength -= spent;
            if (p.strength <= 0) p.alive = 0;
            if (r.strength <= 0) r.alive = 0;
            events_.push_back({Event::Kind::projectile_clash, p.owner, p.move, 0, 0, hd.impact, {(p.pos.x + r.pos.x) / 2, (p.pos.y + r.pos.y) / 2}});
        }
        if (!p.alive || p.stop > 0 || p.wait > 0) continue;
        // One that comes back hits once going and once coming back (G-19).
        if (fighter(p.from).def.moves[static_cast<std::size_t>(p.move)].projectile->turn > 0 && (p.struck & (p.turned ? 2 : 1))) continue;
        const std::int32_t d = 1 - p.owner;
        if (invulnerable(d, to_projectile)) continue;
        for (const Box& b : hd.boxes)
            if (auto at = meets(place(b, p.pos, p.facing), d)) {
                if (n < strikes.size()) strikes[n++] = {p.owner, p.move, 0, true, static_cast<std::int32_t>(k), *at, p.from, p.owner};
                break;
            }
    }
    // Throws: both at once break each other; otherwise the throw takes hold
    // unless a strike lands on the thrower this frame.
    if (throwing[0] && throwing[1]) {
        for (std::int32_t i = 0; i < 2; ++i) {
            FighterState& f = state_.f[static_cast<std::size_t>(i)];
            f.mode = Mode::blockstun;
            f.stun = 12;
            f.move = -1;
            f.push = Fixed::of(4) * away(state_.f[static_cast<std::size_t>(1 - i)], f);
        }
        events_.push_back({Event::Kind::tech, 0, -1, 0, 0, 0, state_.f[0].pos});
        throwing = {false, false};
    }
    // Decided before any is applied: a trade is two hits on one frame.
    std::array<bool, 16> blocked{};
    std::array<std::int32_t, 16> counter{};
    for (std::size_t k = 0; k < n; ++k) {
        const Strike& s = strikes[k];
        const MoveDef& m = fighter(s.from).def.moves[static_cast<std::size_t>(s.move)];
        const HitDef& hd = s.projectile ? m.projectile->hit : m.hits[static_cast<std::size_t>(s.hit)];
        const std::int32_t d = 1 - s.attacker;
        const FighterState& sd = state_.f[static_cast<std::size_t>(d)];
        blocked[k] = blocking(d, hd.level);
        // A counter catches a startup; a punish counter, a recovery.
        if (!blocked[k] && sd.mode == Mode::move)
            counter[k] = sd.move_frame < fighter(d).startup(sd.move) ? 1 : sd.move_frame >= fighter(d).recovery(sd.move) ? 2 : 0;
    }
    for (std::int32_t a = 0; a < 2; ++a) {
        if (!throwing[static_cast<std::size_t>(a)]) continue;
        bool struck = false;
        for (std::size_t k = 0; k < n; ++k) struck = struck || strikes[k].attacker != a;
        if (struck) continue;
        FighterState& sa = state_.f[static_cast<std::size_t>(a)];
        FighterState& sd = state_.f[static_cast<std::size_t>(1 - a)];
        const MoveDef& tm = fighter(a).def.moves[static_cast<std::size_t>(sa.move)];
        const ThrowDef& t = *tm.throw_;
        sa.landed |= throw_bit;
        sa.contact = 1;
        sa.thrower = 1;
        // A level 3 grab with a cinematic: the lock begins as it takes hold (P-5).
        if (tm.cinematic) {
            state_.lock = {a, sa.move, 0, sa.facing, sa.pos.x, sa.pos.y};
            if (tm.level > 0 && sa.super_scale == 0) sa.super_scale = std::max(100, system_.super_scaling[static_cast<std::size_t>(std::min(tm.level, 3))]);
            for (FighterState* f : {&sa, &sd}) {
                f->mode = Mode::cinematic;
                f->vel = {};
                f->stop = 0;
                f->queued = -1;
            }
            for (ProjectileState& p : state_.p) p.alive = 0;
            events_.push_back({Event::Kind::cinematic, a, sa.move, 0, 0, 0, sd.pos});
            continue;
        }
        const bool staggered = sd.mode == Mode::broken;  // a guard break's throw is not broken
        if (t.air) sd.pos.y = Fixed{};  // pulled down out of the air
        sd.mode = Mode::thrown;
        sd.move = -1;
        sd.tech = staggered ? 0 : t.tech_window;
        sd.stun = std::max(t.tech_window, 1);
        sd.throw_damage = t.damage;
        sd.vel = {};
        if (t.swap) {
            // A back throw: the two change places, each facing the other.
            std::swap(sa.pos.x, sd.pos.x);
            sa.facing = sd.pos.x >= sa.pos.x ? 1 : -1;
            sd.facing = -sa.facing;
        }
    }
    for (std::size_t k = 0; k < n; ++k) apply(strikes[k], blocked[k], counter[k]);
}

// Blade on blade (system 0.3): two strikes active on one frame whose hit
// boxes meet, facing each other. The super wins over anything else; within
// the clash window the heavier wins, or with equal weight neither does (a
// perfect clash: nothing changes); outside it the fresher wins — the one
// that met a strike already swung. Both strikes are spent and both moves end
// in a shared stop and a clash recovery; the loser takes the winner's
// posture, and breaks if that fills it.
void Match::clash() {
    FighterState& a = state_.f[0];
    FighterState& b = state_.f[1];
    if (state_.over || a.mode != Mode::move || b.mode != Mode::move || a.facing == b.facing) return;
    const MoveDef& ma = fighter(0).def.moves[static_cast<std::size_t>(a.move)];
    const MoveDef& mb = fighter(1).def.moves[static_cast<std::size_t>(b.move)];
    if (!ma.clash || !mb.clash) return;
    for (std::size_t h1 = 0; h1 < ma.hits.size() && h1 < 30; ++h1) {
        const HitDef& x = ma.hits[h1];
        if (!x.active.contains(a.move_frame) || (a.landed & (1 << h1))) continue;
        for (std::size_t h2 = 0; h2 < mb.hits.size() && h2 < 30; ++h2) {
            const HitDef& y = mb.hits[h2];
            if (!y.active.contains(b.move_frame) || (b.landed & (1 << h2))) continue;
            std::optional<Vec> at;
            for (const Box& bx : x.boxes)
                for (const Box& by : y.boxes)
                    if (!at) {
                        const Rect rx = place(bx, a.pos, a.facing), ry = place(by, b.pos, b.facing);
                        if (rx.overlaps(ry)) at = meeting(rx, ry);
                    }
            if (!at) continue;
            // A strike the other is invulnerable to passes through it: no blades meet.
            if (invulnerable(1, a.pos.y > Fixed{} ? to_air : to_strike) || invulnerable(0, b.pos.y > Fixed{} ? to_air : to_strike)) return;
            const std::int32_t t0 = a.move_frame - x.active.from, t1 = b.move_frame - y.active.from;
            const std::int32_t w0 = fighter(0).weights[static_cast<std::size_t>(a.move)], w1 = fighter(1).weights[static_cast<std::size_t>(b.move)];
            std::int32_t winner = -1;
            if ((ma.level > 0) != (mb.level > 0)) winner = ma.level > 0 ? 0 : 1;
            else if (std::abs(t0 - t1) <= system_.clash_window) winner = w0 == w1 ? -1 : (w0 > w1 ? 0 : 1);
            else winner = t0 < t1 ? 0 : 1;
            const std::array<std::int32_t, 2> hits{static_cast<std::int32_t>(h1), static_cast<std::int32_t>(h2)};
            const std::array<std::int32_t, 2> moves{a.move, b.move};
            const std::array<bool, 2> cancel{ma.clash_cancel, mb.clash_cancel};
            const std::uint16_t impact = winner == 1 ? y.impact : x.impact;
            events_.push_back({Event::Kind::clash, winner, winner >= 0 ? moves[static_cast<std::size_t>(winner)] : moves[0], 0, 0, impact, *at});
            for (std::int32_t i = 0; i < 2; ++i) {
                FighterState& f = state_.f[static_cast<std::size_t>(i)];
                const FighterState& o = state_.f[static_cast<std::size_t>(1 - i)];
                f.landed |= 1 << hits[static_cast<std::size_t>(i)];
                f.move = -1;
                f.stop = std::max(f.stop, system_.clash_stop);
                f.push = Fixed::of(3) * away(o, f);
                if (f.pos.y > Fixed{}) {
                    f.mode = Mode::reset;  // a clash in the air: both fall apart
                    f.vel = {Fixed::of(2) * away(o, f), max(f.vel.y, Fixed{})};
                } else {
                    f.mode = Mode::clash;
                    f.vel = {};
                    f.stun = winner == 1 - i ? system_.clash_lost : system_.clash_recovery;
                    f.clash_cancel = winner < 0 && cancel[static_cast<std::size_t>(i)] ? 1 : 0;
                }
            }
            if (winner >= 0) {
                FighterState& loser = state_.f[static_cast<std::size_t>(1 - winner)];
                const auto& per = fighter(winner).posture[static_cast<std::size_t>(moves[static_cast<std::size_t>(winner)])];
                take_posture(1 - winner, per[static_cast<std::size_t>(hits[static_cast<std::size_t>(winner)])], *at);
                (void)loser;
            }
            return;
        }
    }
}

// Posture taken by fighter i (a blocked hit, a lost clash): unless steadied
// after a break; at its max, a guard break.
void Match::take_posture(std::int32_t i, std::int32_t amount, Vec at) {
    FighterState& f = state_.f[static_cast<std::size_t>(i)];
    f.quiet = 0;
    if (f.steady > 0 || amount <= 0) return;
    f.posture = std::min(fighter(i).def.posture_max, f.posture + amount);
    if (f.posture < fighter(i).def.posture_max || f.pos.y > Fixed{}) return;
    f.mode = Mode::broken;
    f.stun = system_.break_stagger;
    f.move = -1;
    f.vel = {};
    f.push = Fixed{};
    events_.push_back({Event::Kind::guard_break, 1 - i, -1, 0, 0, 0, at});
}

// A deflect (system 0.3): back pressed afresh just before the hit — not
// held for a while before (or soon after the last deflect, for a string) —
// read from the directions as given, so turning around is not a press.
bool Match::deflects(std::int32_t i) const noexcept {
    const FighterState& f = state_.f[static_cast<std::size_t>(i)];
    auto back = [&](std::int32_t age) {
        const int r = f.log.raw(age);
        return f.facing > 0 ? (r == 1 || r == 4 || r == 7) : (r == 3 || r == 6 || r == 9);
    };
    for (std::int32_t a = 0; a <= system_.deflect_window; ++a) {
        if (!back(a) || back(a + 1)) continue;
        if (f.deflected > 0) return true;
        bool fresh = true;
        for (std::int32_t k = a + 1; k <= a + system_.deflect_fresh && fresh; ++k) fresh = !back(k);
        if (fresh) return true;
    }
    return false;
}

void Match::apply(const Strike& s, bool blocked, std::int32_t counter) {
    FighterState& a = body(s.body);  // a clone's strike: its own body stands still and keeps the meter (none for its side)
    FighterState& d = state_.f[static_cast<std::size_t>(1 - s.attacker)];
    const MoveDef& m = fighter(s.from).def.moves[static_cast<std::size_t>(s.move)];
    const HitDef& h = s.projectile ? m.projectile->hit : m.hits[static_cast<std::size_t>(s.hit)];
    const Vec at = s.at;
    const bool own = s.from == s.attacker;  // a reflected projectile gives no one meter (G-15)
    if (s.projectile) {
        ProjectileState& p = state_.p[static_cast<std::size_t>(s.slot)];
        if (m.projectile->turn > 0) p.struck |= p.turned ? 2 : 1, p.stop = h.hitstop;  // it goes on (G-19)
        else if (--p.hits <= 0) p.alive = 0;
        else p.stop = h.hitstop, p.wait = m.projectile->interval;
    } else {
        a.landed |= 1 << s.hit;
        a.contact = 1;
    }
    d.queued = -1;
    const std::int32_t sign = away(a, d);
    if (blocked) {
        const bool deflect = m.level == 0 && deflects(1 - s.attacker);
        d.mode = Mode::blockstun;
        d.stun = deflect ? std::max(1, h.blockstun - system_.deflect_gain) : h.blockstun;
        d.move = -1;
        d.push = h.pushback * sign;
        if (m.level == 0 && own) a.meter = std::min(system_.meter_max, a.meter + m.meter_gain / 2);
        if (deflect) {
            // Neither side's posture changes; the defender is 4 frames better off.
            d.deflected = system_.deflect_again;
            d.quiet = 0;
            events_.push_back({Event::Kind::deflect, s.attacker, s.move, s.hit, 0, h.impact, at});
        } else {
            d.health -= h.chip;
            events_.push_back({Event::Kind::block, s.attacker, s.move, s.hit, h.chip, h.impact, at});
            const auto& per = fighter(s.from).posture[static_cast<std::size_t>(s.move)];
            take_posture(1 - s.attacker, s.projectile ? per.back() : per[static_cast<std::size_t>(s.hit)], at);
        }
    } else if (const auto& armor = d.mode == Mode::move ? fighter(1 - s.attacker).def.moves[static_cast<std::size_t>(d.move)].armor : std::nullopt;
               armor && armor->frames.contains(d.move_frame) && d.armored < armor->hits && m.level == 0) {
        // Super armour (G-16): the damage taken, the move goes on; both stand still for the hitstop.
        ++d.armored;
        const std::int32_t damage = h.damage;
        d.health -= damage;
        if (own) d.meter = std::min(system_.meter_max, d.meter + damage / 10);
        if (own) a.meter = std::min(system_.meter_max, a.meter + m.meter_gain);
        events_.push_back({Event::Kind::armor, s.attacker, s.move, s.hit, damage, h.impact, at});
        vanish(1 - s.attacker);
    } else {
        // The first hit on a broken guard: posture back to 0, steadied, and the combo from it scaled from 80%.
        if (d.mode == Mode::broken || d.posture >= fighter(1 - s.attacker).def.posture_max) {
            d.posture = 0;
            d.steady = system_.break_protect;
            d.scale_cap = system_.break_scaling;
        }
        d.quiet = 0;
        const auto& scaling = system_.scaling;
        std::int32_t step = scaling[static_cast<std::size_t>(std::min<std::int32_t>(d.combo, static_cast<std::int32_t>(scaling.size()) - 1))] *
                            d.scale_cap / 100;
        // A super locks its scaling once, at its first hit: no lower than its level's floor.
        if (m.level > 0) {
            if (a.super_scale == 0) a.super_scale = std::max(step, system_.super_scaling[static_cast<std::size_t>(std::min(m.level, 3))]);
            step = a.super_scale;
        }
        std::int32_t damage = h.damage * step / 100;
        if (counter) damage = damage * (counter == 2 ? system_.punish_damage : system_.counter_damage) / 100;
        d.health -= damage;
        if (own) d.meter = std::min(system_.meter_max, d.meter + damage / 10);
        ++d.combo;
        d.move = -1;
        const bool airborne = d.pos.y > Fixed{} || d.vel.y > Fixed{};
        const bool juggled = airborne && d.mode == Mode::hitstun;
        if (juggled && d.juggle < h.juggle_cost) {
            // Out of juggle points: an air reset, not another hit of the combo.
            d.mode = Mode::reset;
            d.vel = {Fixed::of(2) * sign, max(d.vel.y, Fixed::of(4))};
        } else if (h.launch.y > Fixed{} && (!juggled || !d.relaunched)) {
            if (juggled) d.juggle -= h.juggle_cost, d.relaunched = 1;
            else d.juggle = h.juggle;
            d.mode = Mode::hitstun;
            d.vel = {h.launch.x * sign, h.launch.y};
            d.stun = h.hitstun;
        } else if (airborne) {
            if (juggled) d.juggle -= h.juggle_cost;
            else d.juggle = 0;  // hit out of a jump: it falls, and any more is an air reset
            d.mode = Mode::hitstun;
            d.vel = {h.launch.x * sign, max(d.vel.y, Fixed{})};
            d.stun = h.hitstun;
        } else if (h.knockdown) {
            d.mode = Mode::knockdown;
            d.stun = h.down > 0 ? h.down : system_.knockdown;
            d.vel = {};
        } else {
            d.mode = Mode::hitstun;
            d.stun = h.hitstun + (counter == 2 ? system_.punish_hitstun : counter ? system_.counter_hitstun : 0);
            d.push = h.pushback * sign;
        }
        // A stall (G-22): its extra hitstun the first time in a combo only.
        if (h.stall > 0 && !d.stalled && d.mode == Mode::hitstun) {
            d.stun += h.stall;
            d.stalled = 1;
        }
        vanish(1 - s.attacker);  // hit, its clone is gone (G-21)
        if (m.level == 0 && own) a.meter = std::min(system_.meter_max, a.meter + m.meter_gain);  // a super gives none back
        events_.push_back({counter ? Event::Kind::counter : Event::Kind::hit, s.attacker, s.move, s.hit, damage, h.impact, at, static_cast<std::uint8_t>(counter == 2)});
        // A level-3 super's first hit: the cinematic locks both (P-5).
        if (m.cinematic && !s.projectile && s.hit == 0 && d.health > 0) {
            state_.lock = {s.attacker, s.move, 0, a.facing, a.pos.x, a.pos.y};
            for (FighterState* f : {&a, &d}) {
                f->mode = Mode::cinematic;
                f->vel = {};
                f->stop = 0;
                f->queued = -1;
            }
            for (ProjectileState& p : state_.p) p.alive = 0;
            events_.push_back({Event::Kind::cinematic, s.attacker, s.move, 0, 0, h.impact, at});
        }
    }
    // Each stands still for its own hitstop; a projectile's owner goes on.
    // A counter stops both a little longer; a punish counter longer still.
    const std::int32_t extra = blocked ? 0 : counter == 2 ? system_.punish_hitstop : counter ? system_.counter_hitstop : 0;
    d.stop = std::max(d.stop, h.hitstop + extra);
    if (!s.projectile) a.stop = std::max(a.stop, (h.hitstop_attacker >= 0 ? h.hitstop_attacker : h.hitstop) + extra);
    if (d.health <= 0) {
        d.health = 0;
        d.mode = Mode::ko;
        state_.winner = state_.over && state_.winner != s.attacker ? 2 : s.attacker;
        state_.over = 1;
        events_.push_back({Event::Kind::ko, s.attacker, s.move, s.hit, 0, h.impact, at});
    }
}

// Posture recovers after a quiet while: half as fast holding back, twice as
// fast going forward or attacking; a break's protection runs out while able
// to act; a deflect's window for the next hit closes.
void Match::recover(const std::array<bool, 2>& still) {
    for (std::int32_t i = 0; i < 2; ++i) {
        FighterState& f = state_.f[static_cast<std::size_t>(i)];
        if (still[static_cast<std::size_t>(i)]) continue;
        if (f.deflected > 0) --f.deflected;
        if (f.steady > 0 && actionable(i)) --f.steady;
        if (f.quiet < system_.posture_delay) {
            ++f.quiet;
            continue;
        }
        if (f.posture <= 0 || f.mode == Mode::broken) continue;
        const int d = f.log.direction(0);
        std::int32_t rate = system_.posture_rate;
        if (d == 1 || d == 4 || d == 7) rate = state_.frame % 2 == 0 ? rate : 0;  // half: every other frame
        else if (d == 3 || d == 6 || d == 9 || f.mode == Mode::move) rate *= 2;
        f.posture = std::max(0, f.posture - rate);
    }
}

// Time (G-20, G-21): a mark, a rewind, a clone.
void Match::act(std::int32_t i, const TimeDef& td) {
    FighterState& s = state_.f[static_cast<std::size_t>(i)];
    History& h = state_.history[static_cast<std::size_t>(i)];
    // Where it stood `back` frames ago (at that frame's start), or the earliest it has.
    auto stood = [&](std::int32_t back) { return h.where[static_cast<std::size_t>(std::max(1, state_.frame - back) & (History::frames - 1))]; };
    auto on_stage = [&](Fixed x) { return min(max(x, Fixed{}), system_.stage_width); };
    switch (td.act) {
        case TimeAct::mark:
            h.marked = 1;
            h.mark = {{s.pos.x, Fixed{}}, s.facing};
            events_.push_back({Event::Kind::mark, i, s.move, 0, 0, 0, h.mark.pos});
            break;
        case TimeAct::rewind: {
            const Spot to = h.marked ? h.mark : stood(td.back);
            h.marked = 0;
            events_.push_back({Event::Kind::rewind, i, s.move, 0, 0, 0, s.pos});
            s.pos = {on_stage(to.pos.x), Fixed{}};
            s.facing = to.facing;
            s.vel = {};
            s.move = -1;
            s.mode = td.recovery > 0 ? Mode::landing : Mode::idle;
            s.stun = td.recovery;
            break;
        }
        case TimeAct::clone: {
            Clone& c = state_.clones[static_cast<std::size_t>(i)];
            const Spot from = stood(120);
            c = {};
            c.alive = 1;
            c.left = td.frames;
            c.body.pos = {on_stage(from.pos.x), Fixed{}};
            c.body.facing = from.facing;
            c.body.health = s.health;
            events_.push_back({Event::Kind::clone, i, s.move, 0, 0, 0, c.body.pos});
            break;
        }
    }
}

void Match::vanish(std::int32_t side_of) {
    Clone& c = state_.clones[static_cast<std::size_t>(side_of)];
    if (!c.alive) return;
    c.alive = 0;
    events_.push_back({Event::Kind::vanish, side_of, -1, 0, 0, 0, c.body.pos});
}

// Set pieces (G-15).

bool Match::has_piece(std::int32_t i) const noexcept {
    return std::ranges::any_of(state_.pieces, [i](const PieceState& q) { return q.alive && q.owner == i; });
}

void Match::act(std::int32_t i, const PieceDef& pd) {
    const FighterState& s = state_.f[static_cast<std::size_t>(i)];
    const FighterState& o = state_.f[static_cast<std::size_t>(1 - i)];
    const std::int32_t toward = o.pos.x >= s.pos.x ? 1 : -1;  // from the owner to the other fighter
    switch (pd.act) {
        case PieceAct::place:
            for (const Fixed d : pd.at) place_piece(i, pd.at_opponent ? o.pos.x - d * toward : s.pos.x + d * s.facing, pd);
            break;
        case PieceAct::shatter: {
            const std::int32_t shard = pd.shard_move >= 0 ? pd.shard_move : -1;
            bool any = false;
            for (std::size_t q = 0; q < state_.pieces.size(); ++q) {
                const PieceState& piece = state_.pieces[q];
                if (!piece.alive || piece.owner != i) continue;
                any = true;
                shatter(q, shard >= 0 ? shard : piece.shard, shard >= 0 ? pd.shards : piece.shards);
            }
            // None standing: one placed where it says, and shattered at once.
            if (!any && !pd.at.empty() && shard >= 0) {
                place_piece(i, pd.at_opponent ? o.pos.x - pd.at[0] * toward : s.pos.x + pd.at[0] * s.facing, pd);
                for (std::size_t q = 0; q < state_.pieces.size(); ++q)
                    if (state_.pieces[q].alive && state_.pieces[q].owner == i) shatter(q, shard, pd.shards);
            }
            break;
        }
        case PieceAct::warp: {
            // To the far side of the piece nearest the other fighter; it shatters.
            std::size_t best = state_.pieces.size();
            for (std::size_t q = 0; q < state_.pieces.size(); ++q) {
                const PieceState& piece = state_.pieces[q];
                if (piece.alive && piece.owner == i &&
                    (best == state_.pieces.size() || abs(piece.pos.x - o.pos.x) < abs(state_.pieces[best].pos.x - o.pos.x)))
                    best = q;
            }
            if (best == state_.pieces.size()) break;
            const PieceState& piece = state_.pieces[best];
            const std::int32_t side = o.pos.x >= piece.pos.x ? 1 : -1;
            FighterState& f = state_.f[static_cast<std::size_t>(i)];
            f.pos.x = min(max(piece.pos.x - Fixed::of(40) * side, Fixed{}), system_.stage_width);
            shatter(best, piece.shard, piece.shards);
            break;
        }
    }
}

void Match::place_piece(std::int32_t i, Fixed x, const PieceDef& pd) {
    // The oldest of the owner's give way, down to one fewer than the limit (three at most).
    for (;;) {
        std::int32_t standing = 0;
        std::size_t oldest = state_.pieces.size();
        for (std::size_t q = 0; q < state_.pieces.size(); ++q) {
            const PieceState& piece = state_.pieces[q];
            if (!piece.alive || piece.owner != i) continue;
            ++standing;
            if (oldest == state_.pieces.size() || piece.placed < state_.pieces[oldest].placed) oldest = q;
        }
        if (standing < std::min(pd.limit, 3)) break;
        state_.pieces[oldest].alive = 0;
    }
    for (PieceState& piece : state_.pieces) {
        if (piece.alive) continue;
        piece = {1, i, {min(max(x, Fixed{}), system_.stage_width), Fixed{}}, pd.durability, pd.life, pd.reflects, pd.shard_move, pd.shards, 0, state_.frame};
        events_.push_back({Event::Kind::piece, i, -1, 0, 0, 0, piece.pos});
        return;
    }
}

void Match::shatter(std::size_t q, std::int32_t shard, std::int32_t shards) {
    PieceState& piece = state_.pieces[q];
    piece.alive = 0;
    events_.push_back({Event::Kind::shatter, piece.owner, shard, 0, 0, 0, {piece.pos.x, piece.pos.y + 85_fx}});
    if (shard < 0) return;
    const ProjectileDef& pd = *fighter(piece.owner).def.moves[static_cast<std::size_t>(shard)].projectile;
    const FighterState& o = state_.f[static_cast<std::size_t>(1 - piece.owner)];
    const std::int32_t facing = o.pos.x >= piece.pos.x ? 1 : -1;
    // Its shards, one above another, toward the other fighter; none past a full pool.
    for (std::int32_t k = 0; k < shards; ++k)
        for (ProjectileState& p : state_.p)
            if (!p.alive) {
                p = {1, piece.owner, piece.owner, shard, 0, facing, {piece.pos.x + pd.offset.x * facing, piece.pos.y + pd.offset.y + Fixed::of(35 * k)},
                     pd.hits, pd.strength, 0, 0, 0, 0, 0};
                break;
            }
}

// The pieces against the other side's projectiles (stopped, or turned back)
// and against strikes (a strike costs a piece once; its owner's shatters
// it), then their life.
void Match::pieces() {
    for (ProjectileState& p : state_.p) {
        if (!p.alive || p.stop > 0 || p.wait > 0) continue;
        const MoveDef& m = fighter(p.from).def.moves[static_cast<std::size_t>(p.move)];
        const ProjectileDef& pd = *m.projectile;
        if (!pd.hit.active.contains(p.frame)) continue;
        for (std::size_t q = 0; q < state_.pieces.size() && p.alive; ++q) {
            PieceState& piece = state_.pieces[q];
            if (!piece.alive || piece.owner == p.owner) continue;
            const Rect at = place(system_.piece_box, piece.pos, 1);
            if (!std::ranges::any_of(pd.hit.boxes, [&](const Box& b) { return place(b, p.pos, p.facing).overlaps(at); })) continue;
            const bool reflectable = pd.reflectable ? *pd.reflectable : m.level == 0 && pd.strength < 2;
            if (piece.reflects > 0 && reflectable && !p.reflected) {
                p.owner = piece.owner;
                p.facing = -p.facing;
                p.reflected = 1;
                events_.push_back({Event::Kind::reflect, piece.owner, p.move, 0, 0, pd.hit.impact, {piece.pos.x, p.pos.y}});
                if (--piece.reflects == 0) shatter(q, piece.shard, piece.shards);
            } else {
                if (--p.hits <= 0) p.alive = 0;
                else p.stop = pd.hit.hitstop, p.wait = pd.interval;
                --piece.durability;
                events_.push_back({Event::Kind::piece_hit, p.owner, p.move, piece.durability, 0, pd.hit.impact, {piece.pos.x, p.pos.y}});
                if (piece.durability <= 0) piece.alive = 0;
            }
            break;
        }
    }
    for (std::size_t q = 0; q < state_.pieces.size(); ++q) {
        PieceState& piece = state_.pieces[q];
        if (!piece.alive) continue;
        const Rect at = place(system_.piece_box, piece.pos, 1);
        std::int32_t touched = 0;
        for (std::int32_t a = 0; a < 2; ++a) {
            const FighterState& f = state_.f[static_cast<std::size_t>(a)];
            if (f.mode != Mode::move) continue;
            const MoveDef& m = fighter(a).def.moves[static_cast<std::size_t>(f.move)];
            for (std::size_t h = 0; h < m.hits.size() && h < 30; ++h)
                if (m.hits[h].active.contains(f.move_frame) && !(f.landed & (1 << h)) &&
                    std::ranges::any_of(m.hits[h].boxes, [&](const Box& b) { return place(b, f.pos, f.facing).overlaps(at); }))
                    touched |= 1 << a;
        }
        const std::int32_t fresh = touched & ~piece.touched;
        piece.touched = touched;
        if (fresh & (1 << piece.owner)) {
            shatter(q, piece.shard, piece.shards);
        } else if (fresh) {
            --piece.durability;
            events_.push_back({Event::Kind::piece_hit, 1 - piece.owner, -1, piece.durability, 0, 0, {piece.pos.x, piece.pos.y + 100_fx}});
            if (piece.durability <= 0) piece.alive = 0;
        }
    }
    for (PieceState& piece : state_.pieces)  // from the frame after it was placed
        if (piece.alive && piece.placed != state_.frame && --piece.life <= 0) piece.alive = 0;
}

void Match::play_lock() {
    Lock& l = state_.lock;
    const std::int32_t ai = l.owner, di = 1 - l.owner;
    FighterState& a = state_.f[static_cast<std::size_t>(ai)];
    FighterState& d = state_.f[static_cast<std::size_t>(di)];
    const MoveDef& m = fighter(ai).def.moves[static_cast<std::size_t>(l.move)];
    const CinematicDef& c = *m.cinematic;
    ++l.frame;
    for (const CinematicHit& h : c.hits) {
        if (h.frame != l.frame) continue;
        const std::int32_t step = a.super_scale > 0 ? a.super_scale : 100;
        const std::int32_t damage = h.damage * step / 100;
        d.health = std::max(0, d.health - damage);
        ++d.combo;
        events_.push_back({Event::Kind::hit, ai, l.move, 0, damage, h.impact, {(a.pos.x + d.pos.x) / 2, d.pos.y + Fixed::of(90)}});
    }
    if (l.frame < c.frames) return;
    // The end: where each stands, the defender down, the attacker recovering.
    a.pos = {l.x + c.attacker_at.x * l.facing, max(Fixed{}, l.y + c.attacker_at.y)};
    d.pos = {l.x + c.defender_at.x * l.facing, max(Fixed{}, l.y + c.defender_at.y)};
    a.pos.x = min(max(a.pos.x, Fixed{}), system_.stage_width);
    d.pos.x = min(max(d.pos.x, Fixed{}), system_.stage_width);
    a.mode = Mode::landing;
    a.stun = c.recovery;
    a.move = -1;
    d.mode = Mode::knockdown;
    d.stun = c.down;
    d.move = -1;
    l = {};
    if (d.health <= 0) {
        d.mode = Mode::ko;
        state_.over = 1;
        state_.winner = ai;
        events_.push_back({Event::Kind::ko, ai, -1, 0, 0, 0, d.pos});
    }
}

void Match::tick(Pad left, Pad right) {
    events_.clear();
    // A round over plays on as its finish: the fallen falls, the winner's move runs out and it stands;
    // nobody's pad is read, nothing lands (contacts and clashes skip an ended round) and the clock stays.
    if (state_.over) left = right = Pad{};
    ++state_.frame;
    // The history (G-20, G-21): each side's pad, and where it stood at this frame's start.
    const std::array<Pad, 2> pads{left, right};
    constexpr std::int32_t ring = History::frames - 1;
    for (std::size_t i = 0; i < 2; ++i) {
        History& h = state_.history[i];
        const auto at = static_cast<std::size_t>(state_.frame & ring);
        h.pads[at] = static_cast<std::uint32_t>(pads[i].directions) | (static_cast<std::uint32_t>(pads[i].buttons) << 16);
        h.where[at] = {state_.f[i].pos, state_.f[i].facing};
    }
    state_.f[0].log.record(left, state_.f[0].facing, system_.socd);
    state_.f[1].log.record(right, state_.f[1].facing, system_.socd);
    // A clone plays its side's pad of 120 frames before.
    for (std::size_t c = 0; c < 2; ++c) {
        Clone& clone = state_.clones[c];
        if (!clone.alive) continue;
        const std::uint32_t was = state_.history[c].pads[static_cast<std::size_t>((state_.frame - 120) & ring)];
        clone.body.log.record(Pad{static_cast<std::uint16_t>(was & 0xffff), static_cast<std::uint16_t>(was >> 16)}, clone.body.facing, system_.socd);
    }
    // A cinematic: only it plays; the timer stands still.
    if (state_.lock.owner >= 0) {
        play_lock();
        return;
    }
    // A super's flash: everything stands still (its frames, its invulnerability,
    // the timer); what either side asks for is kept for the first frame after.
    if (state_.freeze > 0) {
        --state_.freeze;
        queue(0);
        queue(1);
        return;
    }
    // A body in hitstop stands still, keeping what it asks for; the others move.
    // (Bodies: the two fighters, then their clones.)
    std::array<bool, 4> still{};
    for (std::int32_t i = 0; i < 4; ++i) {
        if (i >= 2 && !state_.clones[static_cast<std::size_t>(i - 2)].alive) {
            still[static_cast<std::size_t>(i)] = true;
            continue;
        }
        FighterState& s = body(i);
        still[static_cast<std::size_t>(i)] = s.stop > 0;
        if (s.stop > 0) {
            --s.stop;
            queue(i);
        } else if (s.protect > 0) {
            --s.protect;  // counted at the start of each frame after the one it was given on
        }
    }
    for (std::int32_t i = 0; i < 4; ++i)
        if (!still[static_cast<std::size_t>(i)]) decide(i);
    // A super asked for: its flash begins on this frame and nothing moves on it;
    // its first frame comes after the flash (a grab on frame 1 takes hold then).
    if (state_.freeze > 0) return;
    for (std::int32_t i = 0; i < 4; ++i)
        if (!still[static_cast<std::size_t>(i)]) advance(i);
    for (Clone& c : state_.clones)  // not pushed, but on the stage
        if (c.alive) c.body.pos.x = min(max(c.body.pos.x, Fixed{}), system_.stage_width);
    for (ProjectileState& p : state_.p) {
        if (!p.alive) continue;
        if (p.stop > 0) {
            --p.stop;
            continue;
        }
        if (p.wait > 0) --p.wait;
        ++p.frame;
        const ProjectileDef& pd = *fighter(p.from).def.moves[static_cast<std::size_t>(p.move)].projectile;
        // One that comes back (G-19): from its turn, toward its owner wherever it is now; caught within 40.
        const FighterState& owner = state_.f[static_cast<std::size_t>(p.owner)];
        if (pd.turn > 0 && !p.turned && p.frame >= pd.turn) p.turned = 1;
        if (p.turned) p.facing = owner.pos.x >= p.pos.x ? 1 : -1;
        p.pos.x += pd.velocity.x * p.facing;
        p.pos.y += pd.velocity.y;
        const bool landed = pd.velocity.y < Fixed{} && p.pos.y <= Fixed{};
        const bool caught = p.turned && abs(owner.pos.x - p.pos.x) <= 40_fx;
        if (landed || caught || p.frame > pd.life || p.pos.x < Fixed{} || p.pos.x > system_.stage_width) p.alive = 0;
    }
    separate();
    resolve();
    face();
    recover({still[0], still[1]});
    for (std::size_t c = 0; c < 2; ++c)  // a clone's frames run out
        if (state_.clones[c].alive && --state_.clones[c].left <= 0) vanish(static_cast<std::int32_t>(c));
    if (!state_.over && !system_.timeless && --state_.timer <= 0) {
        state_.over = 1;
        const std::int32_t h0 = state_.f[0].health, h1 = state_.f[1].health;
        state_.winner = h0 > h1 ? 0 : h1 > h0 ? 1 : 2;
        events_.push_back({Event::Kind::time_over, state_.winner, -1, 0, 0, 0, {}});
    }
}

}  // namespace gongfu
