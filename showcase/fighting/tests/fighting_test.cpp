// The fighting game as a player and an agent meet it: the match runs at 60
// frames a second on the runtime's clock (exactly, headless), the keyboard
// walks a fighter, an agent's pads land a jab, pause and step play one frame
// at a time, and the stage reads as a semantic node an agent can follow.
// Versus and training frames are pinned: the same pixels everywhere.
import std;
import openmui;
import openmui.showcase.fighting;
import openmui.testing;

using namespace omui::testing;
using namespace omui;

namespace {

struct Game {
    Headless h{HeadlessOptions{.size = {960, 540}}};
    explicit Game(std::string_view mode = "versus") {
        AppSpec spec = fighting::app();
        spec.setup(h.runtime());
        if (mode == "versus") {
            h.mount("main", spec.view());
        } else {
            h.mount("main", fighting::stage({.id = "stage", .mode = std::string(mode), .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}));
        }
        h.step();
    }
    const fighting::Stage& stage() { return *h.runtime().component<fighting::Stage>(h.runtime().find("main")); }
    const gongfu::State& state() { return stage().match().state(); }
    Value seen() {
        for (const auto& n : h.tree().snapshot({}).nodes)
            if (n.id == "main") return n.value;
        return {};
    }
    // An agent's action on the stage: whether it was done.
    bool act(std::string_view action, Value args = {}) {
        Invocation c;
        c.actor = {"agent", "test", {}};
        c.args = std::move(args);
        return h.tree().act("main", action, c).exit_code() == std::optional<int>(0);
    }
    std::uint64_t pixels() {
        Hash hash;
        const auto& px = h.pixels();
        for (int y = 0; y < px.height(); ++y) hash.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        return std::stoull(hash.hex(), nullptr, 16);
    }
};

Value pad(int player, std::string_view p) { return Value{}.set("player", player).set("pad", p); }

}  // namespace

