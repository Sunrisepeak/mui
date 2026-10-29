// The breakout's simulation: a launched ball breaks bricks and scores by the
// chain; a ball too fast for a sampled step still hits a thin brick; a
// paddle swung into a ball's path catches it (relative motion); a ball into
// the seam between two bricks bounces like off a face; the input of a frame
// moves the paddle that frame; capsules give their gifts; lives run out, a
// cleared field ends it; a timeline rewinds 5 seconds and replays to the same
// bytes; a whole game's checksum is the same everywhere. And the machine: a
// launch along the sweeping aim; a moving paddle's spin; switches driving
// shutters, kickers and capacitors on their circuits; a full capacitor's beam
// along its row and the chain it sets off; a core's hit-stop and the
// shutdown's wave; chains banked as sparks and lost with the ball; sparks
// paid; the magnet; the duet's finisher; a gate that comes round; the upper
// chamber; the orbit guard; a forecast that is the path the ball takes.
import std;
import mgame.input;
import mgame.physics;
import mgame.sim;
import openmui.showcase.widget_breakout.sim;
import openmui.testing;

using namespace omui::testing;
namespace bo = omui::breakout;
using mgame::Box;
using mgame::V2;

namespace {

// Eight columns, four rows of 120 × 30 bricks, the top row two hits each.
bo::Level wall_of_bricks(std::uint64_t seed = 1) {
    bo::Level l;
    l.seed = seed;
    std::uint32_t id = 0;
    for (std::uint32_t row = 0; row < 4; ++row)
        for (int col = 0; col < 8; ++col)
            l.bricks.push_back({Box{120.0 + col * 130, 100.0 + row * 40, 120, 30}, row == 0 ? bo::Kind::card : bo::Kind::button, row == 0 ? 2 : 1, id++, row});
    return l;
}

mgame::Actions launch() {
    mgame::Actions a;
    a.held = a.pressed = 1u << bo::launch_button;
    return a;
}

// A player that follows the lowest flying ball with the pointer and launches at once.
bo::Input follow(const bo::State& s, std::int64_t f) {
    bo::Input in;
    double x = bo::field_w / 2, lowest = -1;
    s.world.each<bo::Ball>([&](mgame::Entity, const bo::Ball& b) {
        if (b.on == 0 && b.at.y > lowest) lowest = b.at.y, x = b.at.x;
    });
    const double aim = x + 23.0 * static_cast<double>((f / 37) % 3 - 1);  // off-centre now and then (no libm: the same everywhere)
    in.player.pointer_x = static_cast<std::int16_t>(std::lround(std::clamp(aim, 0.0, bo::field_w) / bo::field_w * 32767));
    if (f % 30 == 0) in.player.pressed = in.player.held = 1u << bo::launch_button;
    return in;
}

std::int32_t balls(const bo::State& s) {
    std::int32_t n = 0;
    s.world.each<bo::Ball>([&](mgame::Entity, const bo::Ball&) { ++n; });
    return n;
}

// The state's ball (the first) flying from `at` with `velocity`.
void fling(bo::State& s, V2 at, V2 velocity) {
    s.world.each<bo::Ball>([&](mgame::Entity, bo::Ball& b) {
        b.on = 0;
        b.at = at;
        b.velocity = velocity;
    });
}
bo::Ball ball(const bo::State& s) {
    bo::Ball out{};
    s.world.each<bo::Ball>([&](mgame::Entity, const bo::Ball& b) { out = b; });
    return out;
}
bo::Brick* brick(bo::State& s, std::uint32_t id) {
    bo::Brick* out = nullptr;
    s.world.each<bo::Brick>([&](mgame::Entity, bo::Brick& b) {
        if (b.id == id) out = &b;
    });
    return out;
}
// Whether the step saw it happen (to `id`, when given).
bool saw(const bo::State& s, bo::Happened what, std::optional<std::uint32_t> id = {}) {
    for (std::uint32_t i = 0; i < s.events; ++i)
        if (s.event[i].what == what && (!id || s.event[i].id == *id)) return true;
    return false;
}
// Steps until it happens (at most `most`); whether it did.
bool until(bo::State& s, bo::Happened what, int most = 600, bo::Input in = {}) {
    for (int f = 0; f < most; ++f) {
        bo::step(s, in);
        if (saw(s, what)) return true;
    }
    return false;
}
// A brick out of the way: the field is not cleared.
constexpr bo::BrickSpec aside{Box{0, 0, 10, 10}, bo::Kind::button, 1, 999, 0};

// A small machine: a switch with its shutter over a core, a capacitor's row, kickers.
bo::Level machine(std::uint64_t seed = 1) {
    bo::Level l;
    l.seed = seed;
    std::uint32_t id = 0;
    l.bricks.push_back({Box{560, 120, 160, 40}, bo::Kind::core, 3, id++, 0});
    l.bricks.push_back({Box{540, 170, 200, 16}, bo::Kind::shutter, 1, id++, 1, bo::Gift::none, 1, 0});
    l.bricks.push_back({Box{300, 420, 120, 36}, bo::Kind::toggle, 1, id++, 2, bo::Gift::none, 1, 1});
    l.bricks.push_back({Box{40, 260, 140, 30}, bo::Kind::slider, 3, id++, 3});
    for (int c = 0; c < 6; ++c) l.bricks.push_back({Box{200.0 + c * 150, 260, 120, 30}, c == 3 ? bo::Kind::card : bo::Kind::button, c == 3 ? 3 : 1, id++, 3});
    l.bricks.push_back({Box{1140, 260, 100, 30}, bo::Kind::wall, 1, id++, 3});
    for (int c = 0; c < 8; ++c) l.bricks.push_back({Box{120.0 + c * 135, 60, 110, 30}, bo::Kind::button, 1, id++, 4});
    l.bricks.push_back({Box{80, 560, 80, 20}, bo::Kind::kicker, 1, id++, 5, bo::Gift::none, 2, 1});
    l.bricks.push_back({Box{1120, 560, 80, 20}, bo::Kind::kicker, 1, id++, 5, bo::Gift::none, 2, 0});
    l.bricks.push_back({Box{900, 420, 120, 36}, bo::Kind::toggle, 1, id++, 2, bo::Gift::none, 2, 1});
    return l;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"breakout.sim"};

