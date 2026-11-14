// The breakout as a player and an agent meet it: a room's bricks are real
// components placed by openmui's layout and read into the simulation where
// they stand; 120 steps a second on the runtime's clock (exactly,
// headless); an agent plays it through the field's actions, and every
// brick changes for real (a switch flips, a shutter opens, a capacitor's
// slider fills and empties, a broken one goes from the screen); a rewind
// costs sparks, puts every brick back and cannot be farmed; the inspector
// shows what the screen does not; the canvas room, the dark room, the
// copilot; a run from room to room with a contraption chosen between, its
// casing opened and its script reloaded in three languages; frames and
// sound pinned. And bots that check the rooms: each can be cleared, no
// drain is unavoidable, nothing orbits for ever, and understanding the
// machine pays.
import std;
import omui;
import omui.showcase.widget_breakout;
import mlibs.testing;
import mcpplibs.mtest;
import omui.ui.canvas;
import xengine.audio;

using namespace mlibs::testing;
using namespace mcpplibs::mtest;
using namespace omui;
namespace bo = omui::breakout;

namespace {

// ── bots: deterministic players of the simulation alone ──

// Where the ball soonest down meets the bottom paddle's face, and in how many steps: the forecast off the walls and bricks as they stand.
struct Landing {
    double x = 0, steps = 1e9;
    bool   any = false;
};
Landing landing(const bo::State& s) {
    const double line = s.paddles[0].y - 8 - 8;
    Landing best;
    s.world.each<bo::Ball>([&](xengine::Entity, const bo::Ball& b) {
        if (b.on != 0) return;
        std::array<xengine::dynamics::V2, 16> pts{};
        const std::size_t n = bo::trace(s, b.at, b.velocity, b.radius, pts, 5000);
        double travelled = 0;
        const double speed = xengine::dynamics::length(b.velocity);
        for (std::size_t k = 1; k < n; ++k) {
            const xengine::dynamics::V2 a = pts[k - 1], z = pts[k];
            const double len = xengine::dynamics::length(z - a);
            if (a.y <= line && z.y >= line && z.y > a.y) {
                const double t = (line - a.y) / (z.y - a.y);
                const double steps = (travelled + len * t) / std::max(speed, 1e-9);
                if (steps < best.steps) best = {a.x + (z.x - a.x) * t, steps, true};
                break;
            }
            travelled += len;
        }
    });
    return best;
}

// The top paddle's stand-in (room 5): under the rising ball's meeting with its line, returning it toward the core.
void guard(const bo::State& s, bo::Input& in) {
    if (!s.paddles[1].active) return;
    double best = 1e9, to = s.paddles[1].x;
    s.world.each<bo::Ball>([&](xengine::Entity, const bo::Ball& b) {
        if (b.on) return;
        if (auto x = bo::Copilot::meets(b.at.x, b.at.y, b.velocity.x, b.velocity.y, 46, bo::field_w, 8)) {
            const double steps = (b.at.y - 46) / -b.velocity.y;
            if (steps < best) best = steps, to = *x;
        }
    });
    in.second.pointer_x = static_cast<std::int16_t>(std::lround(std::clamp(to, 0.0, bo::field_w) / bo::field_w * 32767));
}

// Catches: the paddle where the ball comes down, met `offset` (−1 … 1 of its half) off its middle; a waiting ball launched at once.
bo::Input catcher(const bo::State& s, double offset) {
    bo::Input in;
    const Landing l = landing(s);
    const double x = l.any ? l.x - offset * s.paddles[0].half : s.paddles[0].x;
    in.player.pointer_x = static_cast<std::int16_t>(std::lround(std::clamp(x, 0.0, bo::field_w) / bo::field_w * 32767));
    bool waiting = false;
    s.world.each<bo::Ball>([&](xengine::Entity, const bo::Ball& b) { waiting = waiting || b.on == 1; });
    if (waiting && s.frame % 20 == 0) in.player.pressed = in.player.held = 1u << bo::launch_button;
    guard(s, in);
    return in;
}

// Only catches: dead centre but for a little sway (so it does not return the ball straight up for ever).
bo::Input catch_only(const bo::State& s) { return catcher(s, 0.25 * static_cast<double>((s.frame / 97) % 3 - 1)); }

std::int32_t core_hp(const bo::State& s) {
    std::int32_t hp = 0;
    s.world.each<bo::Brick>([&](xengine::Entity, const bo::Brick& b) { hp += b.kind == bo::Kind::core ? b.durability : 0; });
    return hp;
}
std::int32_t open_shutters(const bo::State& s) {
    std::int32_t n = 0;
    s.world.each<bo::Brick>([&](xengine::Entity, const bo::Brick& b) { n += b.kind == bo::Kind::shutter && b.on; });
    return n;
}
std::int32_t charge(const bo::State& s) {
    std::int32_t n = 0;
    s.world.each<bo::Brick>([&](xengine::Entity, const bo::Brick& b) { n += b.kind == bo::Kind::slider ? b.charge : 0; });
    return n;
}

// Understands the machine: before each return (and each launch) it plays a few on in its head — the paddle met at
// seven places (seven aims) — and takes the one that hurts the core most, opens most, charges most, banks most.
struct Planner {
    double       offset = 0;
    std::int64_t until = -1;  // the frame its plan holds to