int main(int argc, char** argv) {
    Suite s{"showcase.fighting"};

    s.test("the roster reads: the training fighter's file, every move compiled", [] {
        const auto& r = fighting::roster();
        check(r.problems.empty(), r.problems.empty() ? std::string() : r.problems.front());
        check_eq(r.fighters.size(), std::size_t{1});
        check_eq(r.fighters[0].def.name, std::string("Kata"));
        check(r.fighters[0].find("236P") >= 0 && r.fighters[0].find("623P") >= 0, "its specials");
        check_eq(r.looks.size(), 2 * r.fighters.size());  // a colour for each side
    });

    s.test("sixty frames a second on the runtime's clock, exactly", [] {
        Game g;
        const auto start = g.state().frame;
        g.h.advance(seconds(1));
        check_eq(g.state().frame - start, 60);
        g.h.advance(seconds(2.5));
        check_eq(g.state().frame - start, 210);
    });

    s.test("the keyboard walks player 1 forward and player 2 back, until let go", [] {
        Game g;
        const auto x0 = g.state().f[0].pos.x, y0 = g.state().f[1].pos.x;
        g.h.step({KeyEvent{.kind = KeyKind::down, .key = Key::d}, KeyEvent{.kind = KeyKind::down, .key = Key::right}});
        g.h.advance(seconds(0.5));
        g.h.step({KeyEvent{.kind = KeyKind::up, .key = Key::d}, KeyEvent{.kind = KeyKind::up, .key = Key::right}});
        const auto x1 = g.state().f[0].pos.x, y1 = g.state().f[1].pos.x;
        check(x1 > x0, std::format("player 1 walked from {} to {}", x0.to_double(), x1.to_double()));
        check(y1 > y0, std::format("player 2, facing left, walked back from {} to {}", y0.to_double(), y1.to_double()));
        check_eq(g.seen().get("p1")->get("doing")->as_string(), std::string_view("walk_forward"));
        g.h.advance(seconds(0.1));
        check(g.state().f[0].pos.x == x1 || g.state().f[0].pos.x < x1 + gongfu::Fixed::of(8), "it stops");
        check_eq(g.seen().get("p1")->get("doing")->as_string(), std::string_view("idle"));
    });

    s.test("pause and step: one frame at a time; step refused while playing", [] {
        Game g;
        check(!g.act("step"), "step plays a paused match");
        check(g.act("pause"));
        const auto f = g.state().frame;
        g.h.advance(seconds(1));
        check_eq(g.state().frame, f);
        check(g.act("step"));
        check(g.act("step"));
        check_eq(g.state().frame, f + 2);
        check(g.act("resume"));
        g.h.advance(seconds(1));
        check_eq(g.state().frame, f + 2 + 60);
    });

    s.test("an agent walks in and lands a jab: player 2's health falls, the stage says so", [] {
        Game g;
        check(g.act("pause"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 90; ++i) check(g.act("step"));
        check(g.act("press", pad(0, "5LP")));
        check(g.act("step"));
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 10; ++i) check(g.act("step"));
        const Value v = g.seen();
        check(v.get("p2")->get("health")->as_int() < 10000, std::format("player 2 at {}", v.get("p2")->get("health")->as_int()));
        check_eq(v.get("p1")->get("name")->as_string(), std::string_view("Kata"));
        check(!g.act("press", pad(2, "5")), "two players");
        check(!g.act("press", pad(0, "5XP")), "not a pad");
        check(g.act("release", Value{}.set("player", 0)));
        check(g.act("restart"));
        check_eq(g.seen().get("p2")->get("health")->as_int(), std::int64_t{10000});
    });

    s.test("training's readings as an agent reads them: a blocked jab is +3, a jab cancelled into a fireball a combo of 900", [] {
        Game g("training");
        check(g.act("pause"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 90; ++i) check(g.act("step"));
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 10; ++i) check(g.act("step"));
        check(g.act("press", pad(1, "6")));  // player 2 faces left: 6 holds back, a block
        check(g.act("press", pad(0, "5LP")));
        check(g.act("step"));
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 40; ++i) check(g.act("step"));
        Value seen = g.seen();  // the pointers below point into it
        const Value* t = seen.get("training");
        check(t && t->get("advantage") && t->get("advantage")->as_int() == 3, std::format("advantage {}", t && t->get("advantage") ? t->get("advantage")->as_int() : -99));
        check(t && t->get("startup") && t->get("startup")->as_int() == 4, "startup 4");
        // Close again, then 5LP, 2, 3, 6+LP: the jab cancelled into the fireball.
        check(g.act("press", pad(1, "5")));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 90; ++i) check(g.act("step"));
        for (std::string_view p : {"5LP", "2", "2", "3", "3", "6", "6LP"}) {
            check(g.act("press", pad(0, p)));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 80; ++i) check(g.act("step"));
        seen = g.seen();
        t = seen.get("training");
        const Value* c = t ? t->get("combo") : nullptr;
        check(c && c->get("hits")->as_int() == 2 && c->get("damage")->as_int() == 900, std::format("{}", c ? c->get("hits")->as_int() : -1));
    });

    s.test("a saved state comes back; a dummy plays its recording again", [] {
        Game g("training");
        check(!g.act("load"), "nothing saved yet");
        check(g.act("pause"));
        check(g.act("save"));
        const gongfu::State saved = g.state();
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 30; ++i) check(g.act("step"));
        check(g.state().frame == saved.frame + 30);
        check(g.act("load"));
        check(gongfu::checksum(g.state()) == gongfu::checksum(saved), "the same bytes");
        // Player 2 walks back and forth while recording, then the dummy does it again.
        check(g.act("press", pad(0, "5")));
        check(g.act("record", Value{}.set("player", 1)));
        std::vector<double> walked;
        for (int i = 0; i < 40; ++i) {
            check(g.act("press", pad(1, i < 20 ? "6" : "4")));
            check(g.act("step"));
            walked.push_back(g.state().f[1].pos.x.to_double());
        }
        check(g.act("release", Value{}.set("player", 1)));
        check(g.act("load"));
        check(g.act("replay", Value{}.set("player", 1)));
        std::vector<double> again;
        for (int i = 0; i < 40; ++i) {
            check(g.act("step"));
            again.push_back(g.state().f[1].pos.x.to_double());
        }
        check(again == walked, "the dummy walks as recorded");
        check_eq(g.seen().get("training")->get("dummy2")->as_string(), std::string_view("playing"));
        check(g.act("stop", Value{}.set("player", 1)));
        check(!g.act("replay", Value{}.set("player", 2)), "two players");
    });

    s.test("frames pinned: versus and training (boxes and the meter) at a jab's active frame", [] {
        std::vector<std::uint64_t> px;
        const auto dir = scratch_dir("fighting");
        for (std::string_view mode : {"versus", "training"}) {
            Game g(mode);
            check(g.act("pause"));
            check(g.act("press", pad(0, "6")));
            for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 90; ++i) check(g.act("step"));
            check(g.act("press", pad(0, "5LP")));
            for (int i = 0; i < 4; ++i) check(g.act("step"));
            g.h.step();
            px.push_back(g.pixels());
            check(g.h.save_png(dir / std::format("{}.png", mode)).has_value());
        }
        std::println("    fighting versus {:016x} training {:016x}", px[0], px[1]);
        check(px[0] != px[1], "training draws the boxes");
        // Pinned: GCC and LLVM, Linux, macOS and Windows draw these bytes.
        check(px == std::vector<std::uint64_t>{0x641fb19b539ee2a9ULL, 0x4e41359e45f64fffULL}, "pinned");
    });

    return s.run(argc, argv);
}
