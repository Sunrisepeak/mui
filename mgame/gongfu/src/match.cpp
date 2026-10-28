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
            return c.motion.length > 0 ? 3 : c.charge.from != 0 ? 2 : c.direction != 0 ? 1 : 0;
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
    FighterState& s = state_.f[static_cast<std::size_t>(i)];
    const MoveDef& m = fighter(i).def.moves[static_cast<std::size_t>(move)];
    s.mode = Mode::move;
    s.move = move;
    s.move_frame = 0;
    s.landed = 0;
    s.contact = 0;
    s.thrower = 0;
    s.meter -= m.meter_cost;
    if (s.pos.y <= Fixed{}) s.vel = {};
    if (m.freeze > 0) state_.freeze = std::max(state_.freeze, m.freeze);
}

void Match::decide(std::int32_t i) {
    FighterState& s = state_.f[static_cast<std::size_t>(i)];
    FighterState& o = state_.f[static_cast<std::size_t>(1 - i)];
    const FighterDef& def = fighter(i).def;

    // A throw broken: both let go, pushed apart.
    if (s.mode == Mode::thrown) {
        if (s.tech > 0 && recognise(s.log, tech_command)) {
            for (FighterState* f : {&s, &o}) {
                f->mode = Mode::blockstun;
                f->stun = 12;
                f->move = -1;
                f->push = Fixed::of(4) * away(f == &s ? o : s, *f);
            }
            events_.push_back({Event::Kind::tech, 1 - i, o.move, 0, 0, 0, s.pos});
        }
        return;
    }
    // A move whose last frame has passed is over: the fighter acts on this one.
    if (s.mode == Mode::move && s.move_frame >= def.moves[static_cast<std::size_t>(s.move)].total) {
        s.move = -1;
        s.thrower = 0;
        if (s.pos.y > Fixed{}) s.mode = Mode::air, s.vel.y = std::min(s.vel.y, Fixed{});
        else s.mode = Mode::idle, s.vel = {}, s.combo = 0;
    }
    const bool ground = s.pos.y <= Fixed{};
    const bool free = neutral(s.mode) || s.mode == Mode::air;
    std::array<std::int32_t, 32> open{};
    const std::size_t open_count = open_cancels(i, open);
    auto is_open = [&](std::int32_t m) {
        return std::find(open.begin(), open.begin() + static_cast<std::ptrdiff_t>(open_count), m) != open.begin() + static_cast<std::ptrdiff_t>(open_count);
    };
    // A move asked for while standing still starts now, if it still may.
    if (s.queued >= 0) {
        const std::int32_t m = s.queued;
        s.queued = -1;
        if ((free || (s.mode == Mode::move && is_open(m))) && may_start(i, m)) {
            start(i, m);
            return;
        }
    }
    if (free || open_count > 0) {
        for (std::int32_t m : order_[static_cast<std::size_t>(i)]) {
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
        const bool threatened = o.mode == Mode::move && o.move_frame <= last_active(1 - i) &&
                                abs(o.pos.x - s.pos.x) <= system_.proximity_guard;
        s.mode = threatened ? Mode::idle : Mode::walk_back;
    } else {
        s.mode = Mode::idle;
    }
}

std::size_t Match::open_cancels(std::int32_t i, std::array<std::int32_t, 32>& out) const noexcept {
    const FighterState& s = state_.f[static_cast<std::size_t>(i)];
    if (s.mode != Mode::move) return 0;
    std::size_t n = 0;
    const MoveDef& m = fighter(i).def.moves[static_cast<std::size_t>(s.move)];
    const auto& targets = fighter(i).cancel_targets[static_cast<std::size_t>(s.move)];
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
    FighterState& s = state_.f[static_cast<std::size_t>(i)];
    if (s.queued >= 0) return;
    const bool free = neutral(s.mode) || s.mode == Mode::air;
    if (!free && s.mode != Mode::move) return;
    std::array<std::int32_t, 32> open{};
    const std::size_t n = open_cancels(i, open);
    const FighterDef& def = fighter(i).def;
    for (std::int32_t m : order_[static_cast<std::size_t>(i)]) {
        if (!free && std::find(open.begin(), open.begin() + static_cast<std::ptrdiff_t>(n), m) == open.begin() + static_cast<std::ptrdiff_t>(n)) continue;
        if (!may_start(i, m)) continue;
        if (recognise(s.log, def.moves[static_cast<std::size_t>(m)].command)) {
            s.queued = m;
            return;
        }
    }
}

bool Match::may_start(std::int32_t i, std::int32_t m) const noexcept {
    const FighterState& s = state_.f[static_cast<std::size_t>(i)];
    const MoveDef& md = fighter(i).def.moves[static_cast<std::size_t>(m)];
    if ((md.stance == Stance::air) != (s.pos.y > Fixed{})) return false;
    if (md.meter_cost > s.meter) return false;
    if (md.projectile && md.projectile->limit > 0) {
        std::int32_t alive = 0;
        for (const ProjectileState& p : state_.p)
            if (p.alive && p.owner == i && fighter(i).def.moves[static_cast<std::size_t>(p.move)].projectile->limit > 0) ++alive;
        if (alive >= md.projectile->limit) return false;
    }
    return true;
}

void Match::advance(std::int32_t i) {
    FighterState& s = state_.f[static_cast<std::size_t>(i)];
    const FighterDef& def = fighter(i).def;
    auto fall = [&] {
        s.vel.y -= def.gravity;
        s.pos = s.pos + s.vel;
    };
    auto to_neutral = [&] {
        s.mode = Mode::idle;
        s.move = -1;
        s.vel = {};
        s.combo = 0;
        s.juggle = 0;
        s.relaunched = 0;
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
                        p = {1, i, s.move, 0, s.facing, {s.pos.x + m.projectile->offset.x * s.facing, s.pos.y + m.projectile->offset.y},
                             m.projectile->hits, m.projectile->strength, 0, 0};
                        break;
                    }
            }
            if (s.pos.y < Fixed{}) {
                s.pos.y = Fixed{};
                if (airborne_start) {  // an air move ends where it lands
                    s.move = -1;
                    s.vel = {};
                    s.mode = def.landing > 0 ? Mode::landing : Mode::idle;
                    s.stun = def.landing;
                    break;
                }
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
                const FighterState& a = state_.f[static_cast<std::size_t>(1 - i)];
                const std::int32_t damage = s.throw_damage;
                s.throw_damage = 0;
                s.health -= damage;
                s.mode = Mode::knockdown;
                s.stun = system_.knockdown;
                events_.push_back({Event::Kind::throw_, 1 - i, a.move, 0, damage, 0, s.pos});
                if (s.health <= 0) {
                    s.health = 0;
                    s.mode = Mode::ko;
                    state_.over = 1;
                    state_.winner = 1 - i;
                    events_.push_back({Event::Kind::ko, 1 - i, a.move, 0, damage, 0, s.pos});
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
    if (const Fixed o = overlap(); o > Fixed{}) {
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
    if (const Fixed o = overlap(); o > Fixed{}) {
        if (place(lp, l.pos, l.facing).left <= Fixed{}) r.pos.x += o;
        else l.pos.x -= o;
        keep(l, lp);
        keep(r, rp);
    }
}

void Match::face() {
    for (std::int32_t i = 0; i < 2; ++i) {
        FighterState& s = state_.f[static_cast<std::size_t>(i)];
        const FighterState& o = state_.f[static_cast<std::size_t>(1 - i)];
        if (neutral(s.mode) && s.pos.y <= Fixed{} && o.pos.x != s.pos.x) s.facing = o.pos.x > s.pos.x ? 1 : -1;
    }
}

std::size_t Match::hurtboxes(std::int32_t i, std::array<Rect, 4>& out) const noexcept {
    const FighterState& s = state_.f[static_cast<std::size_t>(i)];
    const FighterDef& def = fighter(i).def;
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
    if (s.mode == Mode::knockdown || s.mode == Mode::wakeup || s.mode == Mode::ko || s.mode == Mode::thrown || s.mode == Mode::reset) return true;
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
    std::array<std::array<Rect, 4>, 2> body{};
    std::array<std::size_t, 2> bodies{hurtboxes(0, body[0]), hurtboxes(1, body[1])};
    // Where a box meets fighter d's body: the middle of the first overlap.
    auto meets = [&](const Rect& r, std::int32_t d) -> std::optional<Vec> {
        for (std::size_t k = 0; k < bodies[static_cast<std::size_t>(d)]; ++k)
            if (r.overlaps(body[static_cast<std::size_t>(d)][k])) return meeting(r, body[static_cast<std::size_t>(d)][k]);
        return std::nullopt;
    };
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
                    if (n < strikes.size()) strikes[n++] = {a, sa.move, static_cast<std::int32_t>(h), false, -1, *at};
                    break;
                }
        }
        if (m.throw_ && m.throw_->active.contains(sa.move_frame) && !(sa.landed & throw_bit)) {
            const bool grounded = sd.pos.y <= Fixed{} && (neutral(sd.mode) || sd.mode == Mode::landing || sd.mode == Mode::prejump || sd.mode == Mode::move);
            if (grounded && sd.protect == 0 && !invulnerable(d, to_throw) && meets(place(m.throw_->range, sa.pos, sa.facing), d))
                throwing[static_cast<std::size_t>(a)] = true;
        }
    }
    // Projectiles: against each other, then against the other fighter.
    for (std::size_t k = 0; k < state_.p.size(); ++k) {
        ProjectileState& p = state_.p[k];
        if (!p.alive) continue;
        const HitDef& hd = fighter(p.owner).def.moves[static_cast<std::size_t>(p.move)].projectile->hit;
        if (!hd.active.contains(p.frame)) continue;
        for (std::size_t q = k + 1; q < state_.p.size() && p.alive; ++q) {
            ProjectileState& r = state_.p[q];
            if (!r.alive || r.owner == p.owner) continue;
            const HitDef& rh = fighter(r.owner).def.moves[static_cast<std::size_t>(r.move)].projectile->hit;
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
        const std::int32_t d = 1 - p.owner;
        if (invulnerable(d, to_projectile)) continue;
        for (const Box& b : hd.boxes)
            if (auto at = meets(place(b, p.pos, p.facing), d)) {
                if (n < strikes.size()) strikes[n++] = {p.owner, p.move, 0, true, static_cast<std::int32_t>(k), *at};
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
    std::array<bool, 16> blocked{}, counter{};
    for (std::size_t k = 0; k < n; ++k) {
        const Strike& s = strikes[k];
        const MoveDef& m = fighter(s.attacker).def.moves[static_cast<std::size_t>(s.move)];
        const HitDef& hd = s.projectile ? m.projectile->hit : m.hits[static_cast<std::size_t>(s.hit)];
        const std::int32_t d = 1 - s.attacker;
        const FighterState& sd = state_.f[static_cast<std::size_t>(d)];
        blocked[k] = blocking(d, hd.level);
        counter[k] = !blocked[k] && sd.mode == Mode::move && sd.move_frame < fighter(d).startup(sd.move);
    }
    for (std::int32_t a = 0; a < 2; ++a) {
        if (!throwing[static_cast<std::size_t>(a)]) continue;
        bool struck = false;
        for (std::size_t k = 0; k < n; ++k) struck = struck || strikes[k].attacker != a;
        if (struck) continue;
        FighterState& sa = state_.f[static_cast<std::size_t>(a)];
        FighterState& sd = state_.f[static_cast<std::size_t>(1 - a)];
        const ThrowDef& t = *fighter(a).def.moves[static_cast<std::size_t>(sa.move)].throw_;
        sa.landed |= throw_bit;
        sa.contact = 1;
        sa.thrower = 1;
        sd.mode = Mode::thrown;
        sd.move = -1;
        sd.tech = t.tech_window;
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

void Match::apply(const Strike& s, bool blocked, bool counter) {
    FighterState& a = state_.f[static_cast<std::size_t>(s.attacker)];
    FighterState& d = state_.f[static_cast<std::size_t>(1 - s.attacker)];
    const MoveDef& m = fighter(s.attacker).def.moves[static_cast<std::size_t>(s.move)];
    const HitDef& h = s.projectile ? m.projectile->hit : m.hits[static_cast<std::size_t>(s.hit)];
    const Vec at = s.at;
    if (s.projectile) {
        ProjectileState& p = state_.p[static_cast<std::size_t>(s.slot)];
        if (--p.hits <= 0) p.alive = 0;
        else p.stop = h.hitstop, p.wait = m.projectile->interval;
    } else {
        a.landed |= 1 << s.hit;
        a.contact = 1;
    }
    d.queued = -1;
    const std::int32_t sign = away(a, d);
    if (blocked) {
        d.mode = Mode::blockstun;
        d.stun = h.blockstun;
        d.move = -1;
        d.health -= h.chip;
        d.push = h.pushback * sign;
        a.meter = std::min(system_.meter_max, a.meter + m.meter_gain / 2);
        events_.push_back({Event::Kind::block, s.attacker, s.move, s.hit, h.chip, h.impact, at});
    } else {
        const auto& scaling = system_.scaling;
        const std::int32_t step = scaling[static_cast<std::size_t>(std::min<std::int32_t>(d.combo, static_cast<std::int32_t>(scaling.size()) - 1))];
        std::int32_t damage = h.damage * step / 100;
        if (counter) damage = damage * system_.counter_damage / 100;
        d.health -= damage;
        d.meter = std::min(system_.meter_max, d.meter + damage / 10);
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
            d.stun = h.hitstun + (counter ? system_.counter_hitstun : 0);
            d.push = h.pushback * sign;
        }
        a.meter = std::min(system_.meter_max, a.meter + m.meter_gain);
        events_.push_back({counter ? Event::Kind::counter : Event::Kind::hit, s.attacker, s.move, s.hit, damage, h.impact, at});
    }
    // Each stands still for its own hitstop; a projectile's owner goes on.
    d.stop = std::max(d.stop, h.hitstop);
    if (!s.projectile) a.stop = std::max(a.stop, h.hitstop_attacker >= 0 ? h.hitstop_attacker : h.hitstop);
    if (d.health <= 0) {
        d.health = 0;
        d.mode = Mode::ko;
        state_.winner = state_.over && state_.winner != s.attacker ? 2 : s.attacker;
        state_.over = 1;
        events_.push_back({Event::Kind::ko, s.attacker, s.move, s.hit, 0, h.impact, at});
    }
}

void Match::tick(Pad left, Pad right) {
    events_.clear();
    if (state_.over) return;
    ++state_.frame;
    state_.f[0].log.record(left, state_.f[0].facing, system_.socd);
    state_.f[1].log.record(right, state_.f[1].facing, system_.socd);
    // A super's flash: everything stands still (its frames, its invulnerability,
    // the timer); what either side asks for is kept for the first frame after.
    if (state_.freeze > 0) {
        --state_.freeze;
        queue(0);
        queue(1);
        return;
    }
    // A fighter in hitstop stands still, keeping what it asks for; the other moves.
    std::array<bool, 2> still{};
    for (std::int32_t i = 0; i < 2; ++i) {
        FighterState& s = state_.f[static_cast<std::size_t>(i)];
        still[static_cast<std::size_t>(i)] = s.stop > 0;
        if (s.stop > 0) {
            --s.stop;
            queue(i);
        } else if (s.protect > 0) {
            --s.protect;  // counted at the start of each frame after the one it was given on
        }
    }
    for (std::int32_t i = 0; i < 2; ++i)
        if (!still[static_cast<std::size_t>(i)]) decide(i);
    for (std::int32_t i = 0; i < 2; ++i)
        if (!still[static_cast<std::size_t>(i)]) advance(i);
    for (ProjectileState& p : state_.p) {
        if (!p.alive) continue;
        if (p.stop > 0) {
            --p.stop;
            continue;
        }
        if (p.wait > 0) --p.wait;
        ++p.frame;
        const ProjectileDef& pd = *fighter(p.owner).def.moves[static_cast<std::size_t>(p.move)].projectile;
        p.pos.x += pd.velocity.x * p.facing;
        p.pos.y += pd.velocity.y;
        const bool landed = pd.velocity.y < Fixed{} && p.pos.y <= Fixed{};
        if (landed || p.frame > pd.life || p.pos.x < Fixed{} || p.pos.x > system_.stage_width) p.alive = 0;
    }
    separate();
    resolve();
    face();
    if (!state_.over && --state_.timer <= 0) {
        state_.over = 1;
        const std::int32_t h0 = state_.f[0].health, h1 = state_.f[1].health;
        state_.winner = h0 > h1 ? 0 : h1 > h0 ? 1 : 2;
        events_.push_back({Event::Kind::time_over, state_.winner, -1, 0, 0, 0, {}});
    }
}

}  // namespace gongfu