    static double worth(const bo::State& s, const bo::State& from) {
        double w = 0;
        if (s.over == 1) w += 1e7 - static_cast<double>(s.frame);
        w -= 1e6 * (from.lives - s.lives);
        w += 3000.0 * (core_hp(from) - core_hp(s));
        w += 400.0 * (open_shutters(s) - open_shutters(from));
        w += 120.0 * (charge(s) - charge(from));
        w += 15.0 * (s.sparks - from.sparks);
        w += 20.0 * (from.bricks - s.bricks);
        return w;
    }
    static double rollout(bo::State s, std::optional<double> aim, double first, int steps) {
        const bo::State from = s;
        bool returned = false;
        double breaches = 0;
        for (int k = 0; k < steps && !s.over; ++k) {
            bo::Input in = catcher(s, returned ? 0 : first);
            if (aim && k == 0) {
                in.aim_set = 1;
                in.aim = *aim;
                in.player.pressed = in.player.held = 1u << bo::launch_button;
            }
            bo::step(s, in);
            for (std::uint32_t i = 0; i < s.events; ++i) {
                returned = returned || (s.event[i].what == bo::Happened::paddle && s.event[i].id == 0);
                breaches += s.event[i].what == bo::Happened::breach;
            }
        }
        return worth(s, from) + 500 * breaches;
    }
    bo::Input decide(const bo::State& s) {
        bool waiting = false;
        s.world.each<bo::Ball>([&](xengine::Entity, const bo::Ball& b) { waiting = waiting || b.on == 1; });
        if (waiting && !s.over && !s.freeze) {
            double best = -1e18, aim = 0;
            for (int k = 0; k < 7; ++k) {
                const double a = -1.2 + 0.4 * k;
                const double w = rollout(s, a, 0, 420);
                if (w > best) best = w, aim = a;
            }
            bo::Input in = catcher(s, 0);
            in.aim_set = 1;
            in.aim = aim;
            in.player.pressed = in.player.held = 1u << bo::launch_button;
            return in;
        }
        const Landing l = landing(s);
        if (l.any && l.steps < 40 && s.frame > until) {
            double best = -1e18;
            for (int k = 0; k < 7; ++k) {
                const double o = -0.75 + 0.25 * k;
                const double w = rollout(s, std::nullopt, o, 420);
                if (w > best) best = w, offset = o;
            }
            until = s.frame + static_cast<std::int64_t>(l.steps) + 4;
        }
        return catcher(s, s.frame <= until ? offset : 0);
    }
};

// What a bot made of a room.
struct Played {
    std::int64_t frames = 0, idle_most = 0;
    std::int32_t lives_lost = 0, best = 0, beams = 0, flips = 0;
    std::uint32_t over = 0;
    std::int64_t score = 0;
};
template <class Bot>
Played play_room(const bo::Level& level, Bot&& bot, std::int64_t most) {
    bo::State s = bo::start(level);
    Played p;
    std::int64_t last = 0;
    for (; p.frames < most && !s.over; ++p.frames) {
        bo::step(s, bot(s));
        for (std::uint32_t i = 0; i < s.events; ++i) {
            const bo::Happened w = s.event[i].what;
            if (w == bo::Happened::paddle || w == bo::Happened::hit || w == bo::Happened::broke || w == bo::Happened::flipped || w == bo::Happened::charged ||
                w == bo::Happened::kicked || w == bo::Happened::core_hit || w == bo::Happened::launched)
                last = s.frame;
            p.beams += w == bo::Happened::beam;
            p.flips += w == bo::Happened::flipped;
        }
        p.idle_most = std::max(p.idle_most, s.frame - last);
    }
    p.lives_lost = level.lives - s.lives;
    p.over = s.over;
    p.best = s.best;
    p.score = s.score;
    return p;
}

// ── the game, headless ──

struct Game;
std::string name_of(Game& g, int card);

struct Game {
    Headless h;
    Planner planner;
    std::vector<std::string> broke;
    explicit Game(std::int32_t room = 1, HeadlessLevel up_to = HeadlessLevel::pixels, float scale = 1)
        : h{HeadlessOptions{.level = up_to, .size = {1280, 720}, .scale = scale}} {
        AppSpec spec = bo::app();
        h.runtime().set_theme(spec.theme);
        spec.setup(h.runtime());
        h.mount("main", spec.view());
        h.runtime().on(field(), "brick_broke", [this](const Value& a) { broke.emplace_back(a.get("id")->as_string()); });
        h.step();
        if (room != 1) {
            h.runtime().set(field(), "room", Value(static_cast<std::int64_t>(room)));
            h.step();
        }
        h.advance(seconds(0.6));  // laid out, the simulation begun from it, the style's cross-fade done
    }
    NodeId field() { return h.runtime().find("main/field"); }
    const bo::Field& component() { return *h.runtime().component<bo::Field>(field()); }
    const bo::State& state() { return *component().state(); }
    std::vector<NodeId> bricks() { return h.runtime().children(h.runtime().children(field()).at(0)); }
    Value seen() {
        for (const auto& n : h.tree().snapshot({}).nodes)
            if (n.id == "main/field") return n.value;
        return {};
    }
    // An agent's action on the field: whether it was done.
    bool act(std::string_view action, Value args = {}) {
        Invocation c;
        c.actor = {"agent", "test", {}};
        c.args = std::move(args);
        return h.tree().act("main/field", action, c).exit_code() == std::optional<int>(0);
    }
    // The planner plays (or, `smart` off, one who only catches), paused, a frame at a time, through the field's actions (until the room is over).
    bool smart = true;
    void play(std::int64_t frames) {
        for (std::int64_t k = 0; k < frames && !state().over; ++k) {
            const bo::Input in = smart ? planner.decide(state()) : catch_only(state());
            act("move_to", Value{}.set("x", in.player.pointer_x / 32767.0));
            if (in.aim_set) act("aim", Value{}.set("across", in.aim));
            if (in.player.went_down(bo::launch_button)) act("launch");
            act("step");
        }
    }
    // A frame drawn as a person sees it: paused play stands still, but its springs (a switch's knob) and fades settle.
    void settle() {
        const bool was = component().props.paused;
        if (!was) act("pause");
        h.advance(seconds(0.5));
        if (!was) act("resume");
    }
    std::uint64_t pixels() {
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        return std::stoull(hash.hex(), nullptr, 16);
    }
    // Each brick's component as the simulation has the brick: gone when broken; a shutter shown while
    // closed; a switch checked while on; a capacitor's slider at its charge.
    std::string disagreement() {
        const auto& def = bo::room(component().props.room);
        std::vector<const bo::Brick*> by_id(def.bricks.size(), nullptr);
        state().world.each<bo::Brick>([&](xengine::Entity, const bo::Brick& b) { by_id[b.id] = &b; });
        if (def.canvas) {  // the canvas's scene: a node for each brick standing
            auto& scene = world::scene(bo::cloud_scene);
            for (std::size_t i = 0; i < def.bricks.size(); ++i)
                if (scene.find(bo::brick_id(i)).has_value() != (by_id[i] != nullptr)) return std::format("note {}: in the scene", i);
            return {};
        }
        const auto nodes = bricks();
        const auto& r = h.runtime();
        for (std::size_t i = 0; i < nodes.size(); ++i) {
            const bo::Brick* b = by_id[i];
            const bo::Kind kind = def.bricks[i].kind;
            const bool shown = kind == bo::Kind::gate ? !bo::gates_open(state()) : kind == bo::Kind::shutter ? b && b->on == 0 : b != nullptr;
            if (r.get(nodes[i], "visible").as_bool(true) != shown) return std::format("brick {}: visible", i);
            if (kind == bo::Kind::toggle && r.get(nodes[i], "checked").as_bool() != (b->on != 0)) return std::format("switch {}: checked", i);
            if (kind == bo::Kind::slider && r.get(nodes[i], "value").as_double() != static_cast<double>(b->charge) / b->durability)
                return std::format("slider {}: value {}", i, r.get(nodes[i], "value").as_double());
        }
        return {};
    }
    // Played until it happens (at most `most` frames): whether it did.
    bool play_until(bo::Happened what, std::int64_t most) {
        for (std::int64_t k = 0; k < most && !state().over; ++k) {
            play(1);
            for (std::uint32_t i = 0; i < state().events; ++i)
                if (state().event[i].what == what) return true;
        }
        return false;
    }
};

std::string name_of(Game& g, int card) {
    auto& r = g.h.runtime();
    for (const NodeId n : r.children(r.find(std::format("main/between/offers/offer.{}", card))))
        if (r.get(n, "style").as_string() == "title") return std::string(r.get(n, "text").as_string());
    return {};
}

}  // namespace