    s.test("launched, a ball breaks bricks; the top row takes two; the score grows with the chain", [] {
        bo::State st = bo::start(wall_of_bricks());
        check(st.bricks == 32 && balls(st) == 1);
        bo::step(st, {launch(), {}});
        check(std::ranges::any_of(std::span(st.event).first(st.events), [](const bo::Event& e) { return e.what == bo::Happened::launched; }));
        int broke = 0, hits = 0;
        for (std::int64_t f = 1; f < 3000 && !st.over; ++f) {
            bo::step(st, follow(st, f));
            for (std::uint32_t i = 0; i < st.events; ++i) broke += st.event[i].what == bo::Happened::broke, hits += st.event[i].what == bo::Happened::hit;
        }
        check(broke > 5 && hits > broke && st.score >= 10 * broke, std::format("{} broken from {} hits, score {}", broke, hits, st.score));
    });

    s.test("a ball too fast for a sampled step still hits a thin brick", [] {
        bo::Level l;
        l.speed = 40;  // 40 pixels a step: it would skip a 4-pixel brick sampled
        l.bricks.push_back({Box{340, 300, 600, 4}, bo::Kind::button, 1, 7, 0});  // across the launch's path
        bo::State st = bo::start(l);
        bo::step(st, {launch(), {}});
        bool hit = false;
        for (int f = 0; f < 60 && !hit; ++f) {
            bo::step(st, {});
            for (std::uint32_t i = 0; i < st.events; ++i) hit = hit || (st.event[i].what == bo::Happened::broke && st.event[i].id == 7);
        }
        check(hit, "met mid-step");
    });

    s.test("a paddle swung into the ball's path catches it; the input of a frame moves the paddle that frame", [] {
        bo::Level l;
        l.bricks.push_back({Box{0, 0, 10, 10}, bo::Kind::button, 1, 0, 0});  // out of the way: the field is not cleared
        bo::State st = bo::start(l);
        // A ball falling at the bottom paddle's height, 120 pixels to its right; the paddle swings at it with the pointer.
        st.world.each<bo::Ball>([](mgame::Entity, bo::Ball& b) {
            b.on = 0;
            b.at = {760, 640};
            b.velocity = {0, 9};
        });
        bo::Input in;
        in.player.pointer_x = static_cast<std::int16_t>(std::lround(760.0 / bo::field_w * 32767));
        const double before = st.paddles[0].x;
        bo::step(st, in);
        check(st.paddles[0].x != before, "moved on the frame it was asked");
        bool caught = false;
        for (int f = 0; f < 20 && !caught; ++f) {
            for (std::uint32_t i = 0; i < st.events; ++i) caught = caught || st.event[i].what == bo::Happened::paddle;
            bo::step(st, in);
        }
        check(caught && balls(st) == 1, "caught, not passed through");
    });

