module openmui.showcase.widget_breakout.sim;

import std;
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
constexpr double key_speed = 14, pointer_speed = 26;  // a paddle's most a step
constexpr double paddle_h = 16;
constexpr int    most_hits = 8;                  // collisions a ball meets in one step

std::uint64_t next(std::uint64_t& s) noexcept {  // splitmix64
    std::uint64_t z = (s += 0x9e3779b97f4a7c15ull);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

void happened(State& s, Happened what, std::uint32_t id, V2 at) {
    if (s.events < s.event.size()) s.event[s.events++] = {what, id, at};
}

Box paddle_box(const Paddle& p) noexcept { return {p.x - p.half, p.y - paddle_h / 2, 2 * p.half, paddle_h}; }

// A ball resting on paddle `i`, waiting to be launched.
void rest(State& s, std::size_t i) {
    const mgame::Entity e = s.world.create();
    if (!e.valid()) return;
    const Paddle& p = s.paddles[i];
    const double side = i == 0 ? -1 : 1;  // above the bottom paddle, below the top one
    s.world.add(e, Ball{{p.x, p.y + side * (paddle_h / 2 + 8 + 1)}, {}, 8, static_cast<std::uint32_t>(i + 1), 0});
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

// The gift a broken brick drops, now and then (the state's own generator).
void drop(State& s, V2 at) {
    const std::uint64_t r = next(s.rng);
    if (r % 6 != 0) return;
    const mgame::Entity e = s.world.create();
    if (!e.valid()) return;
    const Gift g = static_cast<Gift>(1 + (r >> 8) % 4);
    s.world.add(e, Capsule{at, g, 0});
    happened(s, Happened::gift, static_cast<std::uint32_t>(g), at);
}

void give(State& s, Gift g) {
    switch (g) {
        case Gift::wide: s.paddles[0].wide = 10 * steps_per_second; break;
        case Gift::slow: s.slow = 8 * steps_per_second; break;
        case Gift::pierce: s.world.each<Ball>([](mgame::Entity, Ball& b) { b.pierce = 5 * steps_per_second; }); break;
        case Gift::multi: {
            // Each flying ball becomes three: itself, and two turned 20° either way.
            std::array<Ball, 64> more{};
            std::size_t n = 0;
            s.world.each<Ball>([&](mgame::Entity, Ball& b) {
                if (b.on != 0) return;
                for (const double sign : {-1.0, 1.0})
                    if (n < more.size()) {
                        Ball c = b;
                        c.velocity = {b.velocity.x * cos20 - sign * b.velocity.y * sin20, sign * b.velocity.x * sin20 + b.velocity.y * cos20};
                        more[n++] = c;
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
Contact first_contact(const State& s, const Ball& b, V2 d, double left) {
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
    const std::array<Box, 3> walls{Box{-100, -100, 100, field_h + 200}, Box{field_w, -100, 100, field_h + 200}, Box{-100, -100, field_w + 200, 100}};
    const std::size_t wall_count = s.paddles[1].active ? 2 : 3;
    for (std::size_t w = 0; w < wall_count; ++w)
        if (auto h = mgame::sweep(b.at, b.radius, d, walls[w])) meet(*h, [&] { c.wall = true; });
    // Paddles, by the ball's motion relative to each (where the paddle stood before this step's move).
    for (std::size_t i = 0; i < 2; ++i) {
        if (!s.paddles[i].active) continue;
        Box from = paddle_box(s.paddles[i]);
        from.x -= s.paddles[i].moved * left;
        if (auto h = mgame::sweep(b.at, b.radius, d - V2{s.paddles[i].moved * left, 0}, from)) meet(*h, [&] { c.paddle = static_cast<std::int32_t>(i); });
    }
    // Bricks, by index.
    s.world.each<Body, Brick>([&](mgame::Entity e, const Body& body, const Brick& brick) {
        if (auto h = mgame::sweep(b.at, b.radius, d, body.box))
            meet(*h, [&] {
                if (c.count < c.bricks.size()) c.bricks[c.count++] = e;
            });
    });
    return c;
}

void hit_brick(State& s, mgame::Entity e) {
    Brick& brick = s.world.get<Brick>(e);
    const Box box = s.world.get<Body>(e).box;
    const V2 middle{box.x + box.w / 2, box.y + box.h / 2};
    if (brick.kind == Kind::wall) {
        happened(s, Happened::wall, brick.id, middle);
        return;
    }
    happened(s, Happened::hit, brick.id, middle);
    if (--brick.durability > 0) return;
    happened(s, Happened::broke, brick.id, middle);
    s.score += 10 * (1 + s.combo);
    ++s.combo;
    s.world.destroy(e);
    --s.bricks;
    drop(s, middle);
}

void fly(State& s, mgame::Entity e) {
    double left = 1;
    for (int k = 0; k < most_hits && left > 0; ++k) {
        Ball& b = s.world.get<Ball>(e);
        const V2 d = b.velocity * left;
        const Contact c = first_contact(s, b, d, left);
        if (c.t > 1) {
            b.at += d;
            return;
        }
        b.at += d * c.t;
        const double paddle_at = left * (1 - c.t);  // the paddles at this instant: that far before their step's end
        left *= 1 - c.t;
        // A brick passed through while piercing; anything else bounces, off the normals of all met at once.
        const bool through = b.pierce > 0 && c.count > 0 && c.paddle < 0 && !c.wall;
        if (!through) {
            V2 n = mgame::normalized(c.normal);
            if (n == V2{}) n = mgame::normalized(-b.velocity);
            b.velocity = mgame::reflect(b.velocity, n);
        }
        if (c.paddle >= 0) {
            // Where on the paddle it met steers it: from straight up at the middle to 60° at an edge.
            const Paddle& p = s.paddles[static_cast<std::size_t>(c.paddle)];
            const double offset = std::clamp((b.at.x - (p.x - p.moved * paddle_at)) / p.half, -1.0, 1.0);
            const double up = c.paddle == 0 ? -1 : 1;
            b.velocity = mgame::normalized(V2{offset * sqrt3, up}) * s.speed;
            s.combo = 0;
            happened(s, Happened::paddle, static_cast<std::uint32_t>(c.paddle), b.at);
        }
        if (c.wall) happened(s, Happened::wall, 0xffffffffu, b.at);
        for (std::size_t i = 0; i < c.count; ++i) hit_brick(s, c.bricks[i]);
    }
    // Its collisions used up: it stays where the last left it (never through anything).
}

}  // namespace

State start(const Level& level) {
    State s;
    s.rng = level.seed;
    s.lives = level.lives;
    s.speed = level.speed;
    s.paddles[1].active = level.second_paddle ? 1 : 0;
    s.paddles[1].y = 30;
    for (const BrickSpec& b : level.bricks) {
        const mgame::Entity e = s.world.create();
        if (!e.valid()) break;
        s.world.add(e, Body{b.box});
        s.world.add(e, Brick{b.kind, b.durability, b.id, b.row});
        if (b.kind != Kind::wall) ++s.bricks;
    }
    rest(s, 0);
    return s;
}

void step(State& s, const Input& in) {
    if (s.over) return;
    ++s.frame;
    s.events = 0;
    // Paddles first: what this frame's input asks moves them this frame (≤ 1 frame of latency).
    move_paddle(s.paddles[0], in.player, s.paddles[0].wide > 0 ? 110 : 70);
    if (s.paddles[1].active) move_paddle(s.paddles[1], in.second, 70);
    if (s.paddles[0].wide > 0) --s.paddles[0].wide;
    if (s.slow > 0) --s.slow;
    // Balls on a paddle ride it; the launch sends them up (down from the top one), a little to the right.
    std::array<bool, 2> launch{in.player.went_down(launch_button), in.second.went_down(launch_button)};
    s.world.each<Ball>([&](mgame::Entity, Ball& b) {
        if (b.on == 0) return;
        const std::size_t i = b.on - 1;
        const Paddle& p = s.paddles[i];
        b.at.x = p.x;
        if (launch[i]) {
            b.on = 0;
            b.velocity = mgame::normalized(V2{0.35, i == 0 ? -1.0 : 1.0}) * s.speed;
            happened(s, Happened::launched, static_cast<std::uint32_t>(i), b.at);
        }
    });
    // The flying balls, by index (a ball made this step flies from the next).
    std::array<mgame::Entity, 128> flying{};
    std::size_t n = 0;
    s.world.each<Ball>([&](mgame::Entity e, Ball& b) {
        if (b.on == 0 && n < flying.size()) flying[n++] = e;
    });
    for (std::size_t i = 0; i < n; ++i)
        if (s.world.alive(flying[i])) {
            fly(s, flying[i]);
            Ball& b = s.world.get<Ball>(flying[i]);
            if (b.pierce > 0) --b.pierce;
            const bool out = b.at.y > field_h + b.radius || (s.paddles[1].active && b.at.y < -b.radius);
            if (out) {
                happened(s, Happened::lost, 0, b.at);
                s.world.destroy(flying[i]);
            }
        }
    // Capsules fall; the bottom paddle catches them.
    std::array<mgame::Entity, 64> caught{};
    std::size_t c = 0;
    s.world.each<Capsule>([&](mgame::Entity e, Capsule& k) {
        k.at.y += 2.5;
        if (mgame::overlaps(k.at, 10, paddle_box(s.paddles[0])) || k.at.y > field_h + 20)
            if (c < caught.size()) caught[c++] = e;
    });
    for (std::size_t i = 0; i < c; ++i) {
        const Capsule k = s.world.get<Capsule>(caught[i]);
        s.world.destroy(caught[i]);
        if (k.at.y <= field_h + 20) {
            give(s, k.gift);
            happened(s, Happened::caught, static_cast<std::uint32_t>(k.gift), k.at);
        }
    }
    // No ball left: a life, and a new ball on the paddle; none left: over.
    bool any = false;
    s.world.each<Ball>([&](mgame::Entity, Ball&) { any = true; });
    if (!any) {
        --s.lives;
        s.combo = 0;
        happened(s, Happened::life, static_cast<std::uint32_t>(std::max(0, s.lives)), {s.paddles[0].x, s.paddles[0].y});
        if (s.lives <= 0) {
            s.over = 2;
            happened(s, Happened::over, 2, {});
        } else {
            rest(s, 0);
        }
    }
    if (s.bricks <= 0 && !s.over) {
        s.over = 1;
        happened(s, Happened::cleared, 0, {});
    }
}

std::uint64_t checksum(const State& s) noexcept {
    std::uint64_t h = s.world.checksum();
    auto bytes = [&h](const auto& v) {
        const auto* p = reinterpret_cast<const unsigned char*>(&v);
        for (std::size_t i = 0; i < sizeof(v); ++i) h = (h ^ p[i]) * 0x100000001b3ull;
    };
    for (const Paddle& p : s.paddles) bytes(p.x), bytes(p.y), bytes(p.half), bytes(p.moved), bytes(p.wide), bytes(p.active);
    bytes(s.rng), bytes(s.frame), bytes(s.score), bytes(s.combo), bytes(s.lives), bytes(s.slow), bytes(s.bricks), bytes(s.speed), bytes(s.over);
    bytes(s.events);
    for (std::uint32_t i = 0; i < s.events; ++i) bytes(s.event[i]);
    return h;
}

}  // namespace omui::breakout