UTest cases{"showcase.widget_breakout", [] {

    "a room's bricks are its components, placed by layout and read into the simulation where they stand"_test = [] {
        Game g;
        const auto nodes = g.bricks();
        expect(eq(nodes.size(), bo::room(1).bricks.size()));
        const Rect me = g.h.runtime().bounds(g.field());
        std::size_t matched = 0;
        g.state().world.each<bo::Body, bo::Brick>([&](xengine::Entity, const bo::Body& b, const bo::Brick& br) {
            const Rect r = g.h.runtime().bounds(nodes[br.id]);
            const double sx = bo::field_w / me.width, sy = bo::field_h / me.height;
            matched += b.box.x == (r.x - me.x) * sx && b.box.y == (r.y - me.y) * sy && b.box.w == r.width * sx && b.box.h == r.height * sy;
        });
        expect(eq(matched, nodes.size()));
        auto& r = g.h.runtime();
        expect(r.get(nodes[5], "label").as_string() == "Do Not Disturb" && r.get(nodes[5], "checked").as_bool()) << "a real switch, on";
        expect(r.get(nodes[0], "text").as_string() == "Shut down") << "the core: a real button";
        expect(std::abs(r.bounds(nodes[5]).x - me.x - 330) < 0.01) << std::format("placed where the room says: {}", r.bounds(nodes[5]).x - me.x);
        expect(eq(g.state().cores, std::int32_t{1}));
    };

    "120 steps a second on the runtime's clock, exactly; the paddle follows the agent; the launch goes where the agent aims"_test = [] {
        Game g;
        const auto start = g.state().frame;
        g.h.advance(seconds(1));
        expect(eq(g.state().frame - start, std::int64_t{120}));
        expect(g.act("move_to", Value{}.set("x", 0.25)));
        g.h.advance(seconds(1));
        expect(std::abs(g.state().paddles[0].x - 0.25 * bo::field_w) < 1) << std::format("paddle at {}", g.state().paddles[0].x);
        expect(g.act("aim", Value{}.set("across", -0.6)));
        expect(g.act("launch"));
        g.h.advance(seconds(0.1));
        bo::Ball b{};
        g.state().world.each<bo::Ball>([&](xengine::Entity, const bo::Ball& x) { b = x; });
        expect(b.on == 0 && b.velocity.x < 0 && std::abs(b.velocity.x / b.velocity.y - 0.6) < 0.05) << std::format("({}, {})", b.velocity.x, b.velocity.y);
        expect(!g.act("aim", Value{}.set("across", "left"))) << "a number";
        expect(g.act("release"));
    };

    "an agent plays room 1: the switch flips for real, the shield opens with it, every brick it breaks goes; the core shut down, the room is cleared"_test = [] {
        Game g;
        expect(g.act("pause"));
        std::string wrong;
        bool opened = false;
        for (int k = 0; k < 120 && !g.state().over; ++k) {
            g.play(30);
            if (wrong.empty()) wrong = g.disagreement();
            opened = opened || !g.h.runtime().get(g.bricks()[1], "visible").as_bool(true);
        }
        expect(eq(wrong, std::string()));
        expect(opened) << "the shield opened";
        expect(eq(g.state().over, std::uint32_t{1}));
        std::println("    cleared at {:.1f} s, score {}, {} broken", static_cast<double>(g.state().frame) / bo::steps_per_second, g.state().score, g.broke.size());
        expect(eq(static_cast<std::int32_t>(g.broke.size()), static_cast<std::int32_t>(std::ranges::count_if(bo::room(1).bricks, [](const bo::BrickDef& b) { return bo::breakable(b.kind); }))));
        const Value v = g.seen();
        expect(eq(v.get("score")->as_int(), g.state().score));
        expect(v.get("changes")->list() && !v.get("changes")->list()->empty()) << "the semantic tree's changes, for the inspector and agents";
    };

    "room 2: a capacitor's slider fills a step a hit and empties as it fires; its beam breaks the dam"_test = [] {
        Game g(2);
        expect(g.act("pause"));
        expect(g.play_until(bo::Happened::charged, 60 * bo::steps_per_second)) << "charged";
        expect(eq(g.disagreement(), std::string()));
        expect(g.play_until(bo::Happened::beam, 60 * bo::steps_per_second)) << "fired";
        expect(eq(g.disagreement(), std::string()));
        g.play(3);
        g.settle();
        expect(g.h.save_png(scratch_dir("breakout-room2") / "beam.png").has_value());
        const auto beam = g.pixels();
        bool emptied = false;
        for (const NodeId n : g.bricks())
            emptied = emptied || (g.h.runtime().get(n, "label").as_string() == "Brightness" && g.h.runtime().get(n, "value").as_double() == 0) ||
                      (g.h.runtime().get(n, "label").as_string() == "Volume" && g.h.runtime().get(n, "value").as_double() == 0);
        expect(emptied) << "a slider back to 0";
        g.play(90 * bo::steps_per_second);
        expect(eq(g.state().over, std::uint32_t{1}));
        expect(eq(g.disagreement(), std::string()));
        std::println("    beam {:016x}", beam);
        expect(eq(beam, std::uint64_t{0xa8ad53b398a6093f}));  // GCC (LLVM: CI)
    };

    "a rewind costs 100 sparks, puts every brick back and the score with it; never past 2 s nor the last landing; it cannot be farmed"_test = [] {
        Game g(3);
        expect(g.act("pause"));
        std::map<std::int64_t, std::pair<std::uint64_t, std::int64_t>> was;  // by frame: the checksum, the score
        expect(!g.act("rewind", Value{}.set("frames", 60))) << "no sparks yet: refused";
        for (int k = 0; k < 90 * bo::steps_per_second && g.state().sparks < 250 && !g.state().over; ++k) {
            was[g.state().frame] = {bo::checksum(g.state()), g.state().score};
            g.play(1);
        }
        expect(g.state().sparks >= 250) << std::format("{} sparks", g.state().sparks);
        const auto now = g.state().frame;
        expect(!g.act("rewind", Value{}.set("frames", bo::rewind_reach + 1))) << "2 s at most";
        expect(g.act("rewind", Value{}.set("frames", 200)));
        expect(eq(g.state().frame, now - 200));
        expect(eq(bo::checksum(g.state()), was[now - 200].first));
        expect(eq(g.state().score, was[now - 200].second));
        expect(eq(g.disagreement(), std::string()));
        const auto paid_from = g.state().sparks;
        g.play(1);
        expect(g.state().sparks <= paid_from - bo::rewind_cost + 10 * 40) << "paid";
        expect(!g.act("rewind", Value{}.set("frames", 5))) << "not past the last landing";
        // Farming: play two seconds, take them back, again and again — the sparks only go down, until it is refused.
        std::int32_t rewinds = 0, before = g.state().sparks;
        for (int k = 0; k < 20; ++k) {
            g.play(bo::rewind_reach);
            const std::int32_t had = g.state().sparks;
            if (!g.act("rewind", Value{}.set("frames", bo::rewind_reach))) break;
            g.play(1);
            expect(g.state().sparks <= had - bo::rewind_cost + 0) << std::format("rewind {}: {} → {}", rewinds, had, g.state().sparks);
            ++rewinds;
        }
        std::println("    {} rewinds from {} sparks", rewinds, before);
        expect(rewinds >= 1 && rewinds <= 3 + 2) << std::format("{} rewinds", rewinds);
        expect(!g.act("rewind", Value{}.set("frames", 60))) << "spent: refused";
        // R held: back through time, the cost when let go.
        Game r(3);
        expect(r.act("pause"));
        r.play(30 * bo::steps_per_second);
        while (r.state().sparks < bo::rewind_cost && !r.state().over) r.play(60);
        expect(r.act("resume"));
        const auto from = r.state().frame;
        const auto sparks = r.state().sparks;
        r.h.runtime().focus(r.field());
        r.h.step({KeyEvent{.kind = KeyKind::down, .key = Key::r}});
        r.h.advance(seconds(1));
        r.h.step({KeyEvent{.kind = KeyKind::up, .key = Key::r}});
        expect(r.state().frame <= from - bo::rewind_reach + 2 && r.state().frame >= from - bo::rewind_reach - 2) << std::format("{} → {}", from, r.state().frame);
        const auto landed = r.state().sparks;
        r.h.advance(seconds(1.0 / 60));
        expect(r.state().sparks <= landed - bo::rewind_cost + 10 * 30 && landed <= sparks) << std::format("{} then {}", landed, r.state().sparks);
        expect(eq(r.disagreement(), std::string()));
    };

    "the inspector (Tab): each brick's role and id, the hidden caches too — buttons to the tree, nothing on the screen — the changes, how the room is made"_test = [] {
        Game g(4);
        g.h.step();
        const auto plain = g.pixels();
        std::int32_t hidden = 0;
        for (const auto& n : g.h.tree().snapshot({}).nodes)
            for (std::size_t i = 0; i < bo::room(4).bricks.size(); ++i)
                if (bo::room(4).bricks[i].kind == bo::Kind::hidden && n.id.ends_with("/" + bo::brick_id(i)) && n.role == spec::Role::button) ++hidden;
        expect(eq(hidden, std::int32_t{7}));
        expect(g.h.runtime().get(g.bricks()[13], "opacity").as_double() == 0) << "invisible on screen";
        g.h.key(Key::tab);
        g.h.step();
        expect(g.seen().get("inspector")->as_bool()) << "open";
        expect(g.pixels() != plain) << "it draws";
        expect(g.h.save_png(scratch_dir("breakout-inspector") / "inspector.png").has_value());        // While it is open the game waits; Tab again, it goes on.
        const auto frame = g.state().frame;
        g.h.advance(seconds(0.5));
        expect(eq(g.state().frame, frame));
        g.h.key(Key::tab);
        g.h.advance(seconds(0.5));
        expect(g.state().frame > frame) << "on again";
        // Its keys are its own: Space pressed while it is open does not launch once it closes.
        Game w(1);
        w.h.step();
        auto waiting = [&] {
            bool on = false;
            w.state().world.each<bo::Ball>([&](xengine::Entity, const bo::Ball& b) { on = on || b.on != 0; });
            return on;
        };
        expect(waiting()) << "a ball on the paddle";
        w.h.key(Key::tab);
        w.h.step();
        w.h.key(Key::space);
        w.h.step();
        w.h.key(Key::tab);
        w.h.advance(seconds(0.5));
        expect(waiting()) << "still on the paddle: the Space was the inspector's";
    };

    "the bricks are the ball's: a click on one reaches the field, an agent's toggle of one refused (read-only, and why); the keys steer after the pointer"_test = [] {
        Game g(1);
        g.h.step();
        std::size_t sw = 0;
        for (std::size_t i = 0; i < bo::room(1).bricks.size(); ++i)
            if (bo::room(1).bricks[i].kind == bo::Kind::toggle) sw = i;
        const NodeId toggle = g.bricks()[sw];
        const bool was = g.h.runtime().get(toggle, "checked").as_bool();
        // A person's click where it is drawn: the field takes it (the switch does not change).
        const Rect r = g.h.runtime().screen_bounds(toggle);
        expect(g.h.runtime().hit_test(r.center()) != toggle) << "the pointer passes to the field";
        g.h.click(r.center());
        g.h.step();
        expect(g.h.runtime().get(toggle, "checked").as_bool() == was) << "unchanged by a click";
        // An agent's toggle: the simulation's state again at once.
        Invocation call;
        call.actor = {"agent", "test", {}};
        const Envelope refused = g.h.runtime().act(toggle, "toggle", call);
        expect(refused.exit_code() != std::optional<int>(0) && refused.ndjson().contains("read-only: the ball operates the bricks")) << refused.ndjson();
        g.h.step();
        expect(g.h.runtime().get(toggle, "checked").as_bool() == was) << "unchanged";
        expect(g.h.tree().node(g.h.runtime().path(toggle))->states.has(State::readonly)) << "read-only to every reader";
        expect(!g.h.tree().node(g.h.runtime().path(toggle))->states.has(State::focusable)) << "not in the Tab order";
    };

    "room 3: a switch of the cloud, near and live, is the ball's too (read-only, an agent told why)"_test = [] {
        Game g(3);
        g.h.settle();
        std::optional<spec::SemanticNode> live;
        for (const auto& n : g.h.tree().snapshot({}).nodes)
            if (n.role == spec::Role::switch_ && n.id.contains("/cloud/")) live = n;
        expect(live.has_value()) << "a live switch in the cloud";
        if (!live) return;
        expect(live->states.has(State::readonly)) << "read-only to every reader";
        Invocation call;
        call.actor = {"agent", "test", {}};
        const Envelope refused = g.h.tree().act(live->id, "toggle", call);
        expect(refused.exit_code() != std::optional<int>(0) && refused.ndjson().contains("the ball operates the bricks")) << refused.ndjson();
    };

    "room 3: the cloud is a canvas's scene — live near, boxes far; through it the camera pulls back, never past half; a rewind puts the scene back"_test = [] {
        Game g(3);
        auto& scene = world::scene(bo::cloud_scene);
        const auto& def = bo::room(3);
        expect(eq(g.state().height, 2 * bo::field_h));
        std::size_t notes = 0;
        for (std::size_t i = 0; i < def.bricks.size(); ++i) notes += scene.find(bo::brick_id(i)).has_value();
        expect(eq(notes, def.bricks.size()));
        const NodeId cloud = g.h.runtime().children(g.field()).at(0);
        g.settle();
        expect(g.h.runtime().children(cloud).size() > 3) << std::format("live components near the paddle: {}", g.h.runtime().children(cloud).size());
        // A live switch is one switch to a reader: not also the box that places it.
        const auto wifi = g.h.tree().find({.role = Role::switch_, .name = "Wi-Fi"});
        expect(wifi.size() == 1) << std::format("{} Wi-Fi switches", wifi.size());
        expect(g.act("pause"));
        double least = 1;
        bool breached = false;
        for (int k = 0; k < 60 * bo::steps_per_second && !g.state().over; ++k) {
            g.play(1);
            least = std::min(least, g.state().zoom);
            for (std::uint32_t i = 0; i < g.state().events; ++i) breached = breached || g.state().event[i].what == bo::Happened::breach;
            if (breached && least < 0.6) break;
        }
        expect(breached && least < 0.6 && least >= 0.5) << std::format("through the cloud; zoomed out to {}", least);
        g.settle();
        expect(g.h.save_png(scratch_dir("breakout-room3") / "chamber.png").has_value());
        const auto chamber = g.pixels();
        auto agrees = [&] {
            std::size_t wrong = 0;
            std::vector<bool> standing(def.bricks.size(), false);
            g.state().world.each<bo::Brick>([&](xengine::Entity, const bo::Brick& b) { standing[b.id] = true; });
            for (std::size_t i = 0; i < def.bricks.size(); ++i) wrong += scene.find(bo::brick_id(i)).has_value() != standing[i];
            return wrong;
        };
        expect(eq(agrees(), std::size_t{0}));
        std::println("    chamber {:016x}", chamber);
        expect(eq(chamber, std::uint64_t{0xe59fbd4073a0798f}));  // GCC (LLVM: CI)
    };

    "room 4: dark but round the ball and the paddle; the X-ray shows what hides, then recharges; the gate comes round"_test = [] {
        Game g(4);
        g.smart = false;  // one who only catches: the room lasts
        expect(g.act("pause"));
        // The gate: closed, then open two seconds of every seven from 0:02.
        const auto gate = static_cast<std::size_t>(std::ranges::find(bo::room(4).bricks, bo::Kind::gate, &bo::BrickDef::kind) - bo::room(4).bricks.begin());
        expect(g.h.runtime().get(g.bricks()[gate], "visible").as_bool(true) && !bo::gates_open(g.state())) << "closed";
        g.play(2 * bo::steps_per_second + 10 - g.state().frame);
        expect(bo::gates_open(g.state()) && !g.h.runtime().get(g.bricks()[gate], "visible").as_bool(true)) << "open";
        g.play(3 * bo::steps_per_second - g.state().frame);
        expect(g.seen().get("dark")->as_bool()) << "its lights still off";
        g.settle();
        const auto dark = g.pixels();
        expect(g.h.save_png(scratch_dir("breakout-room4-dark") / "dark.png").has_value());
        expect(g.act("scan"));
        expect(!g.act("scan")) << "recharging";
        expect(g.seen().get("scanning")->as_bool()) << "scanning";
        g.play(20);
        g.settle();
        const auto scanned = g.pixels();
        expect(g.h.save_png(scratch_dir("breakout-room4") / "scan.png").has_value());
        expect(dark != scanned) << "the X-ray draws";
        g.play(200);
        expect(!g.seen().get("scanning")->as_bool()) << "over";
        g.play(6 * bo::steps_per_second);
        expect(g.act("scan")) << "recharged";
        expect(eq(g.disagreement(), std::string()));
        std::println("    dark {:016x}, scanned {:016x}", dark, scanned);
        expect(eq(dark, std::uint64_t{0x475e133593436b9a}));  // GCC (LLVM: CI)
        expect(eq(scanned, std::uint64_t{0x2a08b435ad1b17ce}));
    };

    "room 5: the copilot plays the top paddle from outside — the semantic tree in, guard_to out, journaled as its own; an order changes where it sends the ball; with a player below, they shut it down"_test = [] {
        Game g(5, HeadlessLevel::layout);
        expect(eq(g.state().paddles[1].active, std::int32_t{1}));
        expect(!Game().act("order", Value{}.set("to", "left"))) << "the copilot is room 5's";
        expect(g.act("order", Value{}.set("to", "left")));
        expect(!g.act("order", Value{}.set("to", "dance"))) << "four orders";
        expect(eq(std::string(g.seen().get("order")->as_string()), std::string("left")));
        // The player below: the planner, through the field's actions, the game running. Left, then right, then hold.
        std::int64_t frames = 0;
        bool went_left = false, went_right = false;
        for (; frames < 90 * 60 && !g.state().over; ++frames) {
            const bo::Input in = g.planner.decide(g.state());
            g.act("move_to", Value{}.set("x", in.player.pointer_x / 32767.0));
            if (in.aim_set) g.act("aim", Value{}.set("across", in.aim));
            if (in.player.went_down(bo::launch_button)) g.act("launch");
            if (went_left && g.seen().get("order")->as_string() == "left") g.act("order", Value{}.set("to", "right"));
            if (went_right && g.seen().get("order")->as_string() == "right") g.act("order", Value{}.set("to", "hold"));
            g.h.step();
            const Value v = g.seen();
            if (const Value* log = v.get("agent"); log && log->list() && !log->list()->empty()) {
                const std::string last(log->list()->back().as_string());
                went_left = went_left || last.ends_with("aims at 0");
                went_right = went_right || last.ends_with("aims at 1280");
            }
        }
        expect(went_left && went_right) << std::format("told left, it aimed off the left wall ({}); right, the right ({})", went_left, went_right);
        std::println("    over {} after {:.1f} s, score {}, lives {}", g.state().over, static_cast<double>(g.state().frame) / 120, g.state().score, g.state().lives);
        expect(eq(g.state().over, std::uint32_t{1}));
        std::size_t guards = 0;
        for (const auto& e : g.h.runtime().action_journal()) guards += e.action == "guard_to" && e.actor.find("copilot") != std::string::npos && e.exit_code == 0;
        expect(guards > 10) << std::format("{} guard_to calls by the copilot", guards);
        expect(!g.seen().get("agent")->list()->empty()) << "its actions, with what it saw, in the field's value";
        // Seen: its word and where it means to send the ball.
        Game p(5);
        expect(p.act("order", Value{}.set("to", "feed")));
        expect(p.act("launch"));
        p.h.advance(seconds(2.5));
        p.settle();
        const auto px = p.pixels();
        expect(p.h.save_png(scratch_dir("breakout-room5") / "copilot.png").has_value());
        std::println("    copilot {:016x}", px);
        expect(eq(px, std::uint64_t{0x5f2cf42ec4b0fc91}));  // GCC (LLVM: CI)
    };

    "a run: room 1 shut down — the wave, the next room's skin, two contraptions offered; one taken, room 2 begins with what the run carries and drops it"_test = [] {
        Game g;
        expect(g.act("pause"));
        g.play(90 * bo::steps_per_second);
        expect(eq(g.state().over, std::uint32_t{1}));
        const auto score = g.state().score;
        const auto lives = g.state().lives;
        expect(!g.act("choose", Value{}.set("gift", "wide"))) << "not yet: the moment first";
        for (int k = 0; k < 200 && !g.seen().get("choosing")->as_bool(); ++k) g.act("step");
        expect(g.seen().get("choosing")->as_bool()) << "choosing";
        const Value seen = g.seen();
        const Value* offers = seen.get("offers");
        expect(offers && offers->list() && offers->list()->size() == 2) << "two offered";
        expect(std::string((*offers->list())[0].as_string()) == "wide" && std::string((*offers->list())[1].as_string()) == "magnet") << "the long paddle, the magnet";
        g.settle();
        expect(g.h.runtime().get(g.h.runtime().find("main/between"), "visible").as_bool()) << "the cards shown";
        expect(g.h.save_png(scratch_dir("breakout-run") / "choice.png").has_value());
        const auto choice = g.pixels();
        expect(!g.act("choose", Value{}.set("gift", "pierce"))) << "not offered";
        expect(g.h.click("main/between/offers/offer.1/offer.1.tools/offer.1.take")) << "the magnet taken";
        g.h.step();
        g.h.advance(seconds(0.25));
        expect(eq(g.component().props.room, std::int32_t{2}));
        expect(!g.h.runtime().get(g.h.runtime().find("main/between"), "visible").as_bool(true)) << "the cards gone";
        expect(g.state().score == score && g.state().lives == lives) << "the run's score and balls carried";
        expect(g.state().rules.gift_mask == bo::gift_bit(bo::Gift::magnet)) << "only the magnet drops";
        std::int32_t slots = 0;
        for (const bo::BrickSpec& b : g.component().level().bricks) slots += b.gift == bo::Gift::magnet;
        expect(eq(slots, std::int32_t{2}));
        expect(eq(g.component().run_so_far().kit.size(), std::size_t{1}));
        // Out of balls: Enter, and a new run from room 1.
        expect(g.act("pause"));
        for (int k = 0; k < 40 * 120 && !g.state().over; ++k) {
            g.act("move_to", Value{}.set("x", 0.0));
            if (k % 30 == 0) g.act("launch");
            g.act("step");
        }
        expect(eq(g.state().over, std::uint32_t{2}));
        expect(g.act("next"));
        g.h.step();
        g.h.advance(seconds(0.25));
        expect(g.component().props.room == 1 && g.component().run_so_far().kit.empty() && g.state().score == 0 && g.state().lives == 3) << "afresh";
        std::println("    choice {:016x}", choice);
        expect(eq(choice, std::uint64_t{0x9db0d154abf33d39}));  // GCC (LLVM: CI)
    };

    "between rooms, a casing open: its TypeScript script edited and reloaded in place under 200 ms, a preset retuning it, a bad one keeping the rules; the rules reach the next room"_test = [] {
        Game g;
        expect(g.act("pause"));
        g.play(90 * bo::steps_per_second);
        expect(g.act("next")) << "on to the choice";
        g.h.step();
        auto& r = g.h.runtime();
        const auto status = [&](std::string_view card) { return std::string(r.get(r.find(std::format("main/between/offers/{}/{}.state", card, card)), "text").as_string()); };
        const auto casing = [&] { return std::string(r.get(r.find("main/between/casing/casing.status"), "text").as_string()); };
        auto wait = [&](auto&& done) {
            for (int k = 0; k < 900 && !done(); ++k) g.h.step();
        };
        // The scripts load as the choice opens.
        wait([&] { return g.seen().get("rules") && g.h.runtime().get(r.find("main/between"), "visible").as_bool(); });
        g.h.advance(seconds(0.5));
        // A preset: the long paddle huge and brief (wide.ts retuned to 260: its power fixed, 1100 / 260 s).
        expect(g.h.click("main/between/offers/offer.0/offer.0.presets/offer.0.brief"));
        wait([&] { return status("offer.0").starts_with("set:") || status("offer.0").starts_with("kept"); });
        expect(eq(status("offer.0"), std::string("set: Huge · 4 s")));
        g.h.step();
        // The casing of the magnet: edited, applied.
        expect(g.h.click("main/between/offers/offer.1/offer.1.tools/offer.1.open"));
        g.h.step();
        const NodeId box = r.find("main/between/casing/casing.script");
        const std::string source(r.get(box, "text").as_string());
        expect(bo::Workshop::tuned(source) == 3.0) << source;
        Invocation edit;
        edit.args = Value{}.set("value", bo::Workshop::retune(source, 5));
        expect(r.act(box, "set_value", edit).exit_code() == std::optional<int>(0));
        expect(g.h.click("main/between/casing/casing.tools/casing.apply"));
        wait([&] { return casing().starts_with("reloaded") || casing().starts_with("kept"); });
        const std::string magnet = casing();
        expect(magnet.starts_with("reloaded in ") && std::stod(magnet.substr(12)) < 200) << magnet;
        expect(g.h.save_png(scratch_dir("breakout-casing") / "casing.png").has_value());
        // A bad edit: the rules kept, the reason said.
        Invocation bad;
        bad.args = Value{}.set("value", std::string("import { verb } from \"openmui\";\nverb(\"gift.magnet\", { effect: \"read\" }, () => ({ magnet_catches: }));"));
        expect(r.act(box, "set_value", bad).exit_code() == std::optional<int>(0));
        expect(g.h.click("main/between/casing/casing.tools/casing.apply"));
        wait([&] { return casing().starts_with("kept") || casing().starts_with("reloaded"); });
        expect(casing().starts_with("kept the rules")) << casing();
        expect(g.h.click("main/between/casing/casing.tools/casing.shipped"));
        wait([&] { return casing().starts_with("reloaded"); });
        expect(g.h.click("main/between/casing/casing.tools/casing.close"));
        expect(g.h.click("main/between/offers/offer.0/offer.0.tools/offer.0.take")) << "the long paddle taken";
        g.h.step();
        g.h.advance(seconds(0.25));
        expect(g.state().rules.wide_half == 260 && g.state().rules.wide_steps == 508) << std::format("wide {} for {} steps", g.state().rules.wide_half, g.state().rules.wide_steps);
        expect(eq(g.state().rules.magnet_catches, std::int32_t{3}));  // as shipped again
        // After room 2, the splitter: its casing edited to four extra balls.
        expect(g.act("pause"));
        g.play(90 * bo::steps_per_second);
        expect(g.act("next")) << "on to the choice";
        g.h.step();
        wait([&] { return g.h.runtime().get(r.find("main/between"), "visible").as_bool(); });
        g.h.advance(seconds(0.5));  // as above: the cards come in by the room's style before they are there to click
        expect(std::string(name_of(g, 0)) == "Splitter") << name_of(g, 0);
        expect(g.h.click("main/between/offers/offer.0/offer.0.tools/offer.0.open"));
        g.h.step();
        g.h.advance(seconds(0.5));  // the casing comes in by the room's style too
        const std::string splitter(r.get(box, "text").as_string());
        expect(splitter.find("from \"openmui\"") != std::string::npos && bo::Workshop::tuned(splitter) == 2.0) << splitter;
        Invocation four;
        four.args = Value{}.set("value", bo::Workshop::retune(splitter, 4));
        expect(r.act(box, "set_value", four).exit_code() == std::optional<int>(0));
        expect(g.h.click("main/between/casing/casing.tools/casing.apply"));
        wait([&] { return casing().starts_with("reloaded") || casing().starts_with("kept"); });
        const std::string split = casing();
        expect(split.starts_with("reloaded in ") && std::stod(split.substr(12)) < 200) << split;
        expect(g.h.click("main/between/offers/offer.0/offer.0.tools/offer.0.take")) << "the splitter taken";
        g.h.step();
        g.h.advance(seconds(0.25));
        expect(g.component().props.room == 3 && g.state().rules.multi_balls == 4) << std::format("room {}, {} extra balls", g.component().props.room, g.state().rules.multi_balls);
        expect(eq(g.state().rules.gift_mask, bo::gift_bit(bo::Gift::wide) | bo::gift_bit(bo::Gift::multi)));
        std::println("    reloads: the magnet {}, the splitter {}", magnet.substr(0, magnet.find(" ms") + 3), split.substr(0, split.find(" ms") + 3));
    };

    "heard: each brick as its component, a chain's notes climbing, the paddle, a beam — on its step's sample, the same everywhere; a rewind is silent"_test = [] {
        Game g(2);
        expect(g.act("pause"));
        g.play(25 * bo::steps_per_second);
        auto& sounds = const_cast<bo::Field&>(g.component()).sounds();
        const auto samples = xengine::audio::render(sounds.mixer(), g.state().frame * sounds.step_samples() + sounds.mixer().rate());
        const auto begun = sounds.mixer().begun();
        expect(begun > 20) << std::format("{} sounds", begun);
        const auto heard = xengine::audio::hash(samples);
        expect(xengine::audio::peak(samples) > 0.05f && xengine::audio::peak(samples) <= 1.0f) << std::format("peak {}", xengine::audio::peak(samples));
        std::println("    heard {:016x} ({} sounds)", heard, begun);
        // Its level, measured: never clipped (a peak under −0.5 dBFS), loud enough to be heard over a room, not a wall of sound.
        double energy = 0;
        float top = 0;
        for (const float v : samples) {
            energy += static_cast<double>(v) * v;
            top = std::max(top, std::abs(v));
        }
        const double rms_db = 10 * std::log10(energy / static_cast<double>(std::max<std::size_t>(1, samples.size()))), peak_db = 20 * std::log10(std::max(top, 1e-9f));
        std::println("    level: peak {:.1f} dBFS, RMS {:.1f} dBFS", peak_db, rms_db);
        expect(peak_db <= -0.5 && rms_db > -24 && rms_db < -10) << std::format("peak {:.1f}, RMS {:.1f} dBFS", peak_db, rms_db);
        // To listen to: the run as heard, its theme under it.
        const auto wav = xengine::audio::wav(samples, sounds.mixer().rate());
        std::ofstream(scratch_dir("breakout-heard") / "heard.wav", std::ios::binary).write(reinterpret_cast<const char*>(wav.data()), static_cast<std::streamsize>(wav.size()));
        expect(eq(heard, std::uint64_t{0x62ef9b47a1e4120d}));  // GCC (LLVM: CI)
    };

    "frames are pinned: room 1 under way with the aim's forecast, and the inspector"_test = [] {
        Game g;
        expect(g.act("pause"));
        g.play(900);
        g.settle();
        const auto played = g.pixels();
        const auto dir = scratch_dir("breakout-room1");  // to look at
        expect(g.h.save_png(dir / "played.png").has_value());
        Game fresh;
        fresh.settle();
        expect(fresh.h.save_png(dir / "start.png").has_value());
        const auto start = fresh.pixels();
        g.h.key(Key::tab);
        g.settle();
        const auto inspector = g.pixels();
        expect(g.h.save_png(dir / "inspector.png").has_value());
        std::println("    start {:016x}, played {:016x}, inspector {:016x}", start, played, inspector);
        expect(eq(start, std::uint64_t{0xe54a8fcdd97b1865}));  // GCC (LLVM: CI)
        expect(eq(played, std::uint64_t{0x85cd01f515200a90}));
        expect(eq(inspector, std::uint64_t{0xc88a844d0df6a473}));
    };

    "the README's loop: room 1 played from its launch to its shutdown — an animated WebP under 1 MB"_test = [] {
        Game g(1, HeadlessLevel::pixels, 0.5f);  // 640 × 360
        expect(g.act("pause"));
        std::int32_t frames = 0;
        for (int k = 0; k < 400 && (g.state().over == 0 || k % 4 != 0); ++k) {
            g.play(8);  // 15 frames a second of the game's 120
            if (g.state().over) {
                for (int i = 0; i < 8; ++i) g.act("step");
            }
            g.h.step();
            g.h.record();
            ++frames;
            if (g.state().over && g.component().run_so_far().score > 0) break;
        }
        const auto file = scratch_dir("readme-widget-breakout") / "room-1.webp";
        expect(g.h.save_recording(file, {.delay_num = 1, .delay_den = 15}).has_value());
        const auto size = std::filesystem::file_size(file);
        std::println("    the loop: {} frames, {} KB", frames, size / 1024);
        expect(size < 1024 * 1024) << std::format("{} bytes", size);
    };

    "the README's stills: a beam breaking the dam, the breakthrough, the X-ray in the dark, a casing open between rooms — 640 × 360"_test = [] {
        const auto dir = scratch_dir("readme-widget-breakout-stills");
        auto save = [&](Game& g, std::string_view name) {
            g.settle();
            const auto file = dir / std::format("{}.png", name);
            expect(g.h.save_png(file).has_value());
            std::println("    {}: {} KB", name, std::filesystem::file_size(file) / 1024);
        };
        {
            Game g(2, HeadlessLevel::pixels, 0.5f);
            expect(g.act("pause"));
            expect(g.play_until(bo::Happened::beam, 60 * bo::steps_per_second)) << "a beam";
            g.play(2);
            save(g, "room-2-beam");
        }
        {
            Game g(3, HeadlessLevel::pixels, 0.5f);
            expect(g.act("pause"));
            expect(g.play_until(bo::Happened::breach, 60 * bo::steps_per_second)) << "the breakthrough";
            g.play(50);
            save(g, "room-3-breakthrough");
        }
        {
            Game g(4, HeadlessLevel::pixels, 0.5f);
            g.smart = false;
            expect(g.act("pause"));
            g.play(3 * bo::steps_per_second);
            expect(g.act("scan"));
            g.play(30);
            save(g, "room-4-x-ray");
        }
        {
            Game g(1, HeadlessLevel::pixels, 0.5f);
            expect(g.act("pause"));
            g.play(90 * bo::steps_per_second);
            expect(g.act("next"));
            for (int k = 0; k < 120; ++k) g.h.step();  // the scripts loaded
            expect(g.h.click("main/between/offers/offer.0/offer.0.tools/offer.0.open"));
            save(g, "between-rooms");
        }
    };

    // ── the bots' checks ──

    "every room can be cleared: a player who understands the machine shuts each down within 90 s, no ball lost"_test = [] {
        for (std::int32_t r = 1; r <= bo::room_count; ++r) {
            Game g(r, HeadlessLevel::layout);
            Planner planner;
            const Played p = play_room(g.component().level(), [&](const bo::State& st) { return planner.decide(st); }, 90 * bo::steps_per_second);
            std::println("    room {}: {:.1f} s, best chain {}, {} beams, {} flips, score {}", r, static_cast<double>(p.frames) / 120, p.best, p.beams, p.flips, p.score);
            expect(p.over == 1 && p.lives_lost == 0) << std::format("room {}: over {}, {} lost", r, p.over, p.lives_lost);
        }
    };

    "no unavoidable drains: a player who only catches, where the ball is going, loses no ball in a minute of any room"_test = [] {
        for (std::int32_t r = 1; r <= bo::room_count; ++r) {
            Game g(r, HeadlessLevel::layout);
            const Played p = play_room(g.component().level(), [](const bo::State& st) { return catch_only(st); }, 60 * bo::steps_per_second);
            expect(p.lives_lost == 0) << std::format("room {}: {} lost", r, p.lives_lost);
        }
    };

    "no endless orbits: in every room, played either way, something moves at least every 10 s"_test = [] {
        for (std::int32_t r = 1; r <= bo::room_count; ++r) {
            Game g(r, HeadlessLevel::layout);
            Planner planner;
            const Played a = play_room(g.component().level(), [&](const bo::State& st) { return planner.decide(st); }, 90 * bo::steps_per_second);
            const Played b = play_room(g.component().level(), [](const bo::State& st) { return catch_only(st); }, 120 * bo::steps_per_second);
            std::println("    room {}: the longest still {:.1f} s and {:.1f} s", r, static_cast<double>(a.idle_most) / 120, static_cast<double>(b.idle_most) / 120);
            expect(a.idle_most < 10 * bo::steps_per_second && b.idle_most < 10 * bo::steps_per_second) << std::format("room {}", r);
        }
    };

    "understanding the machine pays: the player who plays the switches, capacitors and kickers clears the rooms far sooner than one who only catches"_test = [] {
        std::int32_t sooner = 0;
        double smart = 0, plain = 0;
        for (std::int32_t r = 1; r <= bo::room_count; ++r) {
            Game g(r, HeadlessLevel::layout);
            Planner planner;
            const Played a = play_room(g.component().level(), [&](const bo::State& st) { return planner.decide(st); }, 120 * bo::steps_per_second);
            const Played b = play_room(g.component().level(), [](const bo::State& st) { return catch_only(st); }, 120 * bo::steps_per_second);
            sooner += a.over == 1 && (b.over != 1 || a.frames * 4 < b.frames * 3);  // a quarter sooner at least
            smart += static_cast<double>(a.frames) / 120;
            plain += static_cast<double>(b.frames) / 120;  // not cleared: the whole 120 s
            std::println("    room {}: {:.1f} s against {} ", r, static_cast<double>(a.frames) / 120,
                         b.over == 1 ? std::format("{:.1f} s", static_cast<double>(b.frames) / 120) : std::string("not cleared in 120 s"));
        }
        expect(sooner >= 3 && smart < 0.6 * plain) << std::format("sooner in {} rooms of 5; {:.0f} s in all against {:.0f} s", sooner, smart, plain);
    };
}};