    s.test("a ball into the seam between two bricks bounces straight back, as off a face", [] {
        bo::Level l;
        l.bricks.push_back({Box{500, 200, 100, 30}, bo::Kind::wall, 1, 1, 0});
        l.bricks.push_back({Box{600, 200, 100, 30}, bo::Kind::wall, 1, 2, 0});
        l.bricks.push_back({Box{0, 0, 10, 10}, bo::Kind::button, 1, 3, 0});  // out of the way: the field is not cleared
        bo::State st = bo::start(l);
        st.world.each<bo::Ball>([](mgame::Entity, bo::Ball& b) {
            b.on = 0;
            b.at = {600, 400};
            b.velocity = {0, -6};
        });
        V2 after{};
        for (int f = 0; f < 60; ++f) {
            bo::step(st, {});
            st.world.each<bo::Ball>([&](mgame::Entity, const bo::Ball& b) { after = b.velocity; });
            if (after.y > 0) break;
        }
        check(after.x == 0 && after.y == 6, std::format("({}, {})", after.x, after.y));
    });

    s.test("a gate is a wall but on its frames: a ball through it then, bounced off it before and after", [] {
        // A ball rising at a gate across the field; the gate open on frames [from, to).
        auto rise = [](std::int64_t from, std::int64_t to) {
            bo::Level l;
            l.gate_from = from;
            l.gate_to = to;
            l.bricks.push_back({Box{0, 300, bo::field_w, 20}, bo::Kind::gate, 1, 1, 0});
            l.bricks.push_back({Box{0, 0, 10, 10}, bo::Kind::button, 1, 2, 0});  // out of the way: the field is not cleared
            bo::State st = bo::start(l);
            check_eq(st.bricks, std::int32_t{1});  // a gate is not a brick to break
            st.world.each<bo::Ball>([](mgame::Entity, bo::Ball& b) {
                b.on = 0;
                b.at = {640, 400};
                b.velocity = {0, -6};
            });
            double highest = 1000;
            for (int f = 0; f < 40; ++f) {
                bo::step(st, {});
                st.world.each<bo::Ball>([&](mgame::Entity, const bo::Ball& b) { highest = std::min(highest, b.at.y); });
            }
            return highest;
        };
        check(rise(0, 0) > 300, "never open: bounced");
        check(rise(1, 60) < 300, "open: through");
        check(rise(1, 5) > 300, "closed again before the ball came: bounced");
        check(rise(100, 200) > 300, "not open yet: bounced");
    });

    s.test("a taller field: the paddle at its bottom, the camera out to keep the highest ball in sight, or all of it while looking", [] {
        bo::Level l;
        l.height = 4 * bo::field_h;
        l.bricks.push_back({Box{0, 0, 10, 10}, bo::Kind::button, 1, 0, 0});  // out of the way: the field is not cleared
        bo::State st = bo::start(l);
        check_eq(st.paddles[0].y, 4 * bo::field_h - 30);
        check_eq(st.zoom, 1.0);
        st.world.each<bo::Ball>([](mgame::Entity, bo::Ball& b) {
            b.on = 0;
            b.at = {640, 1000};
            b.velocity = {0, -0.001};  // standing, near enough: high up the field
        });
        for (int f = 0; f < 120; ++f) bo::step(st, {});
        const double fit = bo::field_h / (4 * bo::field_h - (1000 - 32));
        check(std::abs(st.zoom - fit) < 1e-3, std::format("zoom {} for {}", st.zoom, fit));
        bo::Input look;
        look.player.held = 1u << bo::look_button;
        for (int f = 0; f < 120; ++f) bo::step(st, look);
        check(std::abs(st.zoom - bo::widest(st)) < 1e-3 && bo::widest(st) == 0.25, std::format("zoom {}", st.zoom));
    });

    s.test("capsules give: wide, three balls for one, slow motion, piercing, the magnet", [] {
        bo::State st = bo::start(wall_of_bricks());
        bo::step(st, {launch(), {}});
        bo::step(st, {});
        const auto add = [&](bo::Gift g) {
            const mgame::Entity e = st.world.create();
            st.world.add(e, bo::Capsule{{st.paddles[0].x, st.paddles[0].y - 12}, g, 0});
            bo::step(st, {});
        };
        add(bo::Gift::wide);
        check(st.paddles[0].wide > 0, "wide");
        add(bo::Gift::multi);
        check_eq(balls(st), std::int32_t{3});
        add(bo::Gift::slow);
        check(st.slow > 0, "slow");
        add(bo::Gift::pierce);
        bool piercing = true;
        st.world.each<bo::Ball>([&](mgame::Entity, const bo::Ball& b) { piercing = piercing && b.pierce > 0; });
        check(piercing, "piercing");
        add(bo::Gift::magnet);
        check_eq(st.paddles[0].magnet, st.rules.magnet_catches);
    });

