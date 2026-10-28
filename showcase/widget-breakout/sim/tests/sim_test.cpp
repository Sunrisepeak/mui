// The breakout's simulation: a launched ball breaks bricks and scores by the
// combo; a ball too fast for a sampled step still hits a thin brick; a
// paddle swung into a ball's path catches it (relative motion); a ball into
// the seam between two bricks bounces like off a face; the input of a frame
// moves the paddle that frame; capsules give their gifts; lives run out, a
// cleared field ends it; a timeline rewinds 5 seconds and replays to the same
// bytes; a whole game's checksum is the same everywhere.
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

// Eight columns, four rows of 120 × 30 bricks, the top row toggles.
bo::Level wall_of_bricks(std::uint64_t seed = 1) {
    bo::Level l;
    l.seed = seed;
    std::uint32_t id = 0;
    for (std::uint32_t row = 0; row < 4; ++row)
        for (int col = 0; col < 8; ++col)
            l.bricks.push_back({Box{120.0 + col * 130, 100.0 + row * 40, 120, 30}, row == 0 ? bo::Kind::toggle : bo::Kind::button, row == 0 ? 2 : 1, id++, row});
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

}  // namespace

int main(int argc, char** argv) {
    Suite s{"breakout.sim"};

    s.test("launched, a ball breaks bricks; a toggle takes two; the score grows with the combo", [] {
        bo::State st = bo::start(wall_of_bricks());
        check(st.bricks == 32 && balls(st) == 1);
        bo::step(st, {launch(), {}});
        check(std::ranges::any_of(std::span(st.event).first(st.events), [](const bo::Event& e) { return e.what == bo::Happened::launched; }));
        int broke = 0, hits = 0;
        for (std::int64_t f = 1; f < 3000 && !st.over; ++f) {
            bo::step(st, follow(st, f));
            for (std::uint32_t i = 0; i < st.events; ++i) broke += st.event[i].what == bo::Happened::broke, hits += st.event[i].what == bo::Happened::hit;
        }
        check(broke > 5 && hits > broke && st.score > 10 * broke, std::format("{} broken from {} hits, score {}", broke, hits, st.score));
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

    s.test("capsules give: wide, three balls for one, slow motion, piercing", [] {
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
        for (int f = 0; f < 5000 && !st.over; ++f) {
            bo::Input in;
            if (f % 60 == 0) in.player = launch();
            in.player.axes[0] = -32767;  // hiding in the left corner
            bo::step(st, in);
        }
        check(st.over == 2 && st.lives == 0, std::format("over {}, lives {}", st.over, st.lives));
        bo::Level one;
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

    s.test("a whole game played by a script: its checksum is the same everywhere", [] {
        bo::State st = bo::start(wall_of_bricks(9));
        std::uint64_t all = 0xcbf29ce484222325ull;
        std::int64_t frames = 0;
        for (std::int64_t f = 0; f < 12000 && !st.over; ++f, ++frames) {
            bo::step(st, follow(st, f));
            all = (all ^ bo::checksum(st)) * 0x100000001b3ull;
        }
        std::println("    game {:016x} ({} frames, score {}, over {})", all, frames, st.score, st.over);
        check_eq(all, std::uint64_t{0x998a082a06e7bfe8});  // GCC, LLVM and wasm
    });

    return s.run(argc, argv);
}
