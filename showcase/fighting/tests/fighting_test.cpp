// The fighting game as a player and an agent meet it: the match runs at 60
// frames a second on the runtime's clock (exactly, headless), the keyboard
// walks a fighter, an agent's pads land a jab, pause and step play one frame
// at a time, and the stage reads as a semantic node an agent can follow.
// Versus and training frames are pinned: the same pixels everywhere.
import std;
import openmui;
import openmui.showcase.fighting;
import openmui.testing;
import gongfu.ai;
import openmui.audio;

using namespace omui::testing;
using namespace omui;
namespace audio = omui::audio;

namespace {

struct Game {
    Headless h;
    explicit Game(std::string_view mode = "versus", std::string_view player1 = "", std::string_view player2 = "", float scale = 1, std::string_view arena = "",
                  bool overlays = true)
        : h{HeadlessOptions{.size = {960, 540}, .scale = scale}} {
        AppSpec spec = fighting::app();
        spec.setup(h.runtime());
        if (mode == "app") {  // the whole application: the title first
            h.mount("main", spec.view());
        } else {
            h.mount("main", fighting::stage({.id = "stage", .arena = std::string(arena), .mode = std::string(mode), .overlays = overlays, .player1 = std::string(player1),
                                             .player2 = std::string(player2), .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}));
        }
        h.step();
    }
    const fighting::Stage& stage() { return *h.runtime().component<fighting::Stage>(h.runtime().find("main")); }
    fighting::Stage& stage_mut() { return const_cast<fighting::Stage&>(stage()); }  // its sounds, rendered by the test
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
        check(r.names == std::vector<std::string>{"kata", "ke", "kun", "li", "mo", "ni", "yue"}, std::format("{}", r.names));
        for (const auto& [i, name] : {std::pair{0, "Kata"}, {1, "Ke"}, {2, "Kun"}, {3, "Li"}, {4, "Mo"}, {5, "Ni"}, {6, "Yue"}})
            check_eq(r.fighters[static_cast<std::size_t>(i)].def.name, std::string(name));
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
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 2; ++i) check(g.act("step"));
        // Held a while (a guard raised just before the hit would be a deflect); player 1 walks back in.
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 90; ++i) check(g.act("step"));
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

    s.test("墨 as designed: every move's startup, and its advantage on hit and on block, measured in the simulation (characters design §3)", [] {
        using gongfu::operator""_fx;
        const gongfu::Fighter& mo = fighting::roster().fighters[fighting::fighter_index("mo")];
        struct Row {
            const char* move;
            int         startup;
            std::optional<int> hit, block;
            bool        low = false;
        };
        // The design's table; the special's numbers are point blank.
        const std::vector<Row> table{
            {"5LP", 4, 4, -1},   {"5MP", 7, 2, -3},  {"5HP", 11, 1, -6},  {"5LK", 5, 3, -2},  {"5MK", 8, 1, -4},          {"5HK", 12, 0, -7},
            {"2LP", 4, 3, -1},   {"2MP", 6, 2, -2},  {"2HP", 8, 2, -8},   {"2LK", 5, 2, -2, true}, {"2MK", 8, 1, -5, true}, {"2HK", 10, {}, -12, true},
            {"236LP", 13, 1, -3}, {"236MP", 13, 0, -4}, {"236HP", 13, -2, -6}, {"623LP", 5, {}, -20}, {"623MP", 6, {}, -22}, {"623HP", 7, {}, -26},
        };
        // The pads that ask for a move, facing right: a motion's steps, then the press.
        auto pads = [](const gongfu::Command& c) {
            std::vector<gongfu::Pad> out;
            for (std::size_t k = 0; k + 1 < c.motion.length; ++k) out.push_back(gongfu::pad(c.motion.steps[k]));
            const int last = c.motion.length > 0 ? c.motion.steps[c.motion.length - 1] : c.direction != 0 ? c.direction : 5;
            out.push_back(gongfu::pad(last, static_cast<std::uint16_t>(c.buttons & -c.buttons)));  // one of its buttons
            return out;
        };
        for (const Row& row : table) {
            const std::int32_t m = mo.find(row.move);
            check(m >= 0, row.move);
            if (m < 0) continue;
            const gongfu::MoveDef& def = mo.def.moves[static_cast<std::size_t>(m)];
            check_eq(mo.startup(m), std::int32_t{row.startup});
            for (const bool block : {false, true}) {
                const std::optional<int>& want = block ? row.block : row.hit;
                if (!want) continue;
                gongfu::SystemDef sys;
                sys.round_frames = 1 << 20;
                gongfu::Match match(sys, mo, mo);
                gongfu::State st = match.state();
                // Close enough that the move meets the body on its first active frame.
                const gongfu::Box& reach = def.projectile ? gongfu::Box{def.projectile->offset.x, 0_fx, 0_fx, 0_fx} : def.hits[0].boxes[0];
                const gongfu::Fixed gap = gongfu::max(reach.x + reach.w / 2, 60_fx);
                const gongfu::Pad guard = block ? gongfu::pad(row.low ? 3 : 6) : gongfu::pad(5);  // facing left: back is 6
                // The guard held a while first (a guard raised just before the hit is a deflect, system 0.3).
                for (int f = 0; f < 12; ++f) match.tick(gongfu::pad(5), guard);
                st = match.state();
                st.f[0].pos.x = 900_fx;
                st.f[1].pos.x = 900_fx + gap;
                st.f[1].mode = st.f[1].mode == gongfu::Mode::crouch ? gongfu::Mode::crouch : gongfu::Mode::idle;
                match.restore(st);
                for (const gongfu::Pad p : pads(def.command)) match.tick(p, guard);
                int contact = -1;
                for (int f = 0; f < 60 && contact < 0; ++f) {
                    for (const gongfu::Event& e : match.events())
                        if (e.attacker == 0 && (e.kind == gongfu::Event::Kind::hit || e.kind == gongfu::Event::Kind::block || e.kind == gongfu::Event::Kind::counter))
                            contact = match.state().f[0].move_frame;
                    if (contact < 0) match.tick(gongfu::pad(5), guard);
                }
                check(contact == row.startup, std::format("{} {}: contact on frame {}", row.move, block ? "blocked" : "hit", contact));
                int attacker = -1, defender = -1;
                for (int f = 1; f < 120 && (attacker < 0 || defender < 0); ++f) {
                    match.tick(gongfu::pad(5), guard);
                    if (attacker < 0 && match.recovered(0)) attacker = f;
                    if (defender < 0 && match.recovered(1)) defender = f;
                }
                check(defender - attacker == *want, std::format("{} on {}: {:+} measured, {:+} designed", row.move, block ? "block" : "hit", defender - attacker, *want));
            }
        }
    });

    s.test("霓 as designed: every normal's, kick rush's and hop's startup and advantage, measured in the simulation (characters design §4)", [] {
        using gongfu::operator""_fx;
        const gongfu::Fighter& mo = fighting::roster().fighters[fighting::fighter_index("ni")];
        struct Row {
            const char* move;
            int         startup;
            std::optional<int> hit, block;
            bool        low = false;
        };
        // The design's table; the special's numbers are point blank.
        const std::vector<Row> table{
            {"5LP", 3, 4, -1},  {"5MP", 5, 3, -1}, {"5HP", 8, 3, -3},  {"5LK", 4, 3, -2},        {"5MK", 6, 2, -3},        {"5HK", 10, 2, -4},
            {"2LP", 3, 3, -1},  {"2MP", 5, 2, -2}, {"2HP", 7, {}, -9}, {"2LK", 4, 2, -2, true},  {"2MK", 7, 1, -4, true}, {"2HK", 9, {}, -11, true},
            {"6MK", 18, 4, -3}, {"236LK", 10, 2, -4}, {"236MK", 12, 2, -6}, {"236HK", 14, {}, -8},
        };
        // The pads that ask for a move, facing right: a motion's steps, then the press.
        auto pads = [](const gongfu::Command& c) {
            std::vector<gongfu::Pad> out;
            for (std::size_t k = 0; k + 1 < c.motion.length; ++k) out.push_back(gongfu::pad(c.motion.steps[k]));
            const int last = c.motion.length > 0 ? c.motion.steps[c.motion.length - 1] : c.direction != 0 ? c.direction : 5;
            out.push_back(gongfu::pad(last, static_cast<std::uint16_t>(c.buttons & -c.buttons)));  // one of its buttons
            return out;
        };
        for (const Row& row : table) {
            const std::int32_t m = mo.find(row.move);
            check(m >= 0, row.move);
            if (m < 0) continue;
            const gongfu::MoveDef& def = mo.def.moves[static_cast<std::size_t>(m)];
            check_eq(mo.startup(m), std::int32_t{row.startup});
            for (const bool block : {false, true}) {
                const std::optional<int>& want = block ? row.block : row.hit;
                if (!want) continue;
                gongfu::SystemDef sys;
                sys.round_frames = 1 << 20;
                gongfu::Match match(sys, mo, mo);
                gongfu::State st = match.state();
                // Close enough that the move meets the body on its first active frame.
                const gongfu::Box& reach = def.projectile ? gongfu::Box{def.projectile->offset.x, 0_fx, 0_fx, 0_fx} : def.hits[0].boxes[0];
                const gongfu::Fixed gap = gongfu::max(reach.x + reach.w / 2, 60_fx);
                const gongfu::Pad guard = block ? gongfu::pad(row.low ? 3 : 6) : gongfu::pad(5);  // facing left: back is 6
                // The guard held a while first (a guard raised just before the hit is a deflect, system 0.3).
                for (int f = 0; f < 12; ++f) match.tick(gongfu::pad(5), guard);
                st = match.state();
                st.f[0].pos.x = 900_fx;
                st.f[1].pos.x = 900_fx + gap;
                st.f[1].mode = st.f[1].mode == gongfu::Mode::crouch ? gongfu::Mode::crouch : gongfu::Mode::idle;
                match.restore(st);
                for (const gongfu::Pad p : pads(def.command)) match.tick(p, guard);
                int contact = -1;
                for (int f = 0; f < 60 && contact < 0; ++f) {
                    for (const gongfu::Event& e : match.events())
                        if (e.attacker == 0 && (e.kind == gongfu::Event::Kind::hit || e.kind == gongfu::Event::Kind::block || e.kind == gongfu::Event::Kind::counter))
                            contact = match.state().f[0].move_frame;
                    if (contact < 0) match.tick(gongfu::pad(5), guard);
                }
                check(contact == row.startup, std::format("{} {}: contact on frame {}", row.move, block ? "blocked" : "hit", contact));
                int attacker = -1, defender = -1;
                for (int f = 1; f < 120 && (attacker < 0 || defender < 0); ++f) {
                    match.tick(gongfu::pad(5), guard);
                    if (attacker < 0 && match.recovered(0)) attacker = f;
                    if (defender < 0 && match.recovered(1)) defender = f;
                }
                check(defender - attacker == *want, std::format("{} on {}: {:+} measured, {:+} designed", row.move, block ? "block" : "hit", defender - attacker, *want));
            }
        }
    });

    s.test("墨's home, the study: painted layers by parallax — moonlit mountains through a moon gate, the study's wall, floorboards catching the lanterns, a desk and a vase at its ends, blinds in front — pinned", [] {
        Game g("versus", "mo", "ni");
        g.h.advance(seconds(1.5));
        const auto dir = scratch_dir("fighting-study");
        check(g.h.save_png(dir / "study.png").has_value());
        const auto px = g.pixels();
        std::println("    study {:016x}", px);
        check_eq(px, std::uint64_t{0x2e7e5f4cf7ac822f});  // GCC and LLVM
    });

    s.test("霓's home, the street: a neon skyline, the alley's shopfronts and a noodle stall, the wet road giving the neon back, rain in front and behind — pinned", [] {
        Game g("versus", "ni", "mo");
        g.h.advance(seconds(1.5));
        check(g.h.save_png(scratch_dir("fighting-street") / "street.png").has_value());
        const auto px = g.pixels();
        std::println("    street {:016x}", px);
        check_eq(px, std::uint64_t{0x7db5b021a0092483});  // GCC and LLVM
    });

    s.test("坤's home, the rooftop: a dusk sky over the skyline, a water tower, a billboard and a fence hung with string lights, court lines on wet concrete — pinned", [] {
        Game g("versus", "kun", "mo");
        g.h.advance(seconds(1.5));
        check(g.h.save_png(scratch_dir("fighting-rooftop") / "rooftop.png").has_value());
        const auto px = g.pixels();
        std::println("    rooftop {:016x}", px);
        check_eq(px, std::uint64_t{0xc14da58397c88742});  // GCC and LLVM
    });

    s.test("璃's home, the greenhouse: a misty garden through bending panes, white iron arches with palms and hanging ferns, wet tile catching the light, a fountain and a stand of terrariums — pinned with 墨 and 霓 on it", [] {
        Game g("versus", "mo", "ni", 1, "greenhouse");
        g.h.advance(seconds(1.5));
        check(g.h.save_png(scratch_dir("fighting-greenhouse") / "greenhouse.png").has_value());
        const auto px = g.pixels();
        std::println("    greenhouse {:016x}", px);
        check_eq(px, std::uint64_t{0xc42c1b08be02b5d3});  // GCC and LLVM
    });

    s.test("岳's home, the gate: a sea of clouds at sunset framed by a stone gateway, guardian lions and banners, worn granite, an incense cauldron and a great bell — pinned with 墨 and 霓 on it", [] {
        Game g("versus", "mo", "ni", 1, "gate");
        g.h.advance(seconds(1.5));
        check(g.h.save_png(scratch_dir("fighting-gate") / "gate.png").has_value());
        const auto px = g.pixels();
        std::println("    gate {:016x}", px);
        check_eq(px, std::uint64_t{0x6ea25c8b53fdc4dc});  // GCC and LLVM
    });

    s.test("刻's home, the clock: the back of a glowing dial, gears and a pendulum over a catwalk, a brass compass in the floorboards, a clockmaker's bench and a grandfather clock — pinned with 墨 and 霓 on it", [] {
        Game g("versus", "mo", "ni", 1, "clock");
        g.h.advance(seconds(1.5));
        check(g.h.save_png(scratch_dir("fighting-clock") / "clock.png").has_value());
        const auto px = g.pixels();
        std::println("    clock {:016x}", px);
        check_eq(px, std::uint64_t{0x36396d0061ab4b08});  // GCC and LLVM
    });

    s.test("the README's loop: 墨's level 3 in her study, from the flash's cut-in to the finishing blow, an animated WebP under 1 MB", [] {
        Game g("training", "mo", "ni", 0.5f, "study", false);  // 480 × 270: a README's width; training's meter, not its overlays
        check(g.act("pause"));
        check(g.act("fill"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 110; ++i) check(g.act("step"));
        for (std::string_view p : {"2", "3", "6", "2", "3", "6HK"}) {
            check(g.act("press", pad(0, p)));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 260; ++i) {
            check(g.act("step"));
            if (i % 5 == 0) {  // 12 frames a second of the match's 60
                g.h.step();
                g.h.record();
            }
        }
        const auto file = scratch_dir("readme-fighting") / "mo-level-3.webp";
        check(g.h.save_recording(file, {.delay_num = 1, .delay_den = 12, .quality = 82}).has_value());
        const auto size = std::filesystem::file_size(file);
        std::println("    the loop: {} KB", size / 1024);
        check(size < 1024 * 1024, std::format("{} bytes", size));
    });

    s.test("墨's level 3: her palm, the wrist taken, four strikes of the free hand each leaving its stroke of ink, both palms; the brush drawn for one great stroke, 一, through them, and the fall — frames pinned", [] {
        Game g("training", "mo", "ni");
        check(g.act("pause"));
        check(g.act("fill"));
        // In range of the palm, then 236236K.
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 110; ++i) check(g.act("step"));
        for (std::string_view p : {"2", "3", "6", "2", "3", "6HK"}) {
            check(g.act("press", pad(0, p)));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, "5")));
        const auto dir = scratch_dir("fighting-mo-level3");
        // Each beat at the frame that tells it.
        const std::vector<std::pair<std::int32_t, std::string_view>> beats{{14, "wrist"}, {29, "palm"},  {41, "fingers"}, {53, "knife"},  {65, "chop"},  {77, "both-palms"},
                                                                           {92, "draw"},  {108, "coil"}, {117, "great-stroke"}, {136, "thrown"}, {153, "fall"}, {170, "finisher"}};
        std::vector<std::uint64_t> px;
        std::int32_t lock_from = -1;
        for (int i = 0; i < 320; ++i) {
            check(g.act("step"));
            const auto& st = g.state();
            if (st.lock.owner == 0 && lock_from < 0) lock_from = st.frame;
            const std::int32_t lf = st.lock.owner == 0 ? st.lock.frame : -1;
            const auto beat = std::ranges::find(beats, lf, &std::pair<std::int32_t, std::string_view>::first);
            if (beat == beats.end()) continue;
            g.h.step();
            px.push_back(g.pixels());
            check(g.h.save_png(dir / std::format("{:02}-{}-{}.png", px.size(), lf, beat->second)).has_value());
        }
        check(lock_from > 0, "the palm hit: the cinematic played");
        check_eq(px.size(), beats.size());
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    mo level 3 {}", all.hex());
        check_eq(all.hex(), std::string("19235ebe402ee533"));  // GCC and LLVM, every platform
    });

    s.test("墨's ink: waves of three sizes flung from the brush, and her level 2's tide along the floor — frames pinned", [] {
        const auto dir = scratch_dir("fighting-mo-glyphs");
        Hash all;
        int saved = 0;
        for (const auto& [name, steps] : std::vector<std::pair<std::string, std::vector<std::string>>>{
                 {"236LP", {"2", "3", "6LP"}}, {"236MP", {"2", "3", "6MP"}}, {"236HP", {"2", "3", "6HP"}}, {"214214P", {"2", "1", "4", "2", "1", "4LP"}}}) {
            Game g("training", "mo", "kata");
            check(g.act("pause"));
            check(g.act("fill"));
            for (const auto& p : steps) {
                check(g.act("press", pad(0, p)));
                check(g.act("step"));
            }
            check(g.act("press", pad(0, "5")));
            for (int i = 0; i < 100; ++i) {  // a level 2's flash first
                check(g.act("step"));
                const auto& f = g.state().f[0];
                const bool writing = f.mode == gongfu::Mode::move && f.move_frame == 8 && g.state().freeze == 0;
                const bool flying = std::ranges::any_of(g.state().p, [](const gongfu::ProjectileState& p) { return p.alive && p.frame == 14; });
                if (writing || flying) {
                    g.h.step();
                    all.u64(g.pixels());
                    check(g.h.save_png(dir / std::format("{}-{}.png", name, writing ? "writing" : "flying")).has_value());
                    ++saved;
                }
            }
        }
        check_eq(saved, 8);
        std::println("    mo glyphs {}", all.hex());
        check_eq(all.hex(), std::string("6b7db49871f74060"));  // GCC and LLVM, every platform
    });

    s.test("霓's afterimages and her level 3: the collar taken, two knees to the gut and a headbutt while held, heaved overhead, four kicks in the air, the axe kick into the street — frames pinned", [] {
        Game g("training", "ni", "kun");
        check(g.act("pause"));
        check(g.act("fill"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 120; ++i) check(g.act("step"));
        const auto dir = scratch_dir("fighting-ni-level3");
        std::vector<std::uint64_t> px;
        // A kick rush first: her afterimages.
        for (std::string_view p : {"2", "3", "6MK"}) {
            check(g.act("press", pad(0, p)));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 13; ++i) check(g.act("step"));
        g.h.step();
        px.push_back(g.pixels());
        check(g.h.save_png(dir / "00-afterimages.png").has_value());
        for (int i = 0; i < 60; ++i) check(g.act("step"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 100; ++i) check(g.act("step"));
        for (std::string_view p : {"2", "3", "6", "2", "3", "6HK"}) {
            check(g.act("press", pad(0, p)));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, "5")));
        // Each beat at the frame that tells it.
        const std::vector<std::pair<std::int32_t, std::string_view>> beats{{14, "collar"},  {29, "knee"},       {43, "knee-face"},  {59, "headbutt"}, {72, "heave"},  {101, "kick"},
                                                                           {113, "kick-high"}, {119, "spin-kick"}, {133, "axe-raised"}, {141, "axe"}, {148, "street"}, {162, "finisher"}};
        std::int32_t lock_from = -1;
        for (int i = 0; i < 300; ++i) {
            check(g.act("step"));
            const auto& st = g.state();
            if (st.lock.owner == 0 && lock_from < 0) lock_from = st.frame;
            const std::int32_t lf = st.lock.owner == 0 ? st.lock.frame : -1;
            const auto beat = std::ranges::find(beats, lf, &std::pair<std::int32_t, std::string_view>::first);
            if (beat == beats.end()) continue;
            g.h.step();
            px.push_back(g.pixels());
            check(g.h.save_png(dir / std::format("{:02}-{}-{}.png", px.size() - 1, lf, beat->second)).has_value());
        }
        check(lock_from > 0, "the straight hit: the cinematic played");
        check_eq(px.size(), beats.size() + 1);
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    ni level 3 {}", all.hex());
        check_eq(all.hex(), std::string("7b451696a6e5a1e6"));  // GCC and LLVM, every platform
    });

    s.test("坤's level 3: the ball spun on a finger, the bounce pass into the gut, the glide and 铁山靠, the catch behind his back as they crash down behind him — frames pinned", [] {
        Game g("training", "kun", "mo");
        check(g.act("pause"));
        check(g.act("fill"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 110; ++i) check(g.act("step"));
        for (std::string_view p : {"2", "3", "6", "2", "3", "6HK"}) {
            check(g.act("press", pad(0, p)));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, "5")));
        const auto dir = scratch_dir("fighting-kun-level3");
        // Each beat at the frame that tells it.
        const std::vector<std::pair<std::int32_t, std::string_view>> beats{{24, "spin"},  {51, "coil"},   {57, "bounce"}, {63, "gut"},   {92, "stamp"},
                                                                           {95, "kao"},   {111, "launched"}, {141, "catch"},  {147, "crash"}, {159, "finisher"}};
        std::vector<std::uint64_t> px;
        std::int32_t lock_from = -1;
        for (int i = 0; i < 300; ++i) {
            check(g.act("step"));
            const auto& st = g.state();
            if (st.lock.owner == 0 && lock_from < 0) lock_from = st.frame;
            const std::int32_t lf = st.lock.owner == 0 ? st.lock.frame : -1;
            const auto beat = std::ranges::find(beats, lf, &std::pair<std::int32_t, std::string_view>::first);
            if (beat == beats.end()) continue;
            g.h.step();
            px.push_back(g.pixels());
            check(g.h.save_png(dir / std::format("{:02}-{}-{}.png", px.size(), lf, beat->second)).has_value());
        }
        check(lock_from > 0, "the elbow hit: the cinematic played");
        check_eq(px.size(), beats.size());
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    kun level 3 {}", all.hex());
        check_eq(all.hex(), std::string("4f53a8510723272a"));  // GCC and LLVM, every platform
    });

    s.test("岳 as designed: every normal's and elbow's startup and advantage, measured in the simulation (characters 2 design §3)", [] {
        using gongfu::operator""_fx;
        const gongfu::Fighter& yue = fighting::roster().fighters[fighting::fighter_index("yue")];
        struct Row {
            const char* move;
            int         startup;
            std::optional<int> hit, block;
            bool        low = false;
        };
        const std::vector<Row> table{
            {"5LP", 5, 3, -2},   {"5MP", 8, 2, -4},   {"5HP", 12, 1, -8},   {"5LK", 6, 2, -3},        {"5MK", 9, 1, -5},         {"5HK", 14, {}, -10},
            {"2LP", 5, 2, -2},   {"2MP", 8, 2, -4},   {"2HP", 11, {}, -10}, {"2LK", 6, 1, -3, true},  {"2MK", 10, 0, -6, true}, {"2HK", 13, {}, -14, true},
            {"6HP", 22, 3, -2},  {"214LK", 16, {}, -4}, {"214MK", 18, {}, -2}, {"214HK", 20, {}, 0},
        };
        auto pads = [](const gongfu::Command& c) {
            std::vector<gongfu::Pad> out;
            for (std::size_t k = 0; k + 1 < c.motion.length; ++k) out.push_back(gongfu::pad(c.motion.steps[k]));
            const int last = c.motion.length > 0 ? c.motion.steps[c.motion.length - 1] : c.direction != 0 ? c.direction : 5;
            out.push_back(gongfu::pad(last, static_cast<std::uint16_t>(c.buttons & -c.buttons)));
            return out;
        };
        for (const Row& row : table) {
            const std::int32_t m = yue.find(row.move);
            check(m >= 0, row.move);
            if (m < 0) continue;
            const gongfu::MoveDef& def = yue.def.moves[static_cast<std::size_t>(m)];
            check_eq(yue.startup(m), std::int32_t{row.startup});
            for (const bool block : {false, true}) {
                const std::optional<int>& want = block ? row.block : row.hit;
                if (!want) continue;
                gongfu::SystemDef sys;
                sys.round_frames = 1 << 20;
                gongfu::Match match(sys, yue, yue);
                const gongfu::Pad guard = block ? gongfu::pad(row.low ? 3 : 6) : gongfu::pad(5);
                for (int f = 0; f < 12; ++f) match.tick(gongfu::pad(5), guard);
                gongfu::State st = match.state();
                // The elbow walks in before it lands: started from farther away.
                const gongfu::Fixed gap = gongfu::max(def.hits[0].boxes[0].x + def.hits[0].boxes[0].w / 2, 60_fx) + (def.steps.empty() ? 0_fx : 40_fx);
                st.f[0].pos.x = 900_fx;
                st.f[1].pos.x = 900_fx + gap;
                st.f[1].mode = st.f[1].mode == gongfu::Mode::crouch ? gongfu::Mode::crouch : gongfu::Mode::idle;
                match.restore(st);
                for (const gongfu::Pad p : pads(def.command)) match.tick(p, guard);
                int contact = -1;
                for (int f = 0; f < 60 && contact < 0; ++f) {
                    for (const gongfu::Event& e : match.events())
                        if (e.attacker == 0 && (e.kind == gongfu::Event::Kind::hit || e.kind == gongfu::Event::Kind::block || e.kind == gongfu::Event::Kind::counter))
                            contact = match.state().f[0].move_frame;
                    if (contact < 0) match.tick(gongfu::pad(5), guard);
                }
                check(contact == row.startup, std::format("{} {}: contact on frame {}", row.move, block ? "blocked" : "hit", contact));
                int attacker = -1, defender = -1;
                for (int f = 1; f < 120 && (attacker < 0 || defender < 0); ++f) {
                    match.tick(gongfu::pad(5), guard);
                    if (attacker < 0 && match.recovered(0)) attacker = f;
                    if (defender < 0 && match.recovered(1)) defender = f;
                }
                check(defender - attacker == *want, std::format("{} on {}: {:+} measured, {:+} designed", row.move, block ? "block" : "hit", defender - attacker, *want));
            }
        }
    });

    s.test("岳's level 3, 「泰山压顶」: a 720 out of blockstun, the leap into the clouds, the mountain coming down — frames pinned", [] {
        Game g("training", "yue", "kata");
        check(g.act("pause"));
        check(g.act("fill"));
        // Kata walks in; Yue guards a while (not a deflect), then blocks a heavy and turns the stick twice round in its blockstun.
        check(g.act("press", pad(1, "4")));
        for (int i = 0; i < 200 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 60; ++i) check(g.act("step"));
        check(g.act("press", pad(0, "4")));
        for (int i = 0; i < 12; ++i) check(g.act("step"));
        check(g.act("press", pad(1, "5")));
        check(g.act("press", pad(1, "5HP")));
        check(g.act("step"));
        check(g.act("press", pad(1, "5")));
        for (int i = 0; i < 20 && g.state().f[0].mode != gongfu::Mode::blockstun; ++i) check(g.act("step"));
        check(g.state().f[0].mode == gongfu::Mode::blockstun, std::format("Yue blocks the heavy: gap {}, mode {}", (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double(), static_cast<int>(g.state().f[0].mode)));
        // Round and round until the blockstun is about to end, then a kick: the 720 comes out on the first frame he can act.
        const std::array<std::string_view, 8> circle{"6", "3", "2", "1", "4", "7", "8", "9"};
        std::size_t k = 0;
        for (; k < 60 && (k < 15 || g.state().f[0].stun > 1); ++k) {
            check(g.act("press", pad(0, circle[k % 8])));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, std::string(circle[k % 8]) + "LK")));
        check(g.act("step"));
        check(g.act("press", pad(0, "5")));
        const auto dir = scratch_dir("fighting-yue-level3");
        std::vector<std::uint64_t> px;
        std::int32_t lock_from = -1;
        for (int i = 0; i < 320; ++i) {
            check(g.act("step"));
            const auto& st = g.state();
            if (st.lock.owner == 0 && lock_from < 0) lock_from = st.frame;
            const std::int32_t lf = st.lock.owner == 0 ? st.lock.frame : -1;
            if (st.freeze == 25 || lf == 30 || lf == 100 || lf == 124 || lf == 150 || lf == 162 || lf == 178) {
                g.h.step();
                px.push_back(g.pixels());
                check(g.h.save_png(dir / std::format("{:02}-{}.png", px.size(), lf < 0 ? std::string("flash") : std::format("lock-{}", lf))).has_value());
            }
        }
        check(lock_from > 0, "the grab took: the cinematic played");
        check_eq(px.size(), std::size_t{7});
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    yue level 3 {}", all.hex());
        check_eq(all.hex(), std::string("e6e10769896fdb3d"));  // GCC and LLVM, every platform
    });

    s.test("璃 as designed: every normal's and 「棱光」's startup and advantage, measured in the simulation (characters 2 design §2)", [] {
        using gongfu::operator""_fx;
        const gongfu::Fighter& li = fighting::roster().fighters[fighting::fighter_index("li")];
        struct Row {
            const char* move;
            int         startup;
            std::optional<int> hit, block;
            bool        low = false;
        };
        const std::vector<Row> table{
            {"5LP", 4, 3, -2},  {"5MP", 6, 2, -3},  {"5HP", 9, 1, -6},   {"5LK", 5, 2, -2},        {"5MK", 7, 1, -4},         {"5HK", 11, 0, -7},
            {"2LP", 4, 3, -1},  {"2MP", 6, 2, -2},  {"2HP", 9, {}, -8},  {"2LK", 5, 2, -2, true},  {"2MK", 8, 1, -5, true},  {"2HK", 10, {}, -12, true},
            {"6HP", 20, 2, -4}, {"623LK", 5, {}, -22}, {"623MK", 6, {}, -24}, {"623HK", 7, {}, -26},
        };
        auto pads = [](const gongfu::Command& c) {
            std::vector<gongfu::Pad> out;
            for (std::size_t k = 0; k + 1 < c.motion.length; ++k) out.push_back(gongfu::pad(c.motion.steps[k]));
            const int last = c.motion.length > 0 ? c.motion.steps[c.motion.length - 1] : c.direction != 0 ? c.direction : 5;
            out.push_back(gongfu::pad(last, static_cast<std::uint16_t>(c.buttons & -c.buttons)));
            return out;
        };
        for (const Row& row : table) {
            const std::int32_t m = li.find(row.move);
            check(m >= 0, row.move);
            if (m < 0) continue;
            const gongfu::MoveDef& def = li.def.moves[static_cast<std::size_t>(m)];
            check_eq(li.startup(m), std::int32_t{row.startup});
            for (const bool block : {false, true}) {
                const std::optional<int>& want = block ? row.block : row.hit;
                if (!want) continue;
                gongfu::SystemDef sys;
                sys.round_frames = 1 << 20;
                gongfu::Match match(sys, li, li);
                const gongfu::Pad guard = block ? gongfu::pad(row.low ? 3 : 6) : gongfu::pad(5);
                for (int f = 0; f < 12; ++f) match.tick(gongfu::pad(5), guard);
                gongfu::State st = match.state();
                st.f[0].pos.x = 900_fx;
                st.f[1].pos.x = 900_fx + gongfu::max(def.hits[0].boxes[0].x + def.hits[0].boxes[0].w / 2, 60_fx);
                st.f[1].mode = st.f[1].mode == gongfu::Mode::crouch ? gongfu::Mode::crouch : gongfu::Mode::idle;
                match.restore(st);
                for (const gongfu::Pad p : pads(def.command)) match.tick(p, guard);
                int contact = -1;
                for (int f = 0; f < 60 && contact < 0; ++f) {
                    for (const gongfu::Event& e : match.events())
                        if (e.attacker == 0 && (e.kind == gongfu::Event::Kind::hit || e.kind == gongfu::Event::Kind::block || e.kind == gongfu::Event::Kind::counter))
                            contact = match.state().f[0].move_frame;
                    if (contact < 0) match.tick(gongfu::pad(5), guard);
                }
                check(contact == row.startup, std::format("{} {}: contact on frame {}", row.move, block ? "blocked" : "hit", contact));
                int attacker = -1, defender = -1;
                for (int f = 1; f < 120 && (attacker < 0 || defender < 0); ++f) {
                    match.tick(gongfu::pad(5), guard);
                    if (attacker < 0 && match.recovered(0)) attacker = f;
                    if (defender < 0 && match.recovered(1)) defender = f;
                }
                check(defender - attacker == *want, std::format("{} on {}: {:+} measured, {:+} designed", row.move, block ? "block" : "hit", defender - attacker, *want));
            }
        }
    });

    s.test("璃's panes in play: Kata's fireball stopped by one, her palm shattering it into shards that hit him, her gallery turning a fireball back — frames pinned", [] {
        Game g("training", "li", "kata");
        check(g.act("pause"));
        check(g.act("fill"));
        const auto dir = scratch_dir("fighting-li-panes");
        std::vector<std::uint64_t> px;
        auto shot = [&](std::string_view name) {
            g.h.step();
            px.push_back(g.pixels());
            check(g.h.save_png(dir / std::format("{:02}-{}.png", px.size(), name)).has_value());
        };
        auto press = [&](int player, std::initializer_list<std::string_view> steps) {
            for (std::string_view p : steps) {
                check(g.act("press", pad(player, p)));
                check(g.act("step"));
            }
            check(g.act("press", pad(player, "5")));
        };
        auto count = [&] { return std::ranges::count_if(g.state().pieces, [](const gongfu::PieceState& q) { return q.alive != 0; }); };
        // A pane at 220; Kata (facing left: 214 on the screen) throws his fireball into it.
        press(0, {"2", "3", "6MP"});
        for (int i = 0; i < 30; ++i) check(g.act("step"));
        check(count() == 1, "a pane stands");
        press(1, {"2", "1", "4LP"});
        bool stopped = false;
        for (int i = 0; i < 90 && !stopped; ++i) {
            check(g.act("step"));
            stopped = g.state().pieces[0].durability == 1;
        }
        check(stopped && g.state().f[0].health == 9800, "stopped: the pane cracked, she untouched");
        shot("stopped");
        // She walks to it and strikes it: three shards fly at Kata.
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().pieces[0].pos.x - g.state().f[0].pos.x).to_double() > 60; ++i) check(g.act("step"));
        press(0, {"5MP"});
        int flying = 0;
        for (int i = 0; i < 12; ++i) {
            check(g.act("step"));
            flying = std::max<int>(flying, static_cast<int>(std::ranges::count_if(g.state().p, [](const gongfu::ProjectileState& p) { return p.alive != 0; })));
            if (i == 5) shot("shards");
        }
        check(flying == 3, std::format("{} shards", flying));
        for (int i = 0; i < 80; ++i) check(g.act("step"));
        check(g.state().f[1].health < 10000, "the shards hit");
        // Apart again; her gallery; Kata's fireball turned back.
        check(g.act("press", pad(0, "4")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() < 520; ++i) check(g.act("step"));
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 30; ++i) check(g.act("step"));
        check(g.act("fill"));
        press(0, {"2", "3", "6", "2", "3", "6LP"});
        for (int i = 0; i < 90; ++i) check(g.act("step"));
        check(count() == 3, std::format("{} panes of the gallery", count()));
        shot("gallery");
        const int before = g.state().f[1].health;
        press(1, {"2", "1", "4LP"});
        bool turned = false;
        for (int i = 0; i < 120; ++i) {
            check(g.act("step"));
            if (!turned && std::ranges::any_of(g.state().p, [](const gongfu::ProjectileState& p) { return p.alive && p.reflected; })) {
                turned = true;
                shot("turned");
            }
        }
        check(turned && g.state().f[1].health < before, "turned back, and it hit its thrower");
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    li panes {}", all.hex());
        check_eq(all.hex(), std::string("4eeb2102a9cc3279"));  // GCC and LLVM, every platform
    });

    s.test("璃's level 3, 「琉璃界」: her eyes through cracking glass; panes about them as the world turns to glass; strikes from in front and from behind through her panes, each breaking glass and leaving a crack; the palm through the last pane, everything falling, her finishing picture — frames pinned", [] {
        Game g("training", "li", "kata");
        check(g.act("pause"));
        check(g.act("fill"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 100; ++i) check(g.act("step"));
        for (std::string_view p : {"2", "3", "6", "2", "3", "6HK"}) {
            check(g.act("press", pad(0, p)));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, "5")));
        const auto dir = scratch_dir("fighting-li-level3");
        std::vector<std::uint64_t> px;
        std::int32_t lock_from = -1;
        for (int i = 0; i < 300; ++i) {
            check(g.act("step"));
            const auto& st = g.state();
            if (st.lock.owner == 0 && lock_from < 0) lock_from = st.frame;
            const std::int32_t lf = st.lock.owner == 0 ? st.lock.frame : -1;
            if (st.freeze == 25 || lf == 24 || lf == 51 || lf == 110 || lf == 141 || lf == 152 || lf == 158 || lf == 168) {
                g.h.step();
                px.push_back(g.pixels());
                check(g.h.save_png(dir / std::format("{:02}-{}.png", px.size(), lf < 0 ? std::string("flash") : std::format("lock-{}", lf))).has_value());
            }
        }
        check(lock_from > 0, "the palm hit: the cinematic played");
        check_eq(px.size(), std::size_t{8});
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    li level 3 {}", all.hex());
        check_eq(all.hex(), std::string("77c216e082c09f43"));  // GCC and LLVM, every platform
    });

    s.test("刻 as designed: every normal's, 「停摆」's and 「拨针」's startup and advantage, measured in the simulation (characters 2 design §4)", [] {
        using gongfu::operator""_fx;
        const gongfu::Fighter& ke = fighting::roster().fighters[fighting::fighter_index("ke")];
        struct Row {
            const char* move;
            int         startup;
            std::optional<int> hit, block;
            bool        low = false;
        };
        const std::vector<Row> table{
            {"5LP", 4, 4, -1},  {"5MP", 6, 2, -2}, {"5HP", 9, 2, -5},   {"5LK", 5, 3, -2},        {"5MK", 7, 1, -4},         {"5HK", 11, 1, -5},
            {"2LP", 4, 3, -1},  {"2MP", 6, 2, -2}, {"2HP", 8, {}, -9},  {"2LK", 5, 2, -2, true},  {"2MK", 8, 1, -4, true},  {"2HK", 10, {}, -12, true},
            {"214K", 9, 22, -6}, {"623P", 5, {}, -24},
        };
        auto pads = [](const gongfu::Command& c) {
            std::vector<gongfu::Pad> out;
            for (std::size_t k = 0; k + 1 < c.motion.length; ++k) out.push_back(gongfu::pad(c.motion.steps[k]));
            const int last = c.motion.length > 0 ? c.motion.steps[c.motion.length - 1] : c.direction != 0 ? c.direction : 5;
            out.push_back(gongfu::pad(last, static_cast<std::uint16_t>(c.buttons & -c.buttons)));
            return out;
        };
        for (const Row& row : table) {
            const std::int32_t m = ke.find(row.move);
            check(m >= 0, row.move);
            if (m < 0) continue;
            const gongfu::MoveDef& def = ke.def.moves[static_cast<std::size_t>(m)];
            check_eq(ke.startup(m), std::int32_t{row.startup});
            for (const bool block : {false, true}) {
                const std::optional<int>& want = block ? row.block : row.hit;
                if (!want) continue;
                gongfu::SystemDef sys;
                sys.round_frames = 1 << 20;
                gongfu::Match match(sys, ke, ke);
                const gongfu::Pad guard = block ? gongfu::pad(row.low ? 3 : 6) : gongfu::pad(5);
                for (int f = 0; f < 12; ++f) match.tick(gongfu::pad(5), guard);
                gongfu::State st = match.state();
                st.f[0].pos.x = 900_fx;
                st.f[1].pos.x = 900_fx + gongfu::max(def.hits[0].boxes[0].x + def.hits[0].boxes[0].w / 2, 60_fx);
                st.f[1].mode = st.f[1].mode == gongfu::Mode::crouch ? gongfu::Mode::crouch : gongfu::Mode::idle;
                match.restore(st);
                for (const gongfu::Pad p : pads(def.command)) match.tick(p, guard);
                int contact = -1;
                for (int f = 0; f < 60 && contact < 0; ++f) {
                    for (const gongfu::Event& e : match.events())
                        if (e.attacker == 0 && (e.kind == gongfu::Event::Kind::hit || e.kind == gongfu::Event::Kind::block || e.kind == gongfu::Event::Kind::counter))
                            contact = match.state().f[0].move_frame;
                    if (contact < 0) match.tick(gongfu::pad(5), guard);
                }
                check(contact == row.startup, std::format("{} {}: contact on frame {}", row.move, block ? "blocked" : "hit", contact));
                int attacker = -1, defender = -1;
                for (int f = 1; f < 120 && (attacker < 0 || defender < 0); ++f) {
                    match.tick(gongfu::pad(5), guard);
                    if (attacker < 0 && match.recovered(0)) attacker = f;
                    if (defender < 0 && match.recovered(1)) defender = f;
                }
                check(defender - attacker == *want, std::format("{} on {}: {:+} measured, {:+} designed", row.move, block ? "block" : "hit", defender - attacker, *want));
            }
        }
    });

    s.test("刻's hand comes back through Kata; her clone plays her last two seconds; a rewind leaves its ghosts — frames pinned", [] {
        Game g("training", "ke", "kata");
        check(g.act("pause"));
        check(g.act("fill"));
        const auto dir = scratch_dir("fighting-ke-time");
        std::vector<std::uint64_t> px;
        auto shot = [&](std::string_view name) {
            g.h.step();
            px.push_back(g.pixels());
            check(g.h.save_png(dir / std::format("{:02}-{}.png", px.size(), name)).has_value());
        };
        auto press = [&](int player, std::initializer_list<std::string_view> steps) {
            for (std::string_view p : steps) {
                check(g.act("press", pad(player, p)));
                check(g.act("step"));
            }
            check(g.act("press", pad(player, "5")));
        };
        // The hand (236LP, back on its 30th frame): through Kata going, and again coming back.
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 200; ++i) check(g.act("step"));
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 12; ++i) check(g.act("step"));  // let go a while: forward again would be a dash (66)
        const int before = g.state().f[1].health;
        press(0, {"2", "3", "6LP"});
        int hits = 0;
        bool back = false;
        for (int i = 0; i < 120; ++i) {
            check(g.act("step"));
            for (const auto& p : g.state().p)
                if (p.alive && p.turned && !back) {
                    back = true;
                    shot("coming-back");
                }
            hits += static_cast<int>(std::ranges::count_if(g.stage().match().events(), [](const gongfu::Event& e) { return e.kind == gongfu::Event::Kind::hit && e.attacker == 0; }));
        }
        check(back && hits == 2 && g.state().f[1].health < before, std::format("{} hits", hits));
        // A mark, a walk, the rewind: ghosts along the way back.
        press(0, {"2", "5", "2LP"});
        for (int i = 0; i < 24; ++i) check(g.act("step"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 50; ++i) check(g.act("step"));
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 12; ++i) check(g.act("step"));
        press(0, {"2", "3", "6", "2", "3", "6LP"});
        bool rewound = false;
        for (int i = 0; i < 60 && !rewound; ++i) {
            check(g.act("step"));
            rewound = std::ranges::any_of(g.stage().match().events(), [](const gongfu::Event& e) { return e.kind == gongfu::Event::Kind::rewind; });
        }
        check(rewound, "rewound to the mark");
        for (int i = 0; i < 4; ++i) check(g.act("step"));
        shot("rewind");
        // The clone: her last 120 frames played again from where she stood.
        check(g.act("fill"));
        for (int i = 0; i < 30; ++i) check(g.act("step"));
        press(0, {"2", "1", "4", "2", "1", "4LP"});
        bool out = false;
        for (int i = 0; i < 60 && !out; ++i) {
            check(g.act("step"));
            out = g.state().clones[0].alive != 0;
        }
        check(out, "the clone is out");
        for (int i = 0; i < 20; ++i) check(g.act("step"));
        shot("clone");
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    ke time {}", all.hex());
        check_eq(all.hex(), std::string("ece7223b3c45b615"));  // GCC and LLVM, every platform
    });

    s.test("刻's level 3, 「零时」: time stops — the picture grey, a clock's hands standing, ten hits' sparks hanging — then the bell — frames pinned", [] {
        Game g("training", "ke", "kata");
        check(g.act("pause"));
        check(g.act("fill"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 110; ++i) check(g.act("step"));
        for (std::string_view p : {"2", "3", "6", "2", "3", "6HK"}) {
            check(g.act("press", pad(0, p)));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, "5")));
        const auto dir = scratch_dir("fighting-ke-level3");
        std::vector<std::uint64_t> px;
        std::int32_t lock_from = -1;
        for (int i = 0; i < 320; ++i) {
            check(g.act("step"));
            const auto& st = g.state();
            if (st.lock.owner == 0 && lock_from < 0) lock_from = st.frame;
            const std::int32_t lf = st.lock.owner == 0 ? st.lock.frame : -1;
            if (st.freeze == 25 || lf == 24 || lf == 75 || lf == 135 || lf == 151 || lf == 160 || lf == 180) {
                g.h.step();
                px.push_back(g.pixels());
                check(g.h.save_png(dir / std::format("{:02}-{}.png", px.size(), lf < 0 ? std::string("flash") : std::format("lock-{}", lf))).has_value());
            }
        }
        check(lock_from > 0, "the stab hit: the cinematic played");
        check_eq(px.size(), std::size_t{7});
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    ke level 3 {}", all.hex());
        check_eq(all.hex(), std::string("17d136ff5d9f4e3a"));  // GCC and LLVM, every platform
    });

    s.test("each fighter's win pose: a round won on the clock, the winner standing to play it out at its beats — frames pinned", [] {
        // Who wins, against whom, and the win clip's frames (from the round's end) that tell it.
        struct Win {
            std::string_view           who, against;
            std::vector<std::int32_t>  beats;
        };
        const std::vector<Win> wins{{"mo", "kata", {12, 30, 52, 110}}, {"ni", "kata", {9, 27, 48, 110}}, {"kun", "kata", {13, 25, 34, 110}},
                                    {"li", "kata", {12, 34, 70, 110}},  {"yue", "kata", {18, 25, 62, 110}}, {"ke", "kata", {30, 48, 80, 110}},
                                    {"kata", "mo", {14, 40, 100}}};
        const auto dir = scratch_dir("fighting-wins");
        Hash all;
        for (const Win& w : wins) {
            Game g("versus", w.who, w.against);
            check(g.act("pause"));
            for (int i = 0; i < 100; ++i) check(g.act("step"));
            // The clock at its last frames, player 2 behind on health: the round to player 1 when it runs out.
            auto& match = const_cast<gongfu::Match&>(g.stage().match());
            gongfu::State st = match.state();
            st.timer = 2, st.f[1].health = 5000;
            match.restore(st);
            std::int32_t ended = -1;
            for (int i = 0; i < 20 && ended < 0; ++i) {
                check(g.act("step"));
                if (g.state().over) ended = g.state().frame;
            }
            check(ended > 0 && g.state().winner == 0, std::format("{}: the round to player 1", w.who));
            for (const std::int32_t beat : w.beats) {
                for (int i = 0; i < 400 && g.state().frame - ended < beat; ++i) check(g.act("step"));
                check_eq(g.state().frame - ended, beat);
                g.h.step();
                const std::uint64_t px = g.pixels();
                all.u64(px);
                check(g.h.save_png(dir / std::format("{}-{:03}.png", w.who, beat)).has_value());
            }
        }
        std::println("    wins {}", all.hex());
        check_eq(all.hex(), std::string("47b9e9e3cd117a8f"));  // GCC and LLVM, every platform
    });

    s.test("training reads a perfect clash to an agent and shows it", [] {
        Game g("training");
        check(g.act("pause"));
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 170; ++i) check(g.act("step"));
        check(g.act("press", pad(0, "5HP")));
        check(g.act("press", pad(1, "5HP")));
        check(g.act("step"));
        check(g.act("press", pad(0, "5")));
        check(g.act("press", pad(1, "5")));
        for (int i = 0; i < 20; ++i) check(g.act("step"));
        Value seen = g.seen();
        const Value* x = seen.get("training") ? seen.get("training")->get("exchange") : nullptr;
        check(x && x->get("kind")->as_string() == "perfect clash", x ? std::string(x->get("kind")->as_string()) : std::string("none"));
        check(seen.get("p1")->get("health")->as_int() == 10000 && seen.get("p2")->get("health")->as_int() == 10000, "nothing changed");
        g.h.step();
        check(g.h.save_png(scratch_dir("fighting-clash") / "perfect-clash.png").has_value());
    });

    s.test("balance checks (P-8): a guard that only guards is broken within a minute; no attacker keeps a cornered opponent stunned for 5 seconds", [] {
        using gongfu::operator""_fx;
        const auto& r = fighting::roster();
        gongfu::SystemDef sys = r.system;
        sys.round_frames = 1 << 20;
        // Guard only: each fighter presses (a medium, a heavy, walking in) on each other, who holds back.
        for (std::size_t a = 1; a < r.fighters.size(); ++a)
            for (std::size_t d = 1; d < r.fighters.size(); ++d) {
                gongfu::Match m(sys, r.fighters[a], r.fighters[d]);
                int broken_at = -1;
                for (int f = 0; f < 3600 && broken_at < 0; ++f) {
                    const int c = f % 36;
                    const gongfu::Pad press = c == 0 ? gongfu::pad(5, gongfu::bit(gongfu::Button::mp)) : c == 12 ? gongfu::pad(5, gongfu::bit(gongfu::Button::hp)) : gongfu::pad(6);
                    m.tick(press, gongfu::pad(6));  // the right one faces left: 6 is its guard
                    for (const gongfu::Event& e : m.events())
                        if (e.kind == gongfu::Event::Kind::guard_break) broken_at = f;
                }
                check(broken_at >= 0, std::format("{} guarding {} for a minute: broken at frame {}", r.names[d], r.names[a], broken_at));
            }
        // Loops: random strings from every move a fighter has, in the corner against a defender who does nothing.
        struct Rng {
            std::uint64_t s;
            std::uint32_t next() {
                s ^= s << 13, s ^= s >> 7, s ^= s << 17;
                return static_cast<std::uint32_t>(s >> 11);
            }
        };
        for (std::size_t a = 0; a < r.fighters.size(); ++a) {
            int longest = 0, longest_locked = 0;
            Rng rng{0x5eed + a};
            for (int run = 0; run < 120; ++run) {
                gongfu::Match m(sys, r.fighters[a], r.fighters[a]);
                gongfu::State st = m.state();
                st.f[1].pos.x = sys.stage_width - 40_fx;
                st.f[0].pos.x = sys.stage_width - 120_fx;
                st.f[0].meter = st.f[1].meter = 3000;
                m.restore(st);
                int stunned = 0, locked = 0;
                for (int f = 0; f < 900; ++f) {
                    const std::uint32_t v = rng.next();
                    const int dirs[] = {5, 5, 6, 2, 3, 8};
                    const std::uint16_t button = v % 3 == 0 ? static_cast<std::uint16_t>(1u << ((v >> 4) % 6)) : 0;
                    m.tick(gongfu::pad(dirs[(v >> 8) % 6], button), gongfu::pad(5));
                    const auto mode = m.state().f[1].mode;
                    const bool held = mode == gongfu::Mode::hitstun || mode == gongfu::Mode::broken || mode == gongfu::Mode::cinematic || mode == gongfu::Mode::thrown;
                    stunned = held ? stunned + 1 : 0;
                    longest = std::max(longest, stunned);
                    // Knocked down and hit again as it rises counts too: a loop is never let go, stunned or not.
                    locked = mode == gongfu::Mode::idle ? 0 : locked + 1;
                    longest_locked = std::max(longest_locked, locked);
                    if (m.state().over) break;
                }
            }
            check(longest < 300, std::format("{}: the longest the opponent stayed stunned was {} frames", r.names[a], longest));
            check(longest_locked < 600, std::format("{}: the longest the opponent was never let go was {} frames", r.names[a], longest_locked));
            std::println("    {} longest stun {} frames, never let go {}", r.names[a], longest, longest_locked);
        }
    });

    s.test("hit feel (the hardcore combat design §2.6): per fighter, light < medium < heavy in hitstop; a light hit leaves the camera still, a heavy one shakes it", [] {
        const auto& r = fighting::roster();
        for (std::size_t i = 0; i < r.fighters.size(); ++i) {
            const gongfu::FighterDef& def = r.fighters[i].def;
            const gongfu::present2d::Look& look = r.looks[2 * i];
            const auto find = [&](const std::string& name) -> const gongfu::MoveDef* {
                const auto it = std::ranges::find(def.moves, name, &gongfu::MoveDef::name);
                return it != def.moves.end() && !it->hits.empty() ? &*it : nullptr;
            };
            const auto shake = [&](const gongfu::MoveDef& m) {
                const std::int32_t at = m.hits.front().impact < look.impact_of.size() ? look.impact_of[m.hits.front().impact] : -1;
                return at >= 0 ? look.impacts[static_cast<std::size_t>(at)].shake : -1.0f;
            };
            for (const std::string stance : {"5", "2", "j."})
                for (const std::string limb : {"P", "K"}) {
                    const gongfu::MoveDef *l = find(stance + "L" + limb), *m = find(stance + "M" + limb), *h = find(stance + "H" + limb);
                    const std::string what = std::format("{}'s {}·{}", r.names[i], stance, limb);
                    if (l && h) check(l->hits.front().hitstop < h->hits.front().hitstop, what + ": light stops less than heavy");
                    if (l && m && h)
                        check(l->hits.front().hitstop < m->hits.front().hitstop && m->hits.front().hitstop < h->hits.front().hitstop,
                              what + ": light < medium < heavy");
                    if (l) check(shake(*l) == 0, std::format("{}: a light hit shakes {}", what, shake(*l)));
                    if (h) check(shake(*h) >= 6, std::format("{}: a heavy hit shakes {}", what, shake(*h)));
                    if (m && h) check(shake(*m) > 0 && shake(*m) < shake(*h), std::format("{}: medium shakes {}", what, shake(*m)));
                }
        }
    });

    s.test("balance gates (characters 2 design §7): bots play every pair, sides swapped; each fighter wins 38–62% of its rounds, no pair beyond 25–75", [] {
        const auto& r = fighting::roster();
        constexpr std::int32_t rounds = 100;
        std::vector<double> won(r.fighters.size(), 0);
        std::vector<std::int32_t> played(r.fighters.size(), 0);
        // Each pair on its own thread (its rounds depend on nothing else: the same records however they run).
        struct Pair {
            std::size_t a, b;
            gongfu::ai::Record record;
        };
        std::vector<Pair> pairs;
        for (std::size_t a = 0; a < r.fighters.size(); ++a)
            for (std::size_t b = a + 1; b < r.fighters.size(); ++b) pairs.push_back({a, b, {}});
        {
            std::vector<std::jthread> workers;
            for (Pair& p : pairs)
                workers.emplace_back([&r, at = &p] { at->record = gongfu::ai::versus(r.system, r.fighters[at->a], r.fighters[at->b], rounds, 1); });
        }
        Hash all;
        for (const Pair& p : pairs) {
            const std::size_t a = p.a, b = p.b;
            const gongfu::ai::Record& rec = p.record;
            check(rec.win_rate(0) >= 0.25 && rec.win_rate(0) <= 0.75, std::format("{} against {}: {:.2f}", r.names[a], r.names[b], rec.win_rate(0)));
            won[a] += rec.win_rate(0) * rounds;
            won[b] += rec.win_rate(1) * rounds;
            played[a] += rounds;
            played[b] += rounds;
            all.u64(static_cast<std::uint64_t>(rec.wins[0]) << 32 | static_cast<std::uint64_t>(rec.frames));
        }
        for (std::size_t i = 0; i < r.fighters.size(); ++i) {
            const double rate = won[i] / played[i];
            std::println("    {} wins {:.2f}", r.names[i], rate);
            check(rate >= 0.38 && rate <= 0.62, std::format("{} wins {:.2f} of its rounds", r.names[i], rate));
        }
        std::println("    rounds {}", all.hex());
        check_eq(all.hex(), std::string("b89249a4decd0985"));  // the same rounds on GCC and LLVM
    });

    s.test("a stage's music: the study's piece plays from the round's start, louder as the round gets close; the rooftop's for 坤; none in training's plain room", [] {
        auto loud = [](std::string_view mode, bool hurt, std::string_view player1 = "mo") {
            Game g(mode, player1, "ni");
            check(g.act("pause"));
            auto& sounds = g.stage_mut().sounds();
            std::vector<float> one(static_cast<std::size_t>(2 * sounds.frame_samples()));
            double sum = 0;
            std::size_t n = 0;
            for (int i = 0; i < 60 * 13; ++i) {
                if (hurt && i == 0) {  // a round nearly over
                    auto& match = const_cast<gongfu::Match&>(g.stage().match());
                    gongfu::State st = match.state();
                    st.f[1].health = 900;
                    match.restore(st);
                }
                check(g.act("step"));
                sounds.mixer().render(one);
                if (i >= 60 * 9)  // past the intro (4 bars at 128), the intensity a bar old
                    for (float v : one) sum += static_cast<double>(v) * v, ++n;
            }
            return std::sqrt(sum / static_cast<double>(std::max<std::size_t>(n, 1)));
        };
        const double calm = loud("versus", false), close = loud("versus", true), plain = loud("training", false), rooftop = loud("versus", false, "kun");
        std::println("    music rms: {:.4f} calm, {:.4f} close, {:.4f} training, {:.4f} the rooftop", calm, close, plain, rooftop);
        check(calm > 0.01, "the study's piece is heard");
        check(rooftop > 0.01, "the rooftop's piece is heard");
        check(close > calm * 1.15, "more of it when the round is close");
        check(plain < 1e-4, "training's room is quiet until someone is hit");
    });

    s.test("the fight heard, offline: silence until the first contact, its sounds on its frame, the same bits every run — and a WAV of it", [] {
        Game g("training", "mo", "yue");
        check(g.act("pause"));
        check(g.act("fill"));
        auto& sounds = g.stage_mut().sounds();
        const std::int64_t per = sounds.frame_samples();
        std::vector<float> all;
        std::vector<float> one(static_cast<std::size_t>(2 * per));
        std::int32_t first_contact = -1;
        // Each frame played, then that frame's sound rendered: the mixer's clock keeps the match's.
        auto step = [&] {
            check(g.act("step"));
            if (first_contact < 0 && std::ranges::any_of(g.stage().match().events(), [](const gongfu::Event& e) {
                    return e.kind == gongfu::Event::Kind::hit || e.kind == gongfu::Event::Kind::block || e.kind == gongfu::Event::Kind::counter;
                }))
                first_contact = g.state().frame;
            sounds.mixer().render(one);
            all.insert(all.end(), one.begin(), one.end());
        };
        auto press = [&](std::initializer_list<std::string_view> pads) {
            for (std::string_view p : pads) {
                check(g.act("press", pad(0, p)));
                step();
            }
            check(g.act("press", pad(0, "5")));
        };
        check(g.act("press", pad(0, "6")));
        while ((g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 130) step();
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 6; ++i) step();
        press({"5HP"});
        for (int i = 0; i < 40; ++i) step();
        press({"2", "3", "6MP"});        // a glyph (ink on the body)
        for (int i = 0; i < 80; ++i) step();
        check(g.act("press", pad(0, "6")));  // in range of her thrust again
        while ((g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 110) step();
        check(g.act("press", pad(0, "5")));
        for (int i = 0; i < 12; ++i) step();  // let go a while: forward again would be a dash
        press({"2", "3", "6", "2", "3", "6HK"});  // her level 3: the flash's sweep, the cinematic's hits
        for (int i = 0; i < 280; ++i) step();
        check(g.state().f[1].health < 11100 - 3000, std::format("the level 3 landed: Yue at {}", g.state().f[1].health));
        check(first_contact > 0, "they met");
        const auto at = [&](std::int32_t frame) { return static_cast<std::size_t>(2 * (frame - 1) * per); };
        const auto quiet = audio::peak(std::span(all).first(at(first_contact)));
        const auto loud = audio::peak(std::span(all).subspan(at(first_contact), static_cast<std::size_t>(2 * per * 6)));
        check(quiet == 0 && loud > 0.2f, std::format("{} before the first contact (frame {}), {} after", quiet, first_contact, loud));
        const auto w = audio::wav(all, sounds.mixer().rate());
        std::ofstream(scratch_dir("fighting-sound") / "mo-and-yue.wav", std::ios::binary).write(reinterpret_cast<const char*>(w.data()), static_cast<std::streamsize>(w.size()));
        std::println("    fight heard {:016x} ({:.1f} s)", audio::hash(all), static_cast<double>(all.size()) / 2 / sounds.mixer().rate());
        check_eq(audio::hash(all), std::uint64_t{0x78ce47dee4b6f6f1});  // GCC and LLVM, every platform
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
        check(px == std::vector<std::uint64_t>{0x82097ed0a940b732ULL, 0x5233bbaccdac3381ULL}, "pinned");
    });

    return s.run(argc, argv);
}