    s.test("rules: a level's, bounded; changed by a step's input from that step on, kept in the state so a rewind brings the old back; a brick's own gift always drops", [] {
        bo::Level l = wall_of_bricks();
        l.rules.wide_half = 1000;  // out of bounds: held at the most
        l.bricks[24].gift = bo::Gift::pierce;  // the lowest row's first
        bo::State st = bo::start(l);
        check_eq(st.rules.wide_half, std::int32_t{300});
        mgame::Timeline<bo::Game> t(st, 30 * bo::steps_per_second, 60);
        for (int f = 0; f < 200; ++f) t.step({});
        bo::Input in;
        in.rules = st.rules;
        in.rules.multi_balls = 4;
        in.rules.slow_percent = 25;
        in.new_rules = 1;
        t.step(in);
        check(t.state().rules.multi_balls == 4 && t.state().rules.slow_percent == 25, "from that step");
        for (int f = 0; f < 100; ++f) t.step({});
        check(t.seek(150));
        check_eq(t.state().rules.multi_balls, std::int32_t{2});  // before the change: the old rules
        check(t.seek(300));
        check_eq(t.state().rules.multi_balls, std::int32_t{4});  // replayed: the input brought them again, no script ran
        // Four extra balls a flying ball; the brick with a gift drops it when broken.
        bo::State m = bo::start(l);
        m.rules.multi_balls = 4;
        bo::step(m, {launch(), {}});
        bo::step(m, {});
        const mgame::Entity e = m.world.create();
        m.world.add(e, bo::Capsule{{m.paddles[0].x, m.paddles[0].y - 12}, bo::Gift::multi, 0});
        bo::step(m, {});
        check_eq(balls(m), std::int32_t{5});
        bo::State g = bo::start(l);
        bool dropped = false;
        g.world.each<bo::Ball>([](mgame::Entity, bo::Ball& b) {
            b.on = 0;
            b.at = {180, 300};
            b.velocity = {0, -6};
        });
        for (int f = 0; f < 60 && !dropped; ++f) {
            bo::step(g, {});
            for (std::uint32_t i = 0; i < g.events; ++i) dropped = dropped || (g.event[i].what == bo::Happened::gift && g.event[i].id == static_cast<std::uint32_t>(bo::Gift::pierce));
        }
        check(dropped, "its own gift");
    });

    s.test("lives run out with nobody at the paddle; a cleared field ends it", [] {
        bo::State st = bo::start(wall_of_bricks());
        for (int f = 0; f < 20000 && !st.over; ++f) {
            bo::Input in;
            if (f % 60 == 0) in.player = launch();
            in.player.axes[0] = f % 4000 < 2000 ? -32767 : 32767;  // hiding in one corner, then the other
            bo::step(st, in);
        }
        check(st.over == 2 && st.lives == 0, std::format("over {}, lives {}", st.over, st.lives));
        bo::Level one;  // launched straight up (the aim starts in the middle) into a brick
        one.bricks.push_back({Box{640 - 60, 300, 120, 30}, bo::Kind::button, 1, 0, 0});
        bo::State win = bo::start(one);
        bo::step(win, {launch(), {}});
        for (int f = 0; f < 600 && !win.over; ++f) bo::step(win, follow(win, f));
        check(win.over == 1 && win.bricks == 0, "cleared");
    });

    s.test("a timeline rewinds five seconds and replays to the same bytes; a new branch goes elsewhere", [] {
        mgame::Timeline<bo::Game> t(bo::start(wall_of_bricks(3)), 30 * bo::steps_per_second, 60);
        std::vector<std::uint64_t> sums{bo::checksum(t.state())};
        for (std::int64_t f = 0; f < 3000; ++f) {
            t.step(follow(t.state(), f));
            sums.push_back(bo::checksum(t.state()));
        }
        check(t.seek(3000 - 600), "five seconds back");
        check_eq(bo::checksum(t.state()), sums[2400]);
        for (std::int64_t f = 2400; f < 3000; ++f) t.step(t.input(f));  // the same inputs again
        check_eq(bo::checksum(t.state()), sums[3000]);
        check(t.seek(2400));
        for (std::int64_t f = 2400; f < 3000; ++f) {
            bo::Input in;
            in.player.pointer_x = 0;  // other play: the paddle parked at the left
            t.step(in);
        }
        check(bo::checksum(t.state()) != sums[3000], "another future");
    });

