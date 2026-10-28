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
    explicit Game(std::string_view mode = "versus", std::string_view player1 = "", std::string_view player2 = "") {
        AppSpec spec = fighting::app();
        spec.setup(h.runtime());
        if (mode == "versus") {
            h.mount("main", spec.view());
        } else {
            h.mount("main", fighting::stage({.id = "stage", .mode = std::string(mode), .player1 = std::string(player1), .player2 = std::string(player2),
                                             .width = Dim(layout::pct(100)), .height = Dim(layout::pct(100))}));
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
        check(r.names == std::vector<std::string>{"kata", "mo", "ni", "yue"}, "Kata, Mo, Ni and Yue");
        check_eq(r.fighters[0].def.name, std::string("Kata"));
        check_eq(r.fighters[1].def.name, std::string("Mo"));
        check_eq(r.fighters[2].def.name, std::string("Ni"));
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

    s.test("墨's level 3, 「永字八法」: the flash and its close-up, then 永 written stroke by stroke on paper and sealed — frames pinned", [] {
        Game g("training", "mo", "kata");
        check(g.act("pause"));
        check(g.act("fill"));
        // In range of the thrust, then 236236K.
        check(g.act("press", pad(0, "6")));
        for (int i = 0; i < 120 && (g.state().f[1].pos.x - g.state().f[0].pos.x).to_double() > 110; ++i) check(g.act("step"));
        for (std::string_view p : {"2", "3", "6", "2", "3", "6HK"}) {
            check(g.act("press", pad(0, p)));
            check(g.act("step"));
        }
        check(g.act("press", pad(0, "5")));
        const auto dir = scratch_dir("fighting-mo-level3");
        std::vector<std::uint64_t> px;
        std::int32_t lock_from = -1;
        for (int i = 0; i < 320; ++i) {
            check(g.act("step"));
            const auto& st = g.state();
            const bool flash = st.freeze == 25;
            if (st.lock.owner == 0 && lock_from < 0) lock_from = st.frame;
            const std::int32_t lf = st.lock.owner == 0 ? st.lock.frame : -1;
            if (flash || lf == 6 || lf == 16 || lf == 62 || lf == 125 || lf == 176 || lf == 182) {
                g.h.step();
                px.push_back(g.pixels());
                check(g.h.save_png(dir / std::format("{:02}-{}.png", px.size(), flash ? std::string("flash") : std::format("lock-{}", lf))).has_value());
            }
        }
        check(lock_from > 0, "the thrust hit: the cinematic played");
        check_eq(px.size(), std::size_t{7});
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    mo level 3 {}", all.hex());
        check_eq(all.hex(), std::string("b6012d883ce64d00"));  // GCC and LLVM, every platform
    });

    s.test("墨's glyphs: 一, 人 and 永 written in front of her and thrown, and her level 2's stroke across the stage — frames pinned", [] {
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
        check_eq(all.hex(), std::string("35882e461c7819c7"));  // GCC and LLVM, every platform
    });

    s.test("霓's afterimages and her level 3, 「全频」: the neon city, the flurry, the uppercut — frames pinned", [] {
        Game g("training", "ni", "kata");
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
        std::int32_t lock_from = -1;
        for (int i = 0; i < 300; ++i) {
            check(g.act("step"));
            const auto& st = g.state();
            if (st.lock.owner == 0 && lock_from < 0) lock_from = st.frame;
            const std::int32_t lf = st.lock.owner == 0 ? st.lock.frame : -1;
            if (st.freeze == 25 || lf == 16 || lf == 80 || lf == 162) {
                g.h.step();
                px.push_back(g.pixels());
                check(g.h.save_png(dir / std::format("{:02}-{}.png", px.size(), lf < 0 ? std::string("flash") : std::format("lock-{}", lf))).has_value());
            }
        }
        check(lock_from > 0, "the knee hit: the cinematic played");
        check_eq(px.size(), std::size_t{5});
        Hash all;
        for (auto v : px) all.u64(v);
        std::println("    ni level 3 {}", all.hex());
        check_eq(all.hex(), std::string("b752039ae801b1db"));  // GCC and LLVM, every platform
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
        check_eq(all.hex(), std::string("f1f320982f5c4e96"));  // GCC and LLVM, every platform
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
            int longest = 0;
            Rng rng{0x5eed + a};
            for (int run = 0; run < 120; ++run) {
                gongfu::Match m(sys, r.fighters[a], r.fighters[a]);
                gongfu::State st = m.state();
                st.f[1].pos.x = sys.stage_width - 40_fx;
                st.f[0].pos.x = sys.stage_width - 120_fx;
                st.f[0].meter = st.f[1].meter = 3000;
                m.restore(st);
                int stunned = 0;
                for (int f = 0; f < 900; ++f) {
                    const std::uint32_t v = rng.next();
                    const int dirs[] = {5, 5, 6, 2, 3, 8};
                    const std::uint16_t button = v % 3 == 0 ? static_cast<std::uint16_t>(1u << ((v >> 4) % 6)) : 0;
                    m.tick(gongfu::pad(dirs[(v >> 8) % 6], button), gongfu::pad(5));
                    const auto mode = m.state().f[1].mode;
                    const bool held = mode == gongfu::Mode::hitstun || mode == gongfu::Mode::broken || mode == gongfu::Mode::cinematic || mode == gongfu::Mode::thrown;
                    stunned = held ? stunned + 1 : 0;
                    longest = std::max(longest, stunned);
                    if (m.state().over) break;
                }
            }
            check(longest < 300, std::format("{}: the longest the opponent stayed stunned was {} frames", r.names[a], longest));
            std::println("    {} longest stun {} frames", r.names[a], longest);
        }
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
        check(px == std::vector<std::uint64_t>{0x6673067c5af529f5ULL, 0x6fd3faab68e6d24cULL}, "pinned");
    });

    return s.run(argc, argv);
}
