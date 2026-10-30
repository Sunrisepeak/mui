module openmui.showcase.widget_breakout.sim;

import std;
import openmui.hash;
import mgame.world;
import mgame.input;
import mgame.physics;

namespace omui::breakout {

namespace {

using mgame::Box;
using mgame::V2;

constexpr double sqrt3 = 1.7320508075688772;    // tan 60°: the paddle's steepest bounce
constexpr double cos20 = 0.93969262078590838;   // a multi-ball's spread
constexpr double sin20 = 0.34202014332566871;
constexpr double cos40 = 0.766044443118978;    // and further out (correctly rounded)
constexpr double sin40 = 0.6427876096865394;
constexpr double cos8 = 0.99026806874157031;   // the orbit guard's turn
constexpr double sin8 = 0.13917310096006544;
constexpr double key_speed = 14, pointer_speed = 26;  // a paddle's most a step
constexpr double paddle_h = 16;
constexpr int    most_hits = 8;                  // collisions a ball meets in one step
constexpr double english = 0.5;                  // the paddle's motion, at its fastest, added across the ball's return
constexpr double spin_most = 0.005;              // radians a step a paddle at its fastest gives the ball
constexpr double spin_decay = 0.985;             // a step's spin of the last's
constexpr double steepest = 0.28;                // a ball's speed up or down, at least, of its speed (never along a row for ever)
constexpr double kick_across = 0.6;              // a kicker's direction: across per up
constexpr std::int32_t idle_most = 6 * steps_per_second;  // nothing moved this long: the next wall turns the ball a little
constexpr std::int32_t wave_steps = 72;          // the shutdown's wave
constexpr double wave_speed = 22;                // its radius grows this much a step
constexpr std::int32_t fuse_steps = 10;          // a full capacitor glows this long, then fires
constexpr std::int32_t early_fuse = 4;           // a switch discharging one early
constexpr std::int32_t cooldown = 60;            // a capacitor that fired takes no charge this long (two never fire each other for ever)
constexpr double finisher_half = 36;             // the finisher's beam: half its width

std::uint64_t next(std::uint64_t& s) noexcept {  // splitmix64
    std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

void happened(State& s, Happened what, std::uint32_t id, V2 at, std::int32_t n = 0, V2 to = {}) {
    if (s.events < s.event.size()) s.event[s.events++] = {what, id, at, to, n, 0};
}

Box paddle_box(const Paddle& p) noexcept { return {p.x - p.half, p.y - paddle_h / 2, 2 * p.half, paddle_h}; }
V2 middle(const Box& b) noexcept { return {b.x + b.w / 2, b.y + b.h / 2}; }

void add_sparks(State& s, std::int32_t n) { s.sparks = std::min(spark_cap, s.sparks + n); }

// Up or down at least `steepest` of its speed: a ball never runs along a row for ever.
V2 steep(V2 v) noexcept {
    const double len = mgame::length(v), least = steepest * len;
    if (len == 0 || std::abs(v.y) >= least) return v;
    const double y = v.y < 0 ? -least : least;
    const double x = std::sqrt(len * len - y * y);
    return {v.x < 0 ? -x : x, y};
}

// A ball resting on paddle `i`, waiting to be launched.
void rest(State& s, std::size_t i) {
    const mgame::Entity e = s.world.create();
    if (!e.valid()) return;
    const Paddle& p = s.paddles[i];
    const double side = i == 0 ? -1 : 1;  // above the bottom paddle, below the top one
    s.world.add(e, Ball{{p.x, p.y + side * (paddle_h / 2 + 8 + 1)}, {}, 8, static_cast<std::uint32_t>(i + 1), 0, 0, 0, 0, 0});
    if (i == 0) s.aim_phase = 60, s.aim_held = 0;
}

void move_paddle(Paddle& p, const mgame::Actions& a, double half) {
    const double was = p.x;
    if (a.pointer_x >= 0) {
        const double target = a.pointer_x / 32767.0 * field_w;
        p.x += std::clamp(target - p.x, -pointer_speed, pointer_speed);
    } else {
        p.x += a.axis(0) * key_speed;
    }
    p.half = half;
    p.x = std::clamp(p.x, p.half, field_w - p.half);
    p.moved = p.x - was;
}

// The gift a broken brick drops: its own, or now and then one by chance (of the rules' mask; the state's own generator).
void drop(State& s, V2 at, Gift own) {
    const std::uint64_t r = next(s.rng);
    if (s.ending) return;
    if (own == Gift::none && r % static_cast<std::uint64_t>(s.rules.drop_one_in) != 0) return;
    Gift g = own;
    if (g == Gift::none) {
        const std::uint32_t mask = static_cast<std::uint32_t>(s.rules.gift_mask) & 0x3eu;
        const int count = std::popcount(mask);
        if (count == 0) return;
        int nth = static_cast<int>((r >> 8) % static_cast<std::uint64_t>(count));
        for (std::uint32_t k = 1; k < 6; ++k)
            if ((mask >> k) & 1u) {
                if (nth == 0) {
                    g = static_cast<Gift>(k);
                    break;
                }
                --nth;
            }
    }
    const mgame::Entity e = s.world.create();
    if (!e.valid()) return;
    s.world.add(e, Capsule{at, g, 0});
    happened(s, Happened::gift, static_cast<std::uint32_t>(g), at);
}

void give(State& s, Gift g) {
    switch (g) {
        case Gift::wide: s.paddles[0].wide = s.rules.wide_steps; break;
        case Gift::slow: s.slow = s.rules.slow_steps; break;
        case Gift::pierce: s.world.each<Ball>([&](mgame::Entity, Ball& b) { b.pierce = static_cast<std::uint32_t>(s.rules.pierce_steps); }); break;
        case Gift::magnet: s.paddles[0].magnet = s.rules.magnet_catches; break;
        case Gift::multi: {
            // Each flying ball gains its extra balls: turned 20° one way and the other, then 40°.
            constexpr std::array<std::array<double, 3>, 4> turns{{{1, cos20, sin20}, {-1, cos20, sin20}, {1, cos40, sin40}, {-1, cos40, sin40}}};
            std::array<Ball, 64> more{};
            std::size_t n = 0;
            s.world.each<Ball>([&](mgame::Entity, Ball& b) {
                if (b.on != 0) return;
                for (std::size_t t = 0; t < static_cast<std::size_t>(s.rules.multi_balls); ++t)
                    if (n < more.size()) {
                        const auto [sign, c, sn] = turns[t];
                        Ball m = b;
                        m.velocity = steep({b.velocity.x * c - sign * b.velocity.y * sn, sign * b.velocity.x * sn + b.velocity.y * c});
                        more[n++] = m;
                    }
            });
            for (std::size_t i = 0; i < n; ++i)
                if (const mgame::Entity e = s.world.create(); e.valid()) s.world.add(e, more[i]);
            break;
        }
        case Gift::none: break;
    }
}

// What a ball meets first in what is left of its step: walls, paddles (by
// their motion relative to it), bricks; all met at that instant.
struct Contact {
    double                         t = 2;
    V2                             normal;
    std::array<mgame::Entity, 4>   bricks{};
    std::size_t                    count = 0;
    std::int32_t                   paddle = -1;
    bool                           wall = false;
};

// `left`: the part of the step still to go (the paddles stand where they were that far before their step's end).
Contact first_contact(const State& s, V2 at, double radius, V2 d, double left, bool paddles) {
    Contact c;
    constexpr double same = 1e-12;  // hits within this of each other are one instant
    auto meet = [&](const mgame::Hit& h, auto&& mark) {
        if (h.t < c.t - same) {
            c = {};
            c.t = h.t;
        }
        if (h.t <= c.t + same) {
            c.normal += h.normal;
            mark();
        }
    };
    // Walls: left, right and (unless a paddle guards it) the top.
    const std::array<Box, 3> walls{Box{-100, -100, 100, s.height + 200}, Box{field_w, -100, 100, s.height + 200}, Box{-100, -100, field_w + 200, 100}};
    const std::size_t wall_count = s.paddles[1].active ? 2 : 3;
    for (std::size_t w = 0; w < wall_count; ++w)
        if (auto h = mgame::sweep(at, radius, d, walls[w])) meet(*h, [&] { c.wall = true; });
    // Paddles, by the ball's motion relative to each (where the paddle stood before this step's move).
    for (std::size_t i = 0; paddles && i < 2; ++i) {
        if (!s.paddles[i].active) continue;
        Box from = paddle_box(s.paddles[i]);
        from.x -= s.paddles[i].moved * left;
        if (auto h = mgame::sweep(at, radius, d - V2{s.paddles[i].moved * left, 0}, from)) meet(*h, [&] { c.paddle = static_cast<std::int32_t>(i); });
    }
    // Bricks, by index: all but open shutters and gates.
    const bool open = gates_open(s);
    s.world.each<Body, Brick>([&](mgame::Entity e, const Body& body, const Brick& brick) {
        if ((brick.kind == Kind::gate && open) || (brick.kind == Kind::shutter && brick.on)) return;
        if (auto h = mgame::sweep(at, radius, d, body.box))
            meet(*h, [&] {
                if (c.count < c.bricks.size()) c.bricks[c.count++] = e;
            });
    });
    return c;
}

void zap(State& s, mgame::Entity e, std::int32_t damage);

// A switch flips, and what is on its circuit moves: shutters open or
// close, kickers turn, capacitors with a charge discharge now.
void flip(State& s, mgame::Entity e) {
    Brick& sw = s.world.get<Brick>(e);
    sw.on ^= 1u;
    const V2 from = middle(s.world.get<Body>(e).box);
    happened(s, Happened::flipped, sw.id, from, static_cast<std::int32_t>(sw.on));
    ++s.combo;
    if (sw.wire == 0) return;
    const std::uint32_t wire = sw.wire;
    s.world.each<Body, Brick>([&](mgame::Entity t, const Body& body, Brick& b) {
        if (t == e || b.wire != wire || b.kind == Kind::toggle) return;
        if (b.kind == Kind::shutter || b.kind == Kind::kicker) b.on ^= 1u;
        else if (b.kind == Kind::slider && b.charge > 0 && b.fuse == 0) b.fuse = early_fuse;
        else return;
        happened(s, Happened::wired, b.id, from, static_cast<std::int32_t>(b.on), middle(body.box));
    });
}

// A capacitor takes a hit's charge (a beam's too); full, it is about to fire.
void charge(State& s, mgame::Entity e) {
    Brick& c = s.world.get<Brick>(e);
    const V2 at = middle(s.world.get<Body>(e).box);
    if (c.fuse != 0) {  // about to fire, or cooling down
        happened(s, Happened::wall, c.id, at);
        return;
    }
    c.charge = std::min(c.durability, c.charge + 1);
    happened(s, Happened::charged, c.id, at, c.charge);
    ++s.combo;
    if (c.charge >= c.durability) {
        c.fuse = fuse_steps;
        happened(s, Happened::armed, c.id, at, c.charge);
    }
}

void shatter(State& s, mgame::Entity e) {  // a broken brick's due: score, sparks, a drop
    Brick& brick = s.world.get<Brick>(e);
    const V2 at = middle(s.world.get<Body>(e).box);
    happened(s, Happened::broke, brick.id, at, s.combo);
    s.score += 10 * (1 + s.combo);
    ++s.combo;
    if (brick.kind == Kind::hidden) {
        add_sparks(s, 30);
        happened(s, Happened::cache, brick.id, at, 30);
    }
    const Gift gift = brick.gift;
    const bool core = brick.kind == Kind::core;
    s.world.destroy(e);
    --s.bricks;
    if (core) {
        s.score += 500;
        --s.cores;
        s.freeze = std::max(s.freeze, 30);
        happened(s, Happened::shutdown, brick.id, at);
        if (s.cores <= 0) {  // the room shuts down: a wave from here, the capsules gone
            s.ending = wave_steps;
            s.core_at = at;
            std::array<mgame::Entity, 64> gone{};
            std::size_t n = 0;
            s.world.each<Capsule>([&](mgame::Entity c, Capsule&) {
                if (n < gone.size()) gone[n++] = c;
            });
            for (std::size_t i = 0; i < n; ++i) s.world.destroy(gone[i]);
        }
        return;
    }
    drop(s, at, gift);
}

// A breakable brick takes `damage`.
void damage_brick(State& s, mgame::Entity e, std::int32_t damage) {
    Brick& brick = s.world.get<Brick>(e);
    const V2 at = middle(s.world.get<Body>(e).box);
    brick.durability -= damage;
    if (brick.kind == Kind::core) {
        s.freeze = std::max(s.freeze, 4);
        happened(s, Happened::core_hit, brick.id, at, std::max(0, brick.durability));
    } else {
        happened(s, Happened::hit, brick.id, at, std::max(0, brick.durability));
    }
    if (brick.durability > 0) {
        if (brick.kind == Kind::core) ++s.combo;
        return;
    }
    shatter(s, e);
}

// A beam from a capacitor (along its row) or a paddle's finisher (up its
// column): to the nearest wall, closed shutter or gate each way, everything
// in between taking `damage` (a switch flipped, a capacitor charged by one).
void beam(State& s, const Box& from, bool across, std::int32_t damage, mgame::Entity self, std::uint32_t id) {
    double lo = 0, hi = across ? field_w : from.y;  // along the beam: its reach either way (up the field only, from a paddle)
    const double a0 = across ? from.y : from.x, a1 = across ? from.bottom() : from.right();  // its band, across it
    const double f0 = across ? from.x : from.y, f1 = across ? from.right() : from.y;         // the source's extent along it
    auto along = [&](const Box& b) { return across ? std::pair{b.x, b.right()} : std::pair{b.y, b.bottom()}; };
    auto band = [&](const Box& b) { return across ? b.y < a1 && b.bottom() > a0 : b.x < a1 && b.right() > a0; };
    s.world.each<Body, Brick>([&](mgame::Entity e, const Body& body, const Brick& b) {
        if (e == self || !band(body.box) || !blocks(s, b)) return;
        const auto [b0, b1] = along(body.box);
        if (b1 <= f0) lo = std::max(lo, b1);
        if (across && b0 >= f1) hi = std::min(hi, b0);
    });
    std::array<mgame::Entity, 96> struck{};
    std::size_t n = 0;
    s.world.each<Body, Brick>([&](mgame::Entity e, const Body& body, const Brick& b) {
        if (e == self || !band(body.box) || blocks(s, b)) return;
        const auto [b0, b1] = along(body.box);
        if (b1 > lo && b0 < hi && n < struck.size()) struck[n++] = e;
    });
    const double mid = (a0 + a1) / 2;
    happened(s, Happened::beam, id, across ? V2{lo, mid} : V2{mid, from.y}, damage, across ? V2{hi, mid} : V2{mid, lo});
    s.freeze = std::max(s.freeze, 5);
    s.idle = 0;
    for (std::size_t i = 0; i < n; ++i)
        if (s.world.alive(struck[i])) zap(s, struck[i], damage);
}

// What a beam does to a brick.
void zap(State& s, mgame::Entity e, std::int32_t damage) {
    const Brick& b = s.world.get<Brick>(e);
    switch (b.kind) {
        case Kind::toggle: flip(s, e); break;
        case Kind::slider: charge(s, e); break;
        case Kind::button:
        case Kind::card:
        case Kind::hidden:
        case Kind::core: damage_brick(s, e, damage); break;
        case Kind::wall:
        case Kind::gate:
        case Kind::shutter:
        case Kind::kicker: break;
    }
}

void fire(State& s, mgame::Entity e) {
    Brick& c = s.world.get<Brick>(e);
    const std::int32_t damage = c.charge;
    c.charge = 0;
    c.fuse = -cooldown;
    if (damage > 0) beam(s, s.world.get<Body>(e).box, true, damage, e, c.id);
}

// A ball meets a brick (`normal`: of all it met at once). Whether it sent
// the ball somewhere (a kicker: only away from the side met, else it is a
// wall — a ball never kicked into what kicked it).
bool hit_brick(State& s, mgame::Entity e, Ball& ball, V2 normal) {
    Brick& brick = s.world.get<Brick>(e);
    const V2 at = middle(s.world.get<Body>(e).box);
    switch (brick.kind) {
        case Kind::wall:
        case Kind::gate:
        case Kind::shutter: happened(s, Happened::wall, brick.id, at); return false;
        case Kind::toggle: s.idle = 0; flip(s, e); return false;
        case Kind::slider: s.idle = 0; charge(s, e); return false;
        case Kind::kicker:
            if (mgame::dot(kick_direction(brick), normal) <= 0) {
                happened(s, Happened::wall, brick.id, at);
                return false;
            }
            s.idle = 0;
            ball.velocity = kick_direction(brick) * s.speed;
            ball.spin = 0;
            ++s.combo;
            happened(s, Happened::kicked, brick.id, at, static_cast<std::int32_t>(brick.on));
            return true;
        case Kind::button:
        case Kind::card:
        case Kind::hidden:
        case Kind::core: s.idle = 0; damage_brick(s, e, 1); return false;
    }
    return false;
}

// A ball meets a paddle: where it met steers it (from straight up at the
// middle to 60° at an edge), the paddle's motion adds across and gives it
// spin. The bottom paddle banks the chain (and, holding a magnet, holds the
// ball); in a duet, alternate returns ready the finisher. Whether the ball
// now rests on the paddle.
bool hit_paddle(State& s, Ball& b, std::size_t i, double paddle_at) {
    Paddle& p = s.paddles[i];
    const double px = p.x - p.moved * paddle_at;
    const double offset = std::clamp((b.at.x - px) / p.half, -1.0, 1.0);
    const double motion = std::clamp(p.moved / key_speed, -1.0, 1.0);
    const double up = i == 0 ? -1 : 1;
    b.velocity = mgame::normalized(V2{offset * sqrt3 + motion * english, up}) * s.speed;
    b.spin = motion * spin_most * (i == 0 ? 1 : -1);
    s.idle = 0;
    happened(s, Happened::paddle, static_cast<std::uint32_t>(i), b.at, static_cast<std::int32_t>(motion * 100));
    if (s.paddles[1].active) {
        s.duet = s.last_paddle >= 0 && s.last_paddle != static_cast<std::int32_t>(i) ? s.duet + 1 : 1;
        s.last_paddle = static_cast<std::int32_t>(i);
        happened(s, Happened::duet, static_cast<std::uint32_t>(i), b.at, s.duet);
    }
    if (i != 0) {
        if (s.duet >= duet_full && !s.finisher) {
            s.finisher = 1;
            happened(s, Happened::finisher, 1, b.at);
        }
        return false;  // the top paddle keeps the chain going
    }
    // The bottom paddle: the chain banked.
    if (s.combo >= 3) {
        const std::int32_t gain = 10 * s.combo;
        add_sparks(s, gain);
        s.best = std::max(s.best, s.combo);
        happened(s, Happened::banked, static_cast<std::uint32_t>(gain), b.at, s.combo);
    }
    s.combo = 0;
    if (s.finisher) {  // the duet's finisher: up the ball's column
        s.finisher = 0;
        s.duet = 0;
        beam(s, Box{b.at.x - finisher_half, p.y - paddle_h / 2, 2 * finisher_half, 0}, false, 2, {}, 0xffffffffu);
    }
    if (p.magnet > 0) {
        --p.magnet;
        b.on = 1;
        b.dx = std::clamp(b.at.x - p.x, -(p.half - 10), p.half - 10);
        b.at = {p.x + b.dx, p.y - (paddle_h / 2 + b.radius + 1)};
        b.velocity = {};
        b.spin = 0;
        s.aim_held = 0;
        happened(s, Happened::magnet, 0, b.at, p.magnet);
        return true;
    }
    return false;
}

void fly(State& s, mgame::Entity e) {
    {
        // Its spin turns it a little (exact: a step along the perpendicular, back to its speed), dying away.
        Ball& b = s.world.get<Ball>(e);
        if (b.spin != 0) {
            const double speed = mgame::length(b.velocity);
            b.velocity = steep(mgame::normalized(b.velocity + V2{-b.velocity.y, b.velocity.x} * b.spin) * speed);
            b.spin *= spin_decay;
            if (std::abs(b.spin) < 1e-5) b.spin = 0;
        }
    }
    double left = 1;
    for (int k = 0; k < most_hits && left > 0; ++k) {
        Ball& b = s.world.get<Ball>(e);
        const V2 d = b.velocity * left;
        const Contact c = first_contact(s, b.at, b.radius, d, left, true);
        if (c.t > 1) {
            b.at += d;
            return;
        }
        b.at += d * c.t;
        const double paddle_at = left * (1 - c.t);  // the paddles at this instant: that far before their step's end
        left *= 1 - c.t;
        // Breakable bricks are passed through while piercing; anything else bounces, off the normals of all met at once.
        bool through = b.pierce > 0 && c.count > 0 && c.paddle < 0 && !c.wall;
        for (std::size_t i = 0; i < c.count && through; ++i) through = breakable(s.world.get<Brick>(c.bricks[i]).kind);
        if (!through) {
            V2 n = mgame::normalized(c.normal);
            if (n == V2{}) n = mgame::normalized(-b.velocity);
            b.velocity = mgame::reflect(b.velocity, n);
        }
        if (c.paddle >= 0 && hit_paddle(s, b, static_cast<std::size_t>(c.paddle), paddle_at)) {
            for (std::size_t i = 0; i < c.count; ++i) (void)hit_brick(s, c.bricks[i], s.world.get<Ball>(e), c.normal);
            return;  // held by the magnet
        }
        if (c.wall) {
            happened(s, Happened::wall, 0xffffffffu, b.at);
            if (s.idle > idle_most) {  // round and round with nothing moving: turned 8°
                b.velocity = {b.velocity.x * cos8 - b.velocity.y * sin8, b.velocity.x * sin8 + b.velocity.y * cos8};
                s.idle = 0;
            }
        }
        bool kicked = false;
        for (std::size_t i = 0; i < c.count; ++i)
            if (s.world.alive(c.bricks[i])) kicked = hit_brick(s, c.bricks[i], s.world.get<Ball>(e), c.normal) || kicked;
        Ball& after = s.world.get<Ball>(e);
        if (c.paddle < 0 && !kicked) after.velocity = steep(after.velocity);
    }
    // Its collisions used up: it stays where the last left it (never through anything).
}

// The camera of a taller field: its bottom (the paddle) always on screen, zoomed
// out just enough to keep the highest ball in sight — all of it while a ball
// is in the upper chamber, or while the player looks — easing there a little
// each step.
void aim_camera(State& s, bool look) {
    if (s.height <= field_h) return;
    double top = s.height;
    bool above = false;
    s.world.each<Ball>([&](mgame::Entity, const Ball& b) {
        top = std::min(top, b.at.y - 4 * b.radius);
        above = above || b.above;
    });
    const double want = look || above ? widest(s) : std::clamp(field_h / (s.height - top), widest(s), 1.0);
    s.zoom += (want - s.zoom) * 0.08;
}

// The shutdown's wave: every breakable brick it reaches breaks, scoring.
void wave(State& s) {
    const double r = static_cast<double>(wave_steps - s.ending + 1) * wave_speed;
    std::array<mgame::Entity, 128> reached{};
    std::size_t n = 0;
    s.world.each<Body, Brick>([&](mgame::Entity e, const Body& body, const Brick& b) {
        if (!breakable(b.kind)) return;
        const V2 d = middle(body.box) - s.core_at;
        if (mgame::dot(d, d) <= r * r && n < reached.size()) reached[n++] = e;
    });
    for (std::size_t i = 0; i < n; ++i) {
        const Brick& b = s.world.get<Brick>(reached[i]);
        happened(s, Happened::broke, b.id, middle(s.world.get<Body>(reached[i]).box), -1);
        s.score += 10;
        s.world.destroy(reached[i]);
        --s.bricks;
    }
    if (--s.ending == 0) {
        s.over = 1;
        happened(s, Happened::cleared, 0, s.core_at);
    }
}

}  // namespace

V2 aim_direction(const State& s) noexcept { return mgame::normalized(V2{s.aim, -1}); }

V2 kick_direction(const Brick& b) noexcept { return mgame::normalized(V2{b.on ? kick_across : -kick_across, -1}); }

Rules bounded(Rules r) noexcept {
    constexpr std::int32_t s = steps_per_second;
    r.wide_steps = std::clamp(r.wide_steps, 1 * s, 30 * s);
    r.wide_half = std::clamp(r.wide_half, 80, 300);
    r.multi_balls = std::clamp(r.multi_balls, 1, 4);
    r.slow_steps = std::clamp(r.slow_steps, 1 * s, 20 * s);
    r.slow_percent = std::clamp(r.slow_percent, 25, 90);
    r.pierce_steps = std::clamp(r.pierce_steps, 1 * s, 20 * s);
    r.drop_one_in = std::clamp(r.drop_one_in, 2, 20);
    r.magnet_catches = std::clamp(r.magnet_catches, 1, 8);
    r.gift_mask &= 0x3e;
    return r;
}

State start(const Level& level) {
    State s;
    s.rng = level.seed;
    s.lives = level.lives;
    s.speed = level.speed;
    s.score = level.score;
    s.sparks = std::min(level.sparks, spark_cap);
    s.paddles[1].active = level.second_paddle ? 1 : 0;
    s.paddles[1].y = 30;
    s.height = std::max(level.height, field_h);
    s.chamber = level.chamber;
    s.paddles[0].y = s.height - 30;
    s.rules = bounded(level.rules);
    s.gate_from = level.gate_from;
    s.gate_to = level.gate_to;
    s.gate_period = level.gate_period;
    s.gate_open = level.gate_open;
    for (const BrickSpec& b : level.bricks) {
        const mgame::Entity e = s.world.create();
        if (!e.valid()) break;
        s.world.add(e, Body{b.box});
        s.world.add(e, Brick{b.kind, b.durability, b.id, b.row, b.gift, b.wire, b.on, 0, 0});
        if (breakable(b.kind)) ++s.bricks;
        if (b.kind == Kind::core) ++s.cores;
    }
    rest(s, 0);
    s.aim_phase = level.aim_phase;
    if (level.opening != Gift::none)
        if (const mgame::Entity e = s.world.create(); e.valid()) s.world.add(e, Capsule{{s.paddles[0].x, s.paddles[0].y - 80}, level.opening, 0});
    return s;
}

void step(State& s, const Input& in) {
    if (s.over) return;
    ++s.frame;
    s.events = 0;
    // Paddles first: what this frame's input asks moves them this frame (≤ 1 frame of latency), hit-stop or not.
    if (in.new_rules) s.rules = bounded(in.rules);  // at the step's start, all at once
    if (in.pay) s.sparks -= in.pay;
    move_paddle(s.paddles[0], in.player, s.paddles[0].wide > 0 ? s.rules.wide_half : 70);
    if (s.paddles[1].active) move_paddle(s.paddles[1], in.second, 70);
    aim_camera(s, in.player.down(look_button));
    if (in.aim_set) s.aim = std::clamp(in.aim, -aim_most, aim_most), s.aim_held = 1;
    // Balls on a paddle ride it, hit-stop or not.
    s.world.each<Ball>([&](mgame::Entity, Ball& b) {
        if (b.on != 0) b.at.x = s.paddles[b.on - 1].x + b.dx;
    });
    if (s.freeze > 0) {
        --s.freeze;
        return;
    }
    if (s.ending > 0) {
        wave(s);
        return;
    }
    if (s.paddles[0].wide > 0) --s.paddles[0].wide;
    if (s.slow > 0) --s.slow;
    ++s.idle;
    // The aim sweeps to and fro while a ball waits (a triangle, two seconds across), unless an agent holds it.
    bool waiting = false;
    s.world.each<Ball>([&](mgame::Entity, const Ball& b) { waiting = waiting || b.on == 1; });
    if (waiting && !s.aim_held) {
        const std::int32_t p = s.aim_phase % 240, u = p < 120 ? p : 240 - p;
        s.aim = (u / 60.0 - 1) * aim_most;
        ++s.aim_phase;
    }
    // The launch: from the bottom paddle along the aim; from the top one down, a little to the right.
    std::array<bool, 2> launch{in.player.went_down(launch_button), in.second.went_down(launch_button)};
    bool launched = false;
    s.world.each<Ball>([&](mgame::Entity, Ball& b) {
        if (b.on == 0) return;
        const std::size_t i = b.on - 1;
        if (!launch[i]) return;
        b.on = 0;
        b.dx = 0;
        b.velocity = (i == 0 ? aim_direction(s) : mgame::normalized(V2{0.35, 1.0})) * s.speed;
        launched = launched || i == 0;
        happened(s, Happened::launched, static_cast<std::uint32_t>(i), b.at);
    });
    if (launched) s.aim_held = 0;
    // Capacitors about to fire, by index.
    std::array<mgame::Entity, 32> due{};
    std::size_t f = 0;
    s.world.each<Brick>([&](mgame::Entity e, Brick& b) {
        if (b.kind != Kind::slider || b.fuse == 0) return;
        if (b.fuse < 0) ++b.fuse;  // cooling down
        else if (--b.fuse == 0 && f < due.size()) due[f++] = e;
    });
    for (std::size_t i = 0; i < f; ++i)
        if (s.world.alive(due[i])) fire(s, due[i]);
    // The flying balls, by index (a ball made this step flies from the next).
    std::array<mgame::Entity, 128> flying{};
    std::size_t n = 0;
    s.world.each<Ball>([&](mgame::Entity e, Ball& b) {
        if (b.on == 0 && n < flying.size()) flying[n++] = e;
    });
    for (std::size_t i = 0; i < n && !s.ending; ++i)
        if (s.world.alive(flying[i])) {
            fly(s, flying[i]);
            Ball& b = s.world.get<Ball>(flying[i]);
            if (b.pierce > 0) --b.pierce;
            if (s.chamber > 0) {  // into the upper chamber, and out of it
                if (!b.above && b.at.y < s.chamber - b.radius) {
                    b.above = 1;
                    happened(s, Happened::breach, 0, b.at, s.breached ? 0 : 1);
                    if (!s.breached) {
                        s.breached = 1;
                        s.freeze = std::max(s.freeze, 14);
                        add_sparks(s, 50);
                    }
                } else if (b.above && b.at.y > s.chamber + b.radius) {
                    b.above = 0;
                }
            }
            const bool out = b.at.y > s.height + b.radius || (s.paddles[1].active && b.at.y < -b.radius);
            if (out) {
                happened(s, Happened::lost, 0, b.at);
                s.world.destroy(flying[i]);
            }
        }
    if (s.ending) return;
    // Capsules fall; the bottom paddle catches them.
    std::array<mgame::Entity, 64> caught{};
    std::size_t c = 0;
    s.world.each<Capsule>([&](mgame::Entity e, Capsule& k) {
        k.at.y += 2.5;
        if (mgame::overlaps(k.at, 10, paddle_box(s.paddles[0])) || k.at.y > s.height + 20)
            if (c < caught.size()) caught[c++] = e;
    });
    for (std::size_t i = 0; i < c; ++i) {
        const Capsule k = s.world.get<Capsule>(caught[i]);
        s.world.destroy(caught[i]);
        if (k.at.y <= s.height + 20) {
            give(s, k.gift);
            happened(s, Happened::caught, static_cast<std::uint32_t>(k.gift), k.at);
        }
    }
    // No ball left: the chain unbanked is lost, then a life, and a new ball on the paddle; none left: over.
    bool any = false;
    s.world.each<Ball>([&](mgame::Entity, Ball&) { any = true; });
    if (!any) {
        if (s.combo >= 3) happened(s, Happened::chain_lost, 0, {s.paddles[0].x, s.paddles[0].y}, s.combo);
        --s.lives;
        s.combo = 0;
        s.duet = 0;
        s.finisher = 0;
        s.last_paddle = -1;
        s.paddles[0].magnet = 0;
        happened(s, Happened::life, static_cast<std::uint32_t>(std::max(0, s.lives)), {s.paddles[0].x, s.paddles[0].y});
        if (s.lives <= 0) {
            s.over = 2;
            happened(s, Happened::over, 2, {});
        } else {
            rest(s, 0);
        }
    }
    if (s.cores == 0 && s.bricks <= 0 && !s.over) {  // a room without a core: cleared with its last brick
        s.over = 1;
        happened(s, Happened::cleared, 0, {});
    }
}

std::size_t trace(const State& s, V2 at, V2 velocity, double radius, std::span<V2> points, double length) {
    if (points.empty()) return 0;
    std::size_t n = 0;
    points[n++] = at;
    V2 dir = mgame::normalized(velocity);
    if (dir == V2{}) return n;
    double left = length;
    while (n < points.size() && left > 0) {
        const V2 d = dir * left;
        const Contact c = first_contact(s, at, radius, d, 1, false);
        if (c.t > 1) {
            points[n++] = at + d;
            break;
        }
        at += d * c.t;
        left *= 1 - c.t;
        points[n++] = at;
        if (at.y > s.height) break;
        bool kicked = false;
        for (std::size_t i = 0; i < c.count; ++i)
            if (const Brick& b = s.world.get<Brick>(c.bricks[i]); b.kind == Kind::kicker && mgame::dot(kick_direction(b), c.normal) > 0)
                dir = kick_direction(b), kicked = true;
        if (!kicked) {
            V2 nrm = mgame::normalized(c.normal);
            if (nrm == V2{}) nrm = -dir;
            dir = mgame::normalized(steep(mgame::reflect(dir, nrm)));
        }
        if (left < 1) break;
    }
    return n;
}

std::uint64_t checksum(const State& s) noexcept {
    omui::hash::Fnv1a h(s.world.checksum());
    auto bytes = [&h](const auto& v) { h.bytes(std::as_bytes(std::span(&v, 1))); };
    for (const Paddle& p : s.paddles) bytes(p.x), bytes(p.y), bytes(p.half), bytes(p.moved), bytes(p.wide), bytes(p.active), bytes(p.magnet);
    bytes(s.rng), bytes(s.frame), bytes(s.score), bytes(s.combo), bytes(s.lives), bytes(s.slow), bytes(s.bricks), bytes(s.speed), bytes(s.over), bytes(s.gate_from), bytes(s.gate_to), bytes(s.height), bytes(s.zoom);
    bytes(s.gate_period), bytes(s.gate_open), bytes(s.chamber), bytes(s.aim), bytes(s.aim_phase), bytes(s.aim_held), bytes(s.freeze), bytes(s.ending), bytes(s.sparks);
    bytes(s.cores), bytes(s.best), bytes(s.duet), bytes(s.finisher), bytes(s.last_paddle), bytes(s.idle), bytes(s.breached), bytes(s.core_at.x), bytes(s.core_at.y);
    const Rules& r = s.rules;
    bytes(r.wide_steps), bytes(r.wide_half), bytes(r.multi_balls), bytes(r.slow_steps), bytes(r.slow_percent), bytes(r.pierce_steps), bytes(r.drop_one_in);
    bytes(r.magnet_catches), bytes(r.gift_mask);
    bytes(s.events);
    for (std::uint32_t i = 0; i < s.events; ++i) bytes(s.event[i]);
    return h.value();
}

}  // namespace omui::breakout