    s.test("the launch goes along the aim: it sweeps while the ball waits, or holds where an agent puts it", [] {
        bo::Level l;
        l.bricks.push_back(aside);
        bo::State st = bo::start(l);
        check_eq(st.aim, 0.0);  // it starts straight up
        for (int f = 0; f < 30; ++f) bo::step(st, {});
        check(st.aim > 0.5 && st.aim < bo::aim_most, std::format("swept to {}", st.aim));
        bo::Input hold;
        hold.aim_set = 1;
        hold.aim = -0.5;
        bo::step(st, hold);
        for (int f = 0; f < 50; ++f) bo::step(st, {});
        check_eq(st.aim, -0.5);
        bo::step(st, {launch(), {}});
        check(ball(st).velocity == mgame::normalized(V2{-0.5, -1}) * st.speed, "launched along it");
        check_eq(st.aim_held, std::int32_t{0});
        // The forecast: where a launch along the aim turns, off the walls, is where the ball turns.
        bo::Level open;
        open.bricks.push_back(aside);
        bo::State a = bo::start(open);
        bo::Input steep;
        steep.aim_set = 1;
        steep.aim = 1.2;
        bo::step(a, steep);
        std::array<V2, 4> path{};
        const auto n = bo::trace(a, ball(a).at, bo::aim_direction(a), ball(a).radius, path);
        check(n >= 3, std::format("{} points", n));
        bo::step(a, {launch(), {}});
        std::vector<V2> walls;
        for (int f = 0; f < 400 && walls.size() < 2; ++f) {
            bo::step(a, {});
            for (std::uint32_t i = 0; i < a.events; ++i)
                if (a.event[i].what == bo::Happened::wall) walls.push_back(a.event[i].at);
        }
        check(walls.size() == 2, "two walls met");
        for (std::size_t k = 0; k < 2 && k < walls.size(); ++k)
            check(mgame::length(walls[k] - path[k + 1]) < 1e-6, std::format("turn {}: ({}, {}) forecast ({}, {})", k, walls[k].x, walls[k].y, path[k + 1].x, path[k + 1].y));
    });

    s.test("a paddle moving as it returns the ball gives it spin: its path curves the way the paddle went, and the spin dies away", [] {
        auto returned = [](double axis) {
            bo::Level l;
            l.bricks.push_back(aside);
            bo::State st = bo::start(l);
            fling(st, {640, 668}, {0, 6});
            bo::Input in;
            in.player.axes[0] = static_cast<std::int16_t>(axis * 32767);
            check(until(st, bo::Happened::paddle, 10, in), "returned");
            return st;
        };
        bo::State still = returned(0), moving = returned(1);
        check_eq(ball(still).spin, 0.0);
        check(ball(moving).spin > 0, std::format("spin {}", ball(moving).spin));
        const V2 v0 = ball(moving).velocity;
        for (int f = 0; f < 40; ++f) bo::step(moving, {});
        const V2 v1 = ball(moving).velocity;
        check(v0.x * v1.y - v0.y * v1.x > 0, "turned clockwise: toward the right it was going");
        check(std::abs(mgame::length(v1) - moving.speed) < 1e-9, "at its speed");
        check(ball(moving).spin < 0.6 * 0.005, std::format("dying away: {}", ball(moving).spin));
        // The paddle's motion also sends it further across.
        check(ball(still).velocity.x < v0.x, "further right");
    });

    s.test("a switch flips with every hit and moves its circuit: a shutter opens (a ball goes through), a kicker turns; hit again, back", [] {
        bo::Level l;
        l.bricks.push_back({Box{600, 200, 80, 30}, bo::Kind::toggle, 1, 1, 0, bo::Gift::none, 1, 1});
        l.bricks.push_back({Box{0, 100, 1280, 20}, bo::Kind::shutter, 1, 2, 1, bo::Gift::none, 1, 0});
        l.bricks.push_back({Box{100, 500, 60, 20}, bo::Kind::kicker, 1, 3, 2, bo::Gift::none, 1, 0});
        l.bricks.push_back(aside);
        auto highest = [](bo::State st, double x) {
            fling(st, {x, 400}, {0, -6});
            double top = 1000;
            for (int f = 0; f < 80; ++f) {
                bo::step(st, {});
                top = std::min(top, ball(st).at.y);
            }
            return top;
        };
        bo::State st = bo::start(l);
        check(highest(st, 300) > 120, "closed: bounced under it");
        fling(st, {640, 400}, {0, -6});
        check(until(st, bo::Happened::flipped, 60), "the switch hit");
        check(brick(st, 1)->on == 0 && brick(st, 2)->on == 1 && brick(st, 3)->on == 1, "off; the shutter open, the kicker turned");
        std::int32_t wired = 0;
        for (std::uint32_t i = 0; i < st.events; ++i) wired += st.event[i].what == bo::Happened::wired;
        check_eq(wired, std::int32_t{2});
        check(highest(st, 300) < 100, "open: through it");
        fling(st, {640, 400}, {0, -6});
        check(until(st, bo::Happened::flipped, 60), "hit again");
        check(brick(st, 1)->on == 1 && brick(st, 2)->on == 0, "on; closed again");
    });

