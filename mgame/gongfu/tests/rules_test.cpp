// The rules of system 0.2 (gongfu characters design §2, G-1..G-10): dashes
// asked for by a motion alone, projectiles that fall, hit more than once,
// outweigh each other and are limited on screen, back throws and the frames
// a fighter cannot be thrown, invulnerability by kind, a move's own
// hurtboxes, target combos, juggle points and air resets, hitstop by side
// and input kept through a super's flash, the meter a defender gains, and a
// knockdown's own length.
import std;
import gongfu;
import openmui.testing;

using namespace omui::testing;
using namespace gongfu;

namespace {

constexpr std::uint16_t LP = bit(Button::lp), MP = bit(Button::mp), HP = bit(Button::hp), LK = bit(Button::lk), MK = bit(Button::mk),
                        HK = bit(Button::hk);

HitDef strike(Frames active, Box box, std::int32_t damage, std::int32_t stun, std::int32_t stop = 6) {
    return {.active = active, .boxes = {box}, .damage = damage, .hitstun = stun, .blockstun = stun - 4, .hitstop = stop, .pushback = 2_fx};
}

FighterDef body() {
    FighterDef f;
    f.name = "Rule";
    f.health = 10000;
    f.walk_forward = 4_fx;
    f.walk_back = 3_fx;
    f.jump_up = 20_fx;
    f.jump_forward = 5_fx;
    f.gravity = 1_fx;
    f.push = {-30_fx, 0_fx, 60_fx, 160_fx};
    f.hurt_standing = {-35_fx, 0_fx, 70_fx, 180_fx};
    f.hurt_crouching = {-35_fx, 0_fx, 70_fx, 110_fx};
    f.hurt_air = {-30_fx, 40_fx, 60_fx, 130_fx};

    MoveDef jab{.name = "5LP", .command = {.buttons = LP}, .total = 12};
    jab.hits.push_back(strike({4, 5}, {20_fx, 110_fx, 70_fx, 30_fx}, 300, 14));
    jab.cancels.push_back({.frames = {4, 8}, .into = {"5LP"}});
    f.moves.push_back(jab);

    // A target combo: 5MP into 5HP on contact, and into nothing else.
    MoveDef mid{.name = "5MP", .command = {.buttons = MP}, .total = 18};
    mid.hits.push_back(strike({5, 7}, {20_fx, 100_fx, 80_fx, 40_fx}, 500, 16));
    mid.cancels.push_back({.frames = {5, 14}, .into = {"5HP"}});
    f.moves.push_back(mid);
    MoveDef heavy{.name = "5HP", .command = {.buttons = HP}, .total = 28};
    heavy.hits.push_back(strike({8, 10}, {20_fx, 100_fx, 90_fx, 40_fx}, 800, 22, 10));
    heavy.hits[0].hitstop_attacker = 4;  // the attacker's own stop: shorter
    f.moves.push_back(heavy);

    // A launcher with one juggle point, and an air follow-up.
    MoveDef launcher{.name = "2HP", .command = {.direction = 2, .buttons = HP}, .stance = Stance::crouching, .total = 30};  // more particular than 5HP: read first
    launcher.hits.push_back(strike({7, 10}, {0_fx, 60_fx, 80_fx, 120_fx}, 850, 40));
    launcher.hits[0].launch = {1_fx, 16_fx};
    launcher.hits[0].juggle = 1;
    launcher.cancels.push_back({.frames = {7, 30}, .into = {"8LK"}});
    f.moves.push_back(launcher);
    // A rising kick that hits the air (it can be asked for again in the juggle).
    MoveDef rising{.name = "8LK", .command = {.direction = 8, .buttons = LK}, .total = 20};
    rising.steps = {{1, {0_fx, 0_fx}}};
    rising.hits.push_back(strike({3, 12}, {-20_fx, 120_fx, 120_fx, 200_fx}, 300, 20));
    rising.cancels.push_back({.frames = {3, 20}, .into = {"8LK"}});
    f.moves.push_back(rising);

    // A knockdown with its own length on the ground.
    MoveDef sweep{.name = "2HK", .command = {.direction = 2, .buttons = HK}, .stance = Stance::crouching, .total = 36};
    sweep.hits.push_back(strike({9, 11}, {10_fx, 0_fx, 110_fx, 30_fx}, 900, 20));
    sweep.hits[0].level = Level::low;
    sweep.hits[0].knockdown = true;
    sweep.hits[0].down = 20;
    f.moves.push_back(sweep);

    // An arm stretched out on frames 6–12: hit where the standing box is not.
    MoveDef poke{.name = "5MK", .command = {.buttons = MK}, .total = 24};
    poke.hits.push_back(strike({8, 9}, {40_fx, 80_fx, 60_fx, 20_fx}, 600, 16));
    poke.hurt.push_back({.frames = {6, 12}, .boxes = {{-35_fx, 0_fx, 70_fx, 180_fx}, {35_fx, 90_fx, 70_fx, 40_fx}}});
    f.moves.push_back(poke);

    // Dashes: a motion alone; the back dash passes through strikes, not throws.
    MoveDef dash{.name = "66", .command = {.motion = dash_forward}, .total = 18, .priority = 5};
    dash.steps = {{1, {8_fx, 0_fx}}, {14, {0_fx, 0_fx}}};
    f.moves.push_back(dash);
    MoveDef backdash{.name = "44", .command = {.motion = dash_back}, .total = 22, .priority = 5};
    backdash.steps = {{1, {-6_fx, 0_fx}}, {16, {0_fx, 0_fx}}};
    backdash.invulnerable = {{.frames = {1, 8}, .to = to_strike | to_air | to_projectile}};
    f.moves.push_back(backdash);

    // An anti-air invulnerable only to attacks from the air.
    MoveDef antiair{.name = "623P", .command = {.motion = dragon, .buttons = punches}, .total = 40, .priority = 11};
    antiair.invulnerable = {{.frames = {1, 7}, .to = to_air}};
    antiair.hits.push_back(strike({6, 9}, {0_fx, 90_fx, 70_fx, 120_fx}, 1000, 30, 10));
    antiair.hits[0].knockdown = true;
    f.moves.push_back(antiair);

    // A jumping heavy for the anti-air to meet.
    MoveDef jump{.name = "j.HP", .command = {.buttons = HP}, .stance = Stance::air, .total = 20};
    jump.hits.push_back(strike({6, 12}, {0_fx, -20_fx, 80_fx, 60_fx}, 800, 18));
    jump.hits[0].level = Level::overhead;
    f.moves.push_back(jump);

    // Projectiles: one limited on screen; one that falls; one that hits three times and outweighs.
    MoveDef glyph{.name = "236P", .command = {.motion = quarter_forward, .buttons = punches}, .total = 40, .priority = 10};
    glyph.projectile = ProjectileDef{.spawn = 10, .offset = {50_fx, 100_fx}, .velocity = {6_fx, 0_fx}, .life = 200, .limit = 1};
    glyph.projectile->hit = strike({1, 200}, {-20_fx, -20_fx, 40_fx, 40_fx}, 600, 20, 7);
    f.moves.push_back(glyph);
    MoveDef drop{.name = "j.236P", .command = {.motion = quarter_forward, .buttons = punches}, .stance = Stance::air, .total = 30, .priority = 10};
    drop.projectile = ProjectileDef{.spawn = 8, .offset = {30_fx, 0_fx}, .velocity = {5_fx, -5_fx}, .life = 120};
    drop.projectile->hit = strike({1, 120}, {-15_fx, -15_fx, 30_fx, 30_fx}, 500, 16);
    f.moves.push_back(drop);
    MoveDef great{.name = "236236P", .command = {.motion = motion({2, 3, 6, 2, 3, 6}, 24), .buttons = punches}, .total = 50, .priority = 20,
                  .meter_cost = 1000, .freeze = 30};
    great.projectile = ProjectileDef{.spawn = 10, .offset = {60_fx, 100_fx}, .velocity = {7_fx, 0_fx}, .life = 200, .hits = 3, .interval = 4,
                                     .strength = 2};
    great.projectile->hit = strike({1, 200}, {-30_fx, -30_fx, 60_fx, 60_fx}, 500, 24, 6);
    f.moves.push_back(great);

    // Throws: forward and back.
    MoveDef grab{.name = "throw", .command = {.buttons = static_cast<std::uint16_t>(LP | LK), .together = true}, .total = 30, .priority = 20};
    grab.throw_ = ThrowDef{.active = {5, 6}, .range = {0_fx, 0_fx, 80_fx, 150_fx}, .damage = 1200};
    f.moves.push_back(grab);
    MoveDef back{.name = "4throw", .command = {.direction = 4, .buttons = static_cast<std::uint16_t>(LP | LK), .together = true}, .total = 30,
                 .priority = 21};
    back.throw_ = ThrowDef{.active = {5, 6}, .range = {0_fx, 0_fx, 80_fx, 150_fx}, .damage = 1300, .swap = true};
    f.moves.push_back(back);
    return f;
}

const Fighter& rule() {
    static const Fighter f = [] {
        auto c = compile(body());
        check(c.has_value(), c ? "" : c.error().front().where + ": " + c.error().front().text);
        return c ? *c : Fighter{};
    }();
    return f;
}

SystemDef rules() {
    SystemDef s;
    s.round_frames = 1 << 20;
    return s;
}

Match apart(Fixed gap) {
    Match m(rules(), rule(), rule());
    State s = m.state();
    const Fixed mid = m.system().stage_width / 2;
    s.f[0].pos.x = mid - gap / 2;
    s.f[1].pos.x = mid + (gap - gap / 2);
    m.restore(s);
    return m;
}

bool has(const Match& m, Event::Kind k) {
    return std::ranges::any_of(m.events(), [&](const Event& e) { return e.kind == k; });
}

std::int32_t count(const Match& m, Event::Kind k) {
    return static_cast<std::int32_t>(std::ranges::count_if(m.events(), [&](const Event& e) { return e.kind == k; }));
}

std::string_view moving(const Match& m, std::int32_t i) {
    const FighterState& s = m.state().f[static_cast<std::size_t>(i)];
    return s.mode == Mode::move ? std::string_view(m.fighter(i).def.moves[static_cast<std::size_t>(s.move)].name) : std::string_view("-");
}

// Plays a script for one side (the other neutral, or its own script).
void play(Match& m, std::span<const Pad> a, std::span<const Pad> b = {}) {
    for (std::size_t f = 0; f < std::max(a.size(), b.size()); ++f) m.tick(f < a.size() ? a[f] : pad(5), f < b.size() ? b[f] : pad(5));
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gongfu.rules"};

    s.test("G-1 dashes: tapped twice, not held; the back dash goes through a jab but not a throw", [] {
        Match m = apart(600_fx);
        const std::array tap{pad(6), pad(5), pad(6)};
        play(m, tap);
        check_eq(moving(m, 0), std::string_view("66"));
        Match held = apart(600_fx);
        const std::array walk{pad(6), pad(6), pad(6), pad(6)};
        play(held, walk);
        check(held.state().f[0].mode == Mode::walk_forward, "holding forward walks");
        Match slow = apart(600_fx);
        std::vector<Pad> late{pad(6)};
        for (int i = 0; i < 12; ++i) late.push_back(pad(5));
        late.push_back(pad(6));
        play(slow, late);
        check(moving(slow, 0) != std::string_view("66"), "too slow: a walk");
        // The right fighter faces left: its back is 6.
        Match bd = apart(90_fx);
        const std::array jab{pad(5, LP)};
        const std::array away{pad(6), pad(5), pad(6)};
        play(bd, std::span<const Pad>{}, std::span<const Pad>(away).first(2));
        bd.tick(pad(5, LP), away[2]);
        bool hit = false;
        for (int f = 0; f < 10; ++f) {
            bd.tick(pad(5), pad(5));
            hit = hit || has(bd, Event::Kind::hit);
        }
        check(!hit, "the jab passes through the back dash");
        (void)jab;
        Match bt = apart(70_fx);
        play(bt, std::span<const Pad>{}, std::span<const Pad>(away).first(2));
        bt.tick(pad(5, static_cast<std::uint16_t>(LP | LK)), away[2]);
        bool thrown = false;
        for (int f = 0; f < 10; ++f) {
            bt.tick(pad(5), pad(5));
            thrown = thrown || bt.state().f[1].mode == Mode::thrown;
        }
        check(thrown, "a back dash is thrown");
    });

    s.test("G-2 projectiles: one on screen at a time; one falls and is gone at the ground; one hits three times and outweighs another", [] {
        Match m = apart(900_fx);
        const std::array motion{pad(2), pad(3), pad(6, LP)};
        play(m, motion);
        for (int f = 0; f < 15; ++f) m.tick(pad(5), pad(5));
        check(std::ranges::count_if(m.state().p, [](const ProjectileState& p) { return p.alive != 0; }) == 1, "one glyph out");
        for (int f = 0; f < 30; ++f) m.tick(pad(5), pad(5));  // the first glyph's move is over; the glyph flies on
        play(m, motion);
        check(moving(m, 0) == std::string_view("5LP"), std::format("a second is not started past the limit: {}", moving(m, 0)));
        // Falling: from a jump, it comes down and is gone where it meets the ground.
        Match j = apart(900_fx);
        j.tick(pad(8), pad(5));
        for (int f = 0; f < 10; ++f) j.tick(pad(5), pad(5));
        play(j, motion);
        check_eq(moving(j, 0), std::string_view("j.236P"));
        bool seen_low = false, gone = false;
        for (int f = 0; f < 80 && !gone; ++f) {
            j.tick(pad(5), pad(5));
            for (const ProjectileState& p : j.state().p)
                if (p.alive) seen_low = seen_low || p.pos.y < 60_fx;
            const bool any = std::ranges::any_of(j.state().p, [](const ProjectileState& p) { return p.alive != 0; });
            gone = seen_low && !any;
        }
        check(gone, "it fell and was gone at the ground");
        // The great glyph: three hits, four frames apart at least, and it outweighs a glyph.
        Match g = apart(400_fx);
        State st = g.state();
        st.f[0].meter = 1000;
        g.restore(st);
        const std::array super{pad(2), pad(3), pad(6), pad(2), pad(3), pad(6, LP)};
        play(g, super);
        std::int32_t hits = 0;
        for (int f = 0; f < 120; ++f) {
            g.tick(pad(5), pad(5));
            hits += count(g, Event::Kind::hit);
        }
        check_eq(hits, 3);
        check_eq(g.state().f[1].health, 10000 - 500 - 500 * 100 / 100 - 500 * 80 / 100);
        Match c = apart(900_fx);
        st = c.state();
        st.f[0].meter = 1000;
        c.restore(st);
        const std::array mirrored{pad(2), pad(1), pad(4, LP)};
        play(c, super, std::array{pad(5), pad(5), pad(5), pad(2), pad(1), pad(4, LP)});
        bool clash = false;
        for (int f = 0; f < 160 && !clash; ++f) {
            c.tick(pad(5), pad(5));
            clash = has(c, Event::Kind::projectile_clash);
        }
        (void)mirrored;
        const auto alive = std::ranges::count_if(c.state().p, [](const ProjectileState& p) { return p.alive != 0; });
        check(clash && alive == 1, std::format("clashed {}, {} left", clash, alive));
        bool hit_right = false;
        for (int f = 0; f < 120 && !hit_right; ++f) {
            c.tick(pad(5), pad(5));
            hit_right = std::ranges::any_of(c.events(), [](const Event& e) { return e.kind == Event::Kind::hit && e.attacker == 0; });
        }
        check(hit_right, "the great glyph went on and hit");
    });

    s.test("G-3 throws: a back throw changes places; no throw just after blockstun; a fighter about to jump is thrown", [] {
        Match m = apart(70_fx);
        const Fixed x0 = m.state().f[0].pos.x, x1 = m.state().f[1].pos.x;
        m.tick(pad(4, static_cast<std::uint16_t>(LP | LK)), pad(5));
        for (int f = 0; f < 6; ++f) m.tick(pad(5), pad(5));
        check(m.state().f[1].mode == Mode::thrown && m.state().f[0].pos.x > m.state().f[1].pos.x,
              std::format("{} → {}, {} → {}", x0.to_double(), m.state().f[0].pos.x.to_double(), x1.to_double(), m.state().f[1].pos.x.to_double()));
        check(m.state().f[0].facing == -1 && m.state().f[1].facing == 1, "each faces the other");
        // Protection: thrown neither on the frame blockstun ends nor the next, then thrown.
        for (const int late : {0, 3}) {
            Match p = apart(70_fx);
            State st = p.state();
            st.f[1].mode = Mode::blockstun;
            st.f[1].stun = 6;
            p.restore(st);
            for (int f = 0; f < 6 - 4 + late; ++f) p.tick(pad(5), pad(5));  // the throw's frame 5 lands `late` frames after the stun ends
            p.tick(pad(5, static_cast<std::uint16_t>(LP | LK)), pad(5));
            bool thrown = false;
            for (int f = 0; f < 8; ++f) {
                p.tick(pad(5), pad(5));
                thrown = thrown || p.state().f[1].mode == Mode::thrown;
            }
            check(thrown == (late == 3), std::format("{} frames after blockstun: thrown {}", late, thrown));
        }
        Match pj = apart(70_fx);
        pj.tick(pad(5, static_cast<std::uint16_t>(LP | LK)), pad(5));
        for (int f = 0; f < 3; ++f) pj.tick(pad(5), pad(5));
        pj.tick(pad(5), pad(8));  // about to jump on the throw's active frame
        bool thrown = false;
        for (int f = 0; f < 4; ++f) {
            pj.tick(pad(5), pad(5));
            thrown = thrown || pj.state().f[1].mode == Mode::thrown;
        }
        check(thrown, "prejump is thrown");
    });

    s.test("G-4 invulnerability by kind: an anti-air passes through a jump-in but a ground jab hits it", [] {
        // Fighter 2 on its anti-air's frame 3 (invulnerable to the air); fighter 1 on an active frame of j.HP above it, then of a jab beside it.
        for (const bool from_air : {true, false}) {
            Match m = apart(from_air ? 60_fx : 90_fx);
            State st = m.state();
            FighterState& a = st.f[0];
            FighterState& d = st.f[1];
            a.mode = Mode::move;
            a.move = rule().find(from_air ? "j.HP" : "5LP");
            a.move_frame = from_air ? 7 : 3;  // advanced to the active frame on the next tick
            if (from_air) a.pos.y = 100_fx, a.vel = {0_fx, -1_fx};
            d.mode = Mode::move;
            d.move = rule().find("623P");
            d.move_frame = 2;
            d.facing = -1;
            m.restore(st);
            m.tick(pad(5), pad(5));
            const bool hit = std::ranges::any_of(m.events(), [](const Event& e) { return e.attacker == 0 && (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter); });
            check(hit == !from_air, std::format("{}: hit {}", from_air ? "from the air" : "from the ground", hit));
        }
    });

    s.test("G-5 a move's own hurtboxes: the stretched arm is hit where the body is not", [] {
        // 5MK's arm reaches 105 in front on frames 6–12; a jab reaching 90 from 160 away hits only the arm.
        for (const bool poke : {false, true}) {
            Match m = apart(160_fx);
            m.tick(pad(5), poke ? pad(5, MK) : pad(5));
            for (int f = 0; f < 3; ++f) m.tick(pad(5), pad(5));
            m.tick(pad(5, LP), pad(5));  // the jab's active frames meet 5MK's frames 7–8
            bool hit = false;
            for (int f = 0; f < 6; ++f) {
                m.tick(pad(5), pad(5));
                hit = hit || std::ranges::any_of(m.events(), [](const Event& e) { return e.attacker == 0 && (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter); });
            }
            check(hit == poke, std::format("{}: hit {}", poke ? "the arm out" : "standing", hit));
        }
    });

    s.test("G-6 a target combo: 5MP into 5HP on contact; not into 5LP, not on a whiff", [] {
        for (const auto& [gap, then, want] : {std::tuple{90_fx, HP, "5HP"}, std::tuple{90_fx, LP, "5MP"}, std::tuple{400_fx, HP, "-"}}) {
            Match m = apart(gap);
            m.tick(pad(5, MP), pad(5));
            for (int f = 0; f < 6; ++f) m.tick(pad(5), pad(5));
            m.tick(pad(5, then), pad(5));
            for (int f = 0; f < 12; ++f) m.tick(pad(5), pad(5));  // through the hitstop
            check(moving(m, 0) == std::string_view(want), std::format("gap {}, then {}: {}", gap.to_double(), then, moving(m, 0)));
        }
    });

    s.test("G-7 juggle points: one follow-up in the air, then an air reset", [] {
        Match m = apart(80_fx);
        m.tick(pad(2, HP), pad(5));
        std::vector<std::int32_t> combo;
        bool reset = false;
        for (int f = 0; f < 90; ++f) {
            // 8LK cancels on contact: ask for it every few frames.
            m.tick(f % 6 == 3 ? pad(8, LK) : pad(5), pad(5));
            for (const Event& e : m.events())
                if (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter) combo.push_back(m.state().f[1].combo);
            reset = reset || m.state().f[1].mode == Mode::reset;
        }
        check(combo.size() >= 2 && combo[0] == 1 && combo[1] == 2, std::format("{}", combo));
        check(reset, "the third hit in the air resets");
        check(m.state().f[1].mode == Mode::idle && m.state().f[1].pos.y == Fixed{}, "it lands free");
    });

    s.test("G-8 hitstop by side; a projectile's owner keeps moving; input kept through a super's flash", [] {
        Match m = apart(90_fx);
        m.tick(pad(5, HP), pad(5));
        for (int f = 0; f < 7; ++f) m.tick(pad(5), pad(5));
        check(has(m, Event::Kind::hit), "5HP hit on frame 8");
        check(m.state().f[0].stop == 4 && m.state().f[1].stop == 10, std::format("stops {} / {}", m.state().f[0].stop, m.state().f[1].stop));
        // The glyph: when it hits, its owner's move goes on.
        Match p = apart(160_fx);
        const std::array motion{pad(2), pad(3), pad(6, LP)};
        play(p, motion);
        std::int32_t frame_at_hit = -1;
        for (int f = 0; f < 80 && frame_at_hit < 0; ++f) {
            p.tick(pad(5), pad(5));
            if (has(p, Event::Kind::hit)) frame_at_hit = p.state().f[0].move_frame;
        }
        p.tick(pad(5), pad(5));
        check(frame_at_hit > 0 && p.state().f[0].move_frame == frame_at_hit + 1 && p.state().f[0].stop == 0,
              std::format("the owner at frame {} then {}", frame_at_hit, p.state().f[0].move_frame));
        // A reversal pressed during the flash starts on the frame after it.
        Match fl = apart(500_fx);
        State st = fl.state();
        st.f[0].meter = 1000;
        fl.restore(st);
        const std::array super{pad(2), pad(3), pad(6), pad(2), pad(3), pad(6, LP)};
        play(fl, super);
        check(fl.state().freeze > 0, "the flash");
        const std::array dp{pad(4), pad(1), pad(2), pad(1, LP)};
        for (int f = 0; f < 10; ++f) fl.tick(pad(5), pad(5));
        play(fl, std::span<const Pad>{}, dp);  // asked for well inside the flash
        while (fl.state().freeze > 0) fl.tick(pad(5), pad(5));
        fl.tick(pad(5), pad(5));
        check_eq(moving(fl, 1), std::string_view("623P"));
    });

    s.test("G-9 a defender gains a tenth of the damage it takes as meter; G-10 a knockdown's own length", [] {
        Match m = apart(90_fx);
        m.tick(pad(5, LP), pad(5));
        for (int f = 0; f < 4; ++f) m.tick(pad(5), pad(5));
        check_eq(m.state().f[1].meter, 30);
        Match k = apart(100_fx);
        k.tick(pad(2, HK), pad(5));
        int down = 0;
        bool hit = false;
        for (int f = 0; f < 80; ++f) {
            k.tick(pad(5), pad(5));
            hit = hit || has(k, Event::Kind::hit);
            if (k.state().f[1].mode == Mode::knockdown && k.state().f[1].stop == 0) ++down;
        }
        check(hit, "the sweep hit");
        check_eq(down, 20);
    });

    s.test("all of it rolled back: 5,000 frames of motions, dashes, supers, juggles and throws, every rollback the bytes of a straight run; the hash pinned", [] {
        // Inputs a player could give this fighter: held directions, presses, and now and then a whole motion.
        struct Rng {
            std::uint64_t s;
            std::uint32_t next() {
                s ^= s << 13, s ^= s >> 7, s ^= s << 17;
                return static_cast<std::uint32_t>(s >> 11);
            }
        };
        auto script = [](std::uint64_t seed, int frames) {
            Rng r{seed};
            std::vector<Pad> out;
            int held = 5;
            while (static_cast<int>(out.size()) < frames) {
                const std::uint32_t v = r.next();
                switch (v % 16) {
                    case 0: for (Pad p : {pad(2), pad(3), pad(6, LP)}) out.push_back(p); break;
                    case 1: for (Pad p : {pad(6), pad(2), pad(3, HP)}) out.push_back(p); break;
                    case 2: for (Pad p : {pad(6), pad(5), pad(6)}) out.push_back(p); break;
                    case 3: for (Pad p : {pad(4), pad(5), pad(4)}) out.push_back(p); break;
                    case 4: for (Pad p : {pad(2), pad(3), pad(6), pad(2), pad(3), pad(6, LP)}) out.push_back(p); break;
                    case 5: out.push_back(pad(held, static_cast<std::uint16_t>(LP | LK))); break;
                    case 6: out.push_back(pad(2, HP)); break;
                    case 7: out.push_back(pad(8, LK)); break;
                    default: {
                        constexpr std::array<int, 10> dirs{5, 6, 6, 4, 2, 3, 1, 9, 7, 8};
                        if (v % 3 == 0) held = dirs[(v >> 6) % dirs.size()];
                        const std::uint16_t b = v % 4 == 1 ? static_cast<std::uint16_t>(1u << ((v >> 9) % 6)) : 0;
                        out.push_back(pad(held, b));
                    }
                }
            }
            out.resize(static_cast<std::size_t>(frames));
            return out;
        };
        constexpr int frames = 5000;
        const auto a = script(7, frames), b = script(31, frames);
        FighterDef tough = body();
        tough.health = 1 << 24;
        const Fighter t = *compile(tough);
        Match straight(rules(), t, t);
        State st = straight.state();
        st.f[0].meter = st.f[1].meter = 1000;  // supers from the start
        straight.restore(st);
        std::vector<std::uint64_t> hashes;
        std::array<int, 8> seen{};
        for (int i = 0; i < frames; ++i) {
            straight.tick(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(i)]);
            hashes.push_back(straight.checksum());
            for (std::size_t k = 0; k < 2; ++k) {
                const auto& f = straight.state().f[k];
                seen[0] += f.mode == Mode::reset;
                seen[1] += f.mode == Mode::thrown;
                seen[2] += f.mode == Mode::move && t.def.moves[static_cast<std::size_t>(f.move)].name == "66";
                seen[3] += f.mode == Mode::move && t.def.moves[static_cast<std::size_t>(f.move)].name == "236236P";
            }
            seen[4] += straight.state().freeze > 0;
            seen[5] += static_cast<int>(std::ranges::count_if(straight.state().p, [](const ProjectileState& p) { return p.alive != 0; }));
        }
        Match rolled(rules(), t, t);
        rolled.restore(st);
        std::vector<State> saved;
        Rng depth{99};
        int mismatches = 0;
        for (int i = 0; i < frames; ++i) {
            saved.push_back(rolled.state());
            rolled.tick(a[static_cast<std::size_t>(i)], b[static_cast<std::size_t>(i)]);
            const int back = 1 + static_cast<int>(depth.next() % 10);
            if (back <= i + 1) {
                rolled.restore(saved[static_cast<std::size_t>(i + 1 - back)]);
                for (int j = i + 1 - back; j <= i; ++j) rolled.tick(a[static_cast<std::size_t>(j)], b[static_cast<std::size_t>(j)]);
            }
            if (rolled.checksum() != hashes[static_cast<std::size_t>(i)]) ++mismatches;
        }
        check_eq(mismatches, 0);
        check(seen[0] > 0 && seen[1] > 0 && seen[2] > 0 && seen[3] > 0 && seen[4] > 0 && seen[5] > 0,
              std::format("resets {}, thrown {}, dashes {}, supers {}, flashes {}, projectiles {}", seen[0], seen[1], seen[2], seen[3], seen[4], seen[5]));
        std::println("    rules run hash {:016x}", hashes.back());
        check(hashes.back() == 0xc349faacaa4ed0beULL, std::format("run hash {:016x}", hashes.back()));  // GCC, LLVM and wasm
    });

    return s.run(argc, argv);
}