    s.test("a capacitor charges a step a hit; full, it fires along its row to the nearest wall each way: bricks take its charge, a switch flips, another capacitor charges — set up, it fires too", [] {
        bo::Level l;
        const double y = 300;
        l.bricks.push_back({Box{0, y, 40, 30}, bo::Kind::wall, 1, 1, 0});
        l.bricks.push_back({Box{100, y, 100, 30}, bo::Kind::card, 3, 2, 0});    // three hits' worth
        l.bricks.push_back({Box{500, y, 100, 30}, bo::Kind::slider, 3, 3, 0});  // the capacitor
        l.bricks.push_back({Box{700, y, 60, 30}, bo::Kind::toggle, 1, 4, 0, bo::Gift::none, 0, 1});
        l.bricks.push_back({Box{800, y, 100, 30}, bo::Kind::slider, 3, 5, 0});  // another, set up
        l.bricks.push_back({Box{1000, y, 40, 30}, bo::Kind::wall, 1, 6, 0});
        l.bricks.push_back({Box{1100, y, 100, 30}, bo::Kind::button, 1, 7, 0});  // beyond the wall
        l.bricks.push_back({Box{100, 500, 100, 30}, bo::Kind::button, 1, 8, 1});  // off the row
        bo::State st = bo::start(l);
        brick(st, 3)->charge = 2;
        brick(st, 5)->charge = 2;
        fling(st, {550, 420}, {0, -6});
        check(until(st, bo::Happened::charged, 60), "charged");
        check(saw(st, bo::Happened::armed, 3) && brick(st, 3)->charge == 3, "full: armed");
        check(until(st, bo::Happened::beam, 20), "fired");
        const bo::Event* b = nullptr;
        for (std::uint32_t i = 0; i < st.events; ++i)
            if (st.event[i].what == bo::Happened::beam) b = &st.event[i];
        check(b && b->id == 3 && b->at.x == 40 && b->to.x == 1000 && b->n == 3, "from wall to wall, three strong");
        check(!brick(st, 2), "the card broken");
        check(brick(st, 4)->on == 0, "the switch flipped");
        check(brick(st, 5)->charge == 3 && brick(st, 5)->fuse > 0, "the other charged and armed");
        check(brick(st, 7) && brick(st, 8), "beyond the wall and off the row: untouched");
        check_eq(brick(st, 3)->charge, std::int32_t{0});
        check(st.freeze > 0, "a hit-stop");
        check(until(st, bo::Happened::beam, 30), "the other fired");
        check(brick(st, 3)->charge == 0 && brick(st, 3)->fuse < 0, "the first cooling: not charged back (they never fire each other for ever)");
    });

    s.test("a switch discharges a capacitor on its circuit early, as strong as its charge", [] {
        bo::Level l;
        l.bricks.push_back({Box{600, 200, 80, 30}, bo::Kind::toggle, 1, 1, 0, bo::Gift::none, 2, 1});
        l.bricks.push_back({Box{100, 400, 100, 30}, bo::Kind::slider, 3, 2, 1, bo::Gift::none, 2});
        l.bricks.push_back({Box{400, 400, 100, 30}, bo::Kind::card, 2, 3, 1});
        bo::State st = bo::start(l);
        brick(st, 2)->charge = 1;
        fling(st, {640, 400}, {0, -6});
        check(until(st, bo::Happened::flipped, 60), "the switch hit");
        check(until(st, bo::Happened::beam, 20), "discharged");
        check(brick(st, 3) && brick(st, 3)->durability == 1, "one strong");
    });

    s.test("a kicker sends the ball where it points", [] {
        bo::Level l;
        l.bricks.push_back({Box{600, 500, 80, 20}, bo::Kind::kicker, 1, 1, 0, bo::Gift::none, 0, 1});
        l.bricks.push_back(aside);
        bo::State st = bo::start(l);
        fling(st, {640, 400}, {0.5, 6});
        check(until(st, bo::Happened::kicked, 60), "kicked");
        check(ball(st).velocity == bo::kick_direction(*brick(st, 1)) * st.speed, "along its arrow");
        check(bo::kick_direction(*brick(st, 1)).x > 0 && bo::kick_direction(*brick(st, 1)).y < 0, "up and to the right");
    });

    s.test("a core: each hit a short hit-stop; broken, the room shuts down — a wave from it breaks every brick left, then it is cleared", [] {
        bo::Level l;
        l.bricks.push_back({Box{600, 200, 80, 30}, bo::Kind::core, 2, 1, 0});
        for (int c = 0; c < 8; ++c) l.bricks.push_back({Box{60.0 + c * 150, 80, 100, 30}, bo::Kind::button, 1, static_cast<std::uint32_t>(10 + c), 1});
        bo::State st = bo::start(l);
        check(st.cores == 1 && st.bricks == 9, "a core and eight bricks");
        fling(st, {640, 400}, {0, -6});
        check(until(st, bo::Happened::core_hit, 60), "hit");
        check(st.freeze > 0, "a hit-stop");
        const V2 held = ball(st).at;
        bo::step(st, {});
        check(ball(st).at == held, "nothing moves in it");
        fling(st, {640, 400}, {0, -6});
        check(until(st, bo::Happened::shutdown, 120), "broken");
        const auto score = st.score;
        check(st.cores == 0 && st.freeze >= 29 && !st.over, "a long hit-stop, then the wave");
        check(until(st, bo::Happened::cleared, 200), "cleared");
        check(st.over == 1 && st.bricks == 0, "every brick gone");
        check_eq(st.score, score + 8 * 10);
    });

    s.test("a chain is banked as sparks when the ball comes back to the paddle; the last ball lost loses it; a step's input spends them", [] {
        bo::Level l;
        l.bricks.push_back(aside);
        bo::State st = bo::start(l);
        fling(st, {640, 600}, {0, 6});
        st.combo = 5;
        check(until(st, bo::Happened::banked, 30), "banked");
        check(st.sparks == 50 && st.combo == 0 && st.best == 5, std::format("{} sparks", st.sparks));
        fling(st, {640, 600}, {0, 6});
        st.combo = 2;
        check(until(st, bo::Happened::paddle, 30) && !saw(st, bo::Happened::banked), "a chain of two: not banked");
        fling(st, {100, 600}, {0, 6});
        st.combo = 4;
        check(until(st, bo::Happened::chain_lost, 60), "lost");
        check_eq(st.sparks, std::int32_t{50});
        bo::Input pay;
        pay.pay = 30;
        bo::step(st, pay);
        check_eq(st.sparks, std::int32_t{20});
        fling(st, {st.paddles[0].x, 600}, {0, 6});
        st.combo = 40;
        check(until(st, bo::Happened::banked, 30) && st.sparks == bo::spark_cap, "at most the cap");
    });

    s.test("the magnet holds a returned ball on the paddle, where it met, for another aim", [] {
        bo::Level l;
        l.bricks.push_back(aside);
        bo::State st = bo::start(l);
        st.paddles[0].magnet = 2;
        fling(st, {660, 600}, {0, 6});
        check(until(st, bo::Happened::magnet, 30), "held");
        check(ball(st).on == 1 && std::abs(ball(st).dx - 20) < 1e-9 && st.paddles[0].magnet == 1, std::format("at {}", ball(st).dx));
        bo::Input left;
        left.player.axes[0] = -32767;
        for (int f = 0; f < 10; ++f) bo::step(st, left);
        check(ball(st).at.x == st.paddles[0].x + 20, "riding it");
        bo::step(st, {launch(), {}});
        check(ball(st).on == 0 && ball(st).velocity == bo::aim_direction(st) * st.speed, "launched along the aim");
    });

    s.test("two paddles: alternate returns ready the finisher; the bottom paddle's next return fires a beam up its column", [] {
        bo::Level l;
        l.second_paddle = true;
        l.bricks.push_back({Box{610, 300, 60, 30}, bo::Kind::card, 2, 1, 0});   // in the column
        l.bricks.push_back({Box{900, 300, 60, 30}, bo::Kind::button, 1, 2, 0});  // not
        bo::State st = bo::start(l);
        st.duet = 5;
        st.last_paddle = 0;
        fling(st, {st.paddles[1].x, 100}, {0, -6});
        check(until(st, bo::Happened::finisher, 30) && st.finisher == 1 && st.duet == 6, "ready");
        fling(st, {640, 600}, {0, 6});
        check(until(st, bo::Happened::beam, 40), "fired");
        check(!brick(st, 1) && brick(st, 2), "the column's brick broken, the other not");
        check(st.finisher == 0 && st.duet == 0, "spent");
    });

    s.test("a gate comes round: open its frames of every period", [] {
        bo::Level l;
        l.gate_from = 50;
        l.gate_period = 100;
        l.gate_open = 20;
        l.bricks.push_back(aside);
        bo::State st = bo::start(l);
        std::vector<std::int64_t> open;
        for (int f = 0; f < 300; ++f) {
            bo::step(st, {});
            if (bo::gates_open(st)) open.push_back(st.frame);
        }
        check(open.size() == 60 && open.front() == 50 && open[19] == 69 && open[20] == 150, std::format("{} frames open", open.size()));
    });

    s.test("a ball into a taller room's upper chamber: the breach — the first a hit-stop and sparks — and out again", [] {
        bo::Level l;
        l.height = 2 * bo::field_h;
        l.chamber = 700;
        l.bricks.push_back(aside);
        bo::State st = bo::start(l);
        fling(st, {640, 800}, {0, -6});
        check(until(st, bo::Happened::breach, 40), "in");
        check(st.freeze > 0 && st.sparks == 50 && ball(st).above == 1, "the first time");
        fling(st, {640, 690}, {0, 6});
        for (int f = 0; f < 30; ++f) bo::step(st, {});
        check_eq(ball(st).above, std::uint32_t{0});
        fling(st, {640, 800}, {0, -6});
        check(until(st, bo::Happened::breach, 60) && st.sparks == 50, "again: no more sparks");
    });

    s.test("never along a row for ever: a bounce keeps a ball climbing or falling; nothing moving six seconds, the next wall turns it", [] {
        bo::Level l;
        l.bricks.push_back(aside);
        bo::State st = bo::start(l);
        fling(st, {640, 300}, {6, 0.1});
        check(until(st, bo::Happened::wall, 200), "a wall");
        const V2 v = ball(st).velocity;
        check(std::abs(v.y) >= 0.28 * mgame::length(v) - 1e-9 && v.x < 0, std::format("({}, {})", v.x, v.y));
        bo::State again = bo::start(l);
        fling(again, {640, 300}, {4, -4});
        again.idle = 6 * bo::steps_per_second + 1;
        check(until(again, bo::Happened::wall, 200), "a wall");
        const V2 w = ball(again).velocity;
        check(w.x != -4 || w.y != -4, "turned");
        check_eq(again.idle, std::int32_t{0});
    });

    s.test("a machine played by a script: switches, shutters, kickers, a capacitor, a core — its checksum is the same everywhere", [] {
        bo::State st = bo::start(machine(5));
        std::uint64_t all = 0xcbf29ce484222325ull;
        std::int64_t frames = 0;
        std::int32_t flips = 0, beams = 0, kicks = 0;
        for (std::int64_t f = 0; f < 12000 && !st.over; ++f, ++frames) {
            bo::step(st, follow(st, f));
            for (std::uint32_t i = 0; i < st.events; ++i) {
                flips += st.event[i].what == bo::Happened::flipped;
                beams += st.event[i].what == bo::Happened::beam;
                kicks += st.event[i].what == bo::Happened::kicked;
            }
            all = (all ^ bo::checksum(st)) * 0x100000001b3ull;
        }
        std::println("    machine {:016x} ({} frames, score {}, over {}, {} flips, {} beams, {} kicks, {} sparks)", all, frames, st.score, st.over, flips, beams, kicks, st.sparks);
        check(flips > 0 && kicks > 0, "the machine moved");
        check_eq(all, std::uint64_t{0x1b119ff7016dfad3});  // GCC, LLVM and wasm
    });

    s.test("a whole game played by a script: its checksum is the same everywhere", [] {
        bo::State st = bo::start(wall_of_bricks(9));
        std::uint64_t all = 0xcbf29ce484222325ull;
        std::int64_t frames = 0;
        for (std::int64_t f = 0; f < 12000 && !st.over; ++f, ++frames) {
            bo::step(st, follow(st, f));
            all = (all ^ bo::checksum(st)) * 0x100000001b3ull;
        }
        std::println("    game {:016x} ({} frames, score {}, over {})", all, frames, st.score, st.over);
        check_eq(all, std::uint64_t{0xff443bf0c65e7a3c});  // GCC, LLVM and wasm
    });

    return s.run(argc, argv);
}
