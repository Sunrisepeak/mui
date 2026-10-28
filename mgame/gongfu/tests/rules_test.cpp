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
                  .meter_cost = 1000, .freeze = 30, .level = 1};
    great.projectile = ProjectileDef{.spawn = 10, .offset = {60_fx, 100_fx}, .velocity = {7_fx, 0_fx}, .life = 200, .hits = 3, .interval = 4,
                                     .strength = 2};
    great.projectile->hit = strike({1, 200}, {-30_fx, -30_fx, 60_fx, 60_fx}, 500, 24, 6);
    f.moves.push_back(great);

    // A striking super (level 2), no flash: it wins every clash but another super's.
    MoveDef rush{.name = "214214P", .command = {.motion = motion({2, 1, 4, 2, 1, 4}, 24), .buttons = punches}, .total = 40, .priority = 20,
                 .meter_cost = 2000, .level = 2};
    rush.hits.push_back(strike({8, 12}, {20_fx, 80_fx, 120_fx, 60_fx}, 1500, 30, 12));
    f.moves.push_back(rush);

    // A level-3 super with a cinematic: a thrust, then three hits in a 100-frame lock.
    MoveDef finale{.name = "236236K", .command = {.motion = motion({2, 3, 6, 2, 3, 6}, 24), .buttons = kicks}, .total = 40, .priority = 21,
                   .meter_cost = 3000, .freeze = 50, .level = 3};
    finale.hits.push_back(strike({7, 9}, {20_fx, 80_fx, 90_fx, 60_fx}, 300, 30, 10));
    finale.cinematic = CinematicDef{.frames = 100,
                                    .hits = {{20, 1000, 0}, {40, 1000, 0}, {60, 1000, 0}},
                                    .attacker_at = {0_fx, 0_fx},
                                    .defender_at = {150_fx, 0_fx},
                                    .down = 40,
                                    .recovery = 10};
    f.moves.push_back(finale);

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

// Fighter 2 has held back (its guard, facing left: 6) for a while; the two `gap` apart again.
void guarding(Match& m, Fixed gap) {
    for (int f = 0; f < 12; ++f) m.tick(pad(5), pad(6));
    State s = m.state();
    const Fixed mid = m.system().stage_width / 2;
    s.f[0].pos.x = mid - gap / 2;
    s.f[1].pos.x = mid + (gap - gap / 2);
    s.f[1].mode = Mode::idle;
    m.restore(s);
}

// Fighter 1 presses `button` (as its first pad) while fighter 2 gives `guard`; ticks until something happens or `frames` pass.
std::vector<Event> exchange(Match& m, std::uint16_t button, std::span<const Pad> guard, int frames = 40) {
    std::vector<Event> seen;
    for (int f = 0; f < frames; ++f) {
        m.tick(f == 0 ? pad(5, button) : pad(5), f < static_cast<int>(guard.size()) ? guard[static_cast<std::size_t>(f)] : guard.back());
        seen.insert(seen.end(), m.events().begin(), m.events().end());
    }
    return seen;
}

bool any_kind(std::span<const Event> es, Event::Kind k) {
    return std::ranges::any_of(es, [&](const Event& e) { return e.kind == k; });
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
        check_eq(g.state().f[1].health, 10000 - 3 * 500);  // a super's scaling is locked at its first hit (system 0.3)
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

    s.test("P-1 posture: a block costs the move's posture; full, a guard break; its first follow-up resets it, steadies and scales from 80%", [] {
        FighterDef small = body();
        small.posture_max = 300;
        const Fighter thin = *compile(small);
        check_eq(thin.posture[static_cast<std::size_t>(thin.find("5HP"))][0], std::int32_t{110});
        Match m(rules(), thin, thin);
        guarding(m, 120_fx);
        const std::array hold{pad(6)};
        std::vector<Event> all;
        for (int round = 0; round < 3; ++round) {
            State st = m.state();
            st.f[0].pos.x = st.f[1].pos.x - 120_fx;  // the attacker back in range
            m.restore(st);
            auto es = exchange(m, HP, hold, 40);
            all.insert(all.end(), es.begin(), es.end());
            if (round < 2) check_eq(m.state().f[1].posture, std::int32_t{110 * (round + 1)});
        }
        check(any_kind(all, Event::Kind::guard_break), "the third blocked 5HP breaks a guard of 300");
        // Broken again from the start: staggered, unable to block; the first hit resets posture, steadies, scales.
        Match b(rules(), thin, thin);
        guarding(b, 120_fx);
        State st = b.state();
        st.f[1].posture = 300;
        st.f[1].mode = Mode::broken;
        st.f[1].stun = 40;
        b.restore(st);
        auto es = exchange(b, HP, hold, 20);
        auto hit = std::ranges::find(es, Event::Kind::hit, &Event::kind);
        check(hit != es.end() && hit->damage == 800 * 80 / 100, std::format("a broken guard is hit, scaled from 80%: {}", hit != es.end() ? hit->damage : -1));
        check(b.state().f[1].posture == 0 && b.state().f[1].steady > 0, "posture back to 0, steadied");
    });

    s.test("P-1 posture recovers after a quiet while: half holding back, double going forward", [] {
        std::vector<std::int32_t> recovered;
        for (const int dir : {5, 6, 4}) {  // fighter 2 faces left: 6 is back, 4 forward
            Match m = apart(600_fx);
            State st = m.state();
            st.f[1].posture = 500;
            m.restore(st);
            for (int f = 0; f < 45; ++f) m.tick(pad(5), pad(5));
            const std::int32_t before = m.state().f[1].posture;
            for (int f = 0; f < 40; ++f) m.tick(pad(5), pad(dir));
            recovered.push_back(before - m.state().f[1].posture);
        }
        check(recovered[0] == 120 && recovered[1] == 60 && recovered[2] == 240, std::format("{}", recovered));
    });

    s.test("P-2 clashes: equal and even, nothing changes; heavier wins; fresher wins; a super wins; an invulnerable move passes through", [] {
        // Two 5HPs at once, 180 apart: blades meet, bodies out of reach.
        {
            Match m = apart(180_fx);
            std::vector<Event> es;
            for (int f = 0; f < 40; ++f) {
                m.tick(f == 0 ? pad(5, HP) : pad(5), f == 0 ? pad(5, HP) : pad(5));
                es.insert(es.end(), m.events().begin(), m.events().end());
                if (f == 12) check(m.state().f[0].mode == m.state().f[1].mode, "both in the same state");
            }
            auto c = std::ranges::find(es, Event::Kind::clash, &Event::kind);
            check(c != es.end() && c->attacker == -1, "a perfect clash");
            check(!any_kind(es, Event::Kind::hit) && m.state().f[0].posture == 0 && m.state().f[1].posture == 0 && m.state().f[0].health == 10000 &&
                      m.state().f[1].health == 10000,
                  "nothing changes");
        }
        // 5MP against 5HP, both fresh: the heavier wins; the loser takes its posture and recovers 8 later.
        {
            Match m = apart(180_fx);
            std::vector<Event> es;
            for (int f = 0; f < 40; ++f) {
                m.tick(f == 3 ? pad(5, MP) : pad(5), f == 0 ? pad(5, HP) : pad(5));
                es.insert(es.end(), m.events().begin(), m.events().end());
            }
            auto c = std::ranges::find(es, Event::Kind::clash, &Event::kind);
            check(c != es.end() && c->attacker == 1, std::format("the 5HP wins: {}", c != es.end() ? c->attacker : -9));
            check_eq(m.state().f[0].posture, std::int32_t{110});
        }
        // A long 8LK met 5 frames into its active ones by a fresh 5LP: the fresher wins.
        {
            Match m = apart(160_fx);
            std::vector<Event> es;
            for (int f = 0; f < 40; ++f) {
                m.tick(f == 0 ? pad(8, LK) : pad(5), f == 5 ? pad(5, LP) : pad(5));
                es.insert(es.end(), m.events().begin(), m.events().end());
            }
            auto c = std::ranges::find(es, Event::Kind::clash, &Event::kind);
            check(c != es.end() && c->attacker == 1, std::format("the fresh jab wins: {}", c != es.end() ? c->attacker : -9));
        }
        // A super against a fresh 5HP: the super wins.
        {
            Match m = apart(180_fx);
            State st = m.state();
            st.f[0].meter = 3000;
            m.restore(st);
            const std::array motion{pad(2), pad(1), pad(4), pad(2), pad(1), pad(4, LP)};
            std::vector<Event> es;
            for (int f = 0; f < 50; ++f) {
                m.tick(f < 6 ? motion[static_cast<std::size_t>(f)] : pad(5), f == 7 ? pad(5, HP) : pad(5));
                es.insert(es.end(), m.events().begin(), m.events().end());
            }
            auto c = std::ranges::find(es, Event::Kind::clash, &Event::kind);
            check(c != es.end() && c->attacker == 0, std::format("the super wins: {}", c != es.end() ? c->attacker : -9));
        }
        // An anti-air invulnerable to the air: a jumping strike passes through it, no clash.
        {
            Match m = apart(60_fx);
            State st = m.state();
            st.f[0].mode = Mode::move;
            st.f[0].move = rule().find("j.HP");
            st.f[0].move_frame = 7;
            st.f[0].pos.y = 100_fx;
            st.f[0].vel = {0_fx, -1_fx};
            st.f[1].mode = Mode::move;
            st.f[1].move = rule().find("623P");
            st.f[1].move_frame = 5;
            st.f[1].facing = -1;
            m.restore(st);
            m.tick(pad(5), pad(5));
            check(!has(m, Event::Kind::clash), "no blades meet through invulnerability");
        }
    });

    s.test("P-3 deflects: a fresh back just before the hit; not a back held long, not one too early, not against a super; again for a string", [] {
        auto blocked_with = [](std::span<const Pad> guard) {
            Match m = apart(120_fx);
            return exchange(m, HP, guard, 40);
        };
        // 5HP is active on its frame 8: the hit lands on tick 8 (index 7).
        std::vector<Pad> fresh(40, pad(6));
        for (int f = 0; f < 5; ++f) fresh[static_cast<std::size_t>(f)] = pad(5);  // back from tick 6: two frames before the hit
        auto es = blocked_with(fresh);
        check(any_kind(es, Event::Kind::deflect) && !any_kind(es, Event::Kind::block), "deflected");
        std::vector<Pad> early(40, pad(6));
        for (int f = 0; f < 2; ++f) early[static_cast<std::size_t>(f)] = pad(5);  // back from tick 3: too early
        es = blocked_with(early);
        check(any_kind(es, Event::Kind::block) && !any_kind(es, Event::Kind::deflect), "too early: a block");
        // Held all along (and long before): a block, and the posture it costs.
        Match held = apart(120_fx);
        guarding(held, 120_fx);
        const std::array hold{pad(6)};
        es = exchange(held, HP, hold, 40);
        check(any_kind(es, Event::Kind::block) && held.state().f[1].posture == 110, "held: a block");
        // A deflect leaves the attacker 4 frames worse off than a block (and neither's posture changes).
        auto advantage = [](std::span<const Pad> guard) {
            Match m = apart(120_fx);
            m.tick(pad(5, HP), guard[0]);
            int hit = -1;
            for (int f = 1; f < 30 && hit < 0; ++f) {
                m.tick(pad(5), guard[static_cast<std::size_t>(f)]);
                if (has(m, Event::Kind::block) || has(m, Event::Kind::deflect)) hit = f;
            }
            int att = -1, def = -1;
            for (int f = 1; f < 80 && (att < 0 || def < 0); ++f) {
                m.tick(pad(5), pad(6));
                if (att < 0 && m.recovered(0)) att = f;
                if (def < 0 && m.recovered(1)) def = f;
            }
            return std::pair{def - att, m.state().f[1].posture};
        };
        std::vector<Pad> fresh_long(40, pad(6)), early_long(40, pad(6));
        for (int f = 0; f < 5; ++f) fresh_long[static_cast<std::size_t>(f)] = pad(5);
        for (int f = 0; f < 2; ++f) early_long[static_cast<std::size_t>(f)] = pad(5);
        const auto [d_adv, d_posture] = advantage(fresh_long);
        const auto [b_adv, b_posture] = advantage(early_long);
        check(b_adv - d_adv == 4 && d_posture == 0 && b_posture == 110, std::format("deflect {:+} / block {:+}; posture {} / {}", d_adv, b_adv, d_posture, b_posture));
        // A super is not deflected.
        {
            Match m = apart(160_fx);
            State st = m.state();
            st.f[0].meter = 3000;
            m.restore(st);
            const std::array motion{pad(2), pad(1), pad(4), pad(2), pad(1), pad(4, LP)};
            std::vector<Event> all;
            for (int f = 0; f < 40; ++f) {
                m.tick(f < 6 ? motion[static_cast<std::size_t>(f)] : pad(5), f >= 11 ? pad(6) : pad(5));
                all.insert(all.end(), m.events().begin(), m.events().end());
            }
            check(any_kind(all, Event::Kind::block) && !any_kind(all, Event::Kind::deflect), "a super is blocked, never deflected");
        }
        // A string: 5LP chained into 5LP. Where its two hits land (blocked), then each met by a tap two frames before.
        {
            auto play = [](const std::vector<bool>& back, bool guard_first) {
                Match m = apart(90_fx);
                if (guard_first) guarding(m, 90_fx);  // a guard held long before: blocks, not deflects
                else
                    for (int f = 0; f < 12; ++f) m.tick(pad(5), pad(5));  // the same frames, neutral
                std::vector<std::pair<int, Event::Kind>> at;
                for (int f = 0; f < 40; ++f) {
                    m.tick(f == 0 || f == 6 ? pad(5, LP) : pad(5), back[static_cast<std::size_t>(f)] ? pad(6) : pad(5));
                    for (const Event& e : m.events()) at.emplace_back(f, e.kind);
                }
                return at;
            };
            std::vector<bool> held(40, true);
            const auto plain = play(held, true);
            std::vector<int> lands;
            for (const auto& [f, k] : plain)
                if (k == Event::Kind::block) lands.push_back(f);
            check_eq(lands.size(), std::size_t{2});
            if (lands.size() == 2) {
                std::vector<bool> taps(40, false);
                for (const int f : lands)
                    for (int k = f - 2; k <= f + 4 && k < 40; ++k) taps[static_cast<std::size_t>(k)] = true;
                const auto deflected = play(taps, false);
                check_eq(std::ranges::count(deflected, Event::Kind::deflect, &std::pair<int, Event::Kind>::second), std::ptrdiff_t{2});
            }
        }
    });

    s.test("P-4 three bars; a super's scaling locked once, never under its level's floor; no meter back from a super", [] {
        check_eq(SystemDef{}.meter_max, std::int32_t{3000});
        SystemDef steep = rules();
        steep.scaling = {100, 50, 20};
        Match m(steep, rule(), rule());
        State st = m.state();
        const Fixed mid = m.system().stage_width / 2;
        st.f[0].pos.x = mid - 200_fx;
        st.f[1].pos.x = mid + 200_fx;
        st.f[0].meter = 1000;
        st.f[1].combo = 2;  // already deep in a combo: 20%
        st.f[1].mode = Mode::hitstun;
        st.f[1].stun = 200;
        m.restore(st);
        const std::array super{pad(2), pad(3), pad(6), pad(2), pad(3), pad(6, LP)};
        std::vector<std::int32_t> damage;
        for (int f = 0; f < 120; ++f) {
            m.tick(f < 6 ? super[static_cast<std::size_t>(f)] : pad(5), pad(5));
            for (const Event& e : m.events())
                if (e.kind == Event::Kind::hit) damage.push_back(e.damage);
        }
        check(damage == std::vector<std::int32_t>{150, 150, 150}, std::format("{} (30% of 500, three times)", damage));
        check_eq(m.state().f[0].meter, std::int32_t{0});
    });

    s.test("P-5 a level-3 super's cinematic: its first hit locks both; its hits land on their frames; the timer stands; then each where it says", [] {
        const std::array finale{pad(2), pad(3), pad(6), pad(2), pad(3), pad(6, LK)};
        // On hit.
        {
            Match m = apart(120_fx);
            State st = m.state();
            st.f[0].meter = 3000;
            m.restore(st);
            std::vector<std::pair<std::int32_t, Event>> seen;
            std::int32_t timer_at_lock = -1, lock_frames = 0;
            Fixed start;
            for (int f = 0; f < 260; ++f) {
                m.tick(f < 6 ? finale[static_cast<std::size_t>(f)] : pad(5), pad(5));
                if (has(m, Event::Kind::cinematic)) start = m.state().lock.x;
                for (const Event& e : m.events()) seen.emplace_back(m.state().frame, e);
                if (m.state().lock.owner == 0) {
                    ++lock_frames;
                    if (timer_at_lock < 0) timer_at_lock = m.state().timer;
                    check(m.state().timer == timer_at_lock, "the timer stands still");
                }
            }
            auto cine = std::ranges::find_if(seen, [](const auto& p) { return p.second.kind == Event::Kind::cinematic; });
            check(cine != seen.end(), "the cinematic began");
            std::vector<std::int32_t> after;
            for (const auto& [frame, e] : seen)
                if (cine != seen.end() && frame > cine->first && e.kind == Event::Kind::hit) after.push_back(frame - cine->first);
            check(after == std::vector<std::int32_t>{20, 40, 60}, std::format("hits on the lock's frames {}", after));
            check_eq(lock_frames, 100);  // the frame it begins on, then its frames 1–99; the 100th ends it
            check_eq(m.state().f[1].health, 10000 - 300 - 3 * 1000);
            check(m.state().f[1].pos.x == start + 150_fx && m.state().f[0].pos.x == start, "where it says");
        }
        // Blocked: no cinematic.
        {
            Match m = apart(120_fx);
            guarding(m, 120_fx);
            State st = m.state();
            st.f[0].meter = 3000;
            m.restore(st);
            bool cinematic = false, blocked = false;
            for (int f = 0; f < 120; ++f) {
                m.tick(f < 6 ? finale[static_cast<std::size_t>(f)] : pad(5), pad(6));
                cinematic = cinematic || has(m, Event::Kind::cinematic);
                blocked = blocked || has(m, Event::Kind::block);
            }
            check(blocked && !cinematic, "a blocked level 3 is only a heavy block");
        }
        // A KO in the lock waits for its end.
        {
            Match m = apart(120_fx);
            State st = m.state();
            st.f[0].meter = 3000;
            st.f[1].health = 1500;
            m.restore(st);
            std::int32_t over_at = -1, cine_at = -1;
            for (int f = 0; f < 260 && over_at < 0; ++f) {
                m.tick(f < 6 ? finale[static_cast<std::size_t>(f)] : pad(5), pad(5));
                if (has(m, Event::Kind::cinematic)) cine_at = m.state().frame;
                if (m.state().over) over_at = m.state().frame;
            }
            check(cine_at > 0 && over_at == cine_at + 100 && m.state().winner == 0, std::format("over at {} (lock from {})", over_at, cine_at));
        }
    });

    s.test("all of it rolled back: 5,000 frames of motions, dashes, supers, juggles, throws, clashes, deflects, guard breaks and a cinematic, every rollback the bytes of a straight run; the hash pinned", [] {
        // Inputs a player could give this fighter: held directions, presses, and now and then a whole motion.
        struct Rng {
            std::uint64_t s;
            std::uint32_t next() {
                s ^= s << 13, s ^= s >> 7, s ^= s << 17;
                return static_cast<std::uint32_t>(s >> 11);
            }
        };
        // Directions as the player on the left gives them; the right one's are mirrored.
        // The left one presses; the right one mostly guards (holding back, low or high), and fights back now and then.
        auto script = [](std::uint64_t seed, int frames, bool right) {
            Rng r{seed};
            std::vector<Pad> out;
            int held = 5;
            while (static_cast<int>(out.size()) < frames) {
                const std::uint32_t v = r.next();
                switch (v % 20) {
                    case 16: for (int k = 0; k < 8; ++k) out.push_back(pad(4)); break;           // guard a while: posture
                    case 17: for (int k = 0; k < 10; ++k) out.push_back(pad(k < 8 ? 5 : 4)); break;  // a fresh back: a deflect, if timed
                    case 18: for (int k = 0; k < 6; ++k) out.push_back(pad(6, k == 0 ? MP : 0)); break;  // press forward with a medium
                    case 19: out.push_back(pad(5, HP)); break;
                    case 0: for (Pad p : {pad(2), pad(3), pad(6, LP)}) out.push_back(p); break;
                    case 1: for (Pad p : {pad(6), pad(2), pad(3, HP)}) out.push_back(p); break;
                    case 2: for (Pad p : {pad(6), pad(5), pad(6)}) out.push_back(p); break;
                    case 3: for (Pad p : {pad(6), pad(6), pad(6, LK)}) out.push_back(p); break;
                    case 4: for (Pad p : {pad(2), pad(3), pad(6), pad(2), pad(3), pad(6, LP)}) out.push_back(p); break;
                    case 5: out.push_back(pad(held, static_cast<std::uint16_t>(LP | LK))); break;
                    case 6: out.push_back(pad(2, HP)); break;
                    case 7: out.push_back(pad(8, LK)); break;
                    default: {
                        constexpr std::array<int, 10> press{5, 6, 6, 6, 3, 3, 1, 9, 6, 2};  // mostly forward: they meet
                        constexpr std::array<int, 10> guard{4, 4, 4, 1, 1, 4, 6, 5, 1, 4};
                        if (v % 3 == 0) held = (right ? guard : press)[(v >> 6) % press.size()];
                        const std::uint16_t b = v % 4 == 1 ? static_cast<std::uint16_t>(1u << ((v >> 9) % 6)) : 0;
                        out.push_back(pad(held, b));
                    }
                }
            }
            out.resize(static_cast<std::size_t>(frames));
            if (right)
                for (Pad& p : out) {
                    const bool l = p.directions & Pad::left, rt = p.directions & Pad::right;
                    p.directions = static_cast<std::uint16_t>((p.directions & ~(Pad::left | Pad::right)) | (l ? Pad::right : 0) | (rt ? Pad::left : 0));
                }
            return out;
        };
        constexpr int frames = 5000;
        auto a = script(7, frames, false), b = script(31, frames, true);
        // First, scripted: a pressure string into a held guard until it breaks;
        // two 5HPs meeting; 5HPs met by timed back taps; a launcher and its
        // follow-ups into an air reset. Then the random rest.
        for (int g = 0; g < 1800; ++g) {
            Pad pa = pad(6), pb = pad(5);
            // The level 3 first, while they stand close; then the rest.
            if (g < 200) {
                a[static_cast<std::size_t>(g)] = g < 6 ? std::array{pad(2), pad(3), pad(6), pad(2), pad(3), pad(6, LK)}[static_cast<std::size_t>(g)] : pad(5);
                b[static_cast<std::size_t>(g)] = pad(5);
                continue;
            }
            const int f = g - 200;
            const int c = f % 40;  // a 40-frame cycle: a 5HP, then walking in again
            if (f < 900) pa = c == 0 ? pad(5, HP) : pad(6), pb = pad(6);  // the right one faces left: 6 is its back (to the wall, then guarding)
            else if (f < 1040) pa = c == 0 ? pad(5, HP) : pad(6), pb = c == 0 ? pad(5, HP) : pad(4);
            else if (f < 1360) pa = c == 0 ? pad(5, HP) : pad(6), pb = c >= 6 && c < 12 ? pad(6) : pad(4);  // back tapped as the 5HP comes
            else pa = f % 40 == 0 ? pad(2, HP) : f % 6 == 3 ? pad(8, LK) : pad(6), pb = pad(5);
            a[static_cast<std::size_t>(g)] = pa;
            b[static_cast<std::size_t>(g)] = pb;
        }
        FighterDef tough = body();
        tough.health = 1 << 24;
        tough.posture_max = 250;  // guards break often enough to be rolled back through
        const Fighter t = *compile(tough);
        Match straight(rules(), t, t);
        State st = straight.state();
        st.f[0].meter = st.f[1].meter = 3000;  // supers from the start, the level 3 too
        st.f[0].pos.x = 900_fx;                 // close: they fight
        st.f[1].pos.x = 1020_fx;
        straight.restore(st);
        std::vector<std::uint64_t> hashes;
        std::array<int, 8> seen{};
        std::array<int, 4> events{};  // clashes, deflects, guard breaks, hits
        int blocks = 0, cinematics = 0;
        int most_posture = 0;
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
            most_posture = std::max({most_posture, straight.state().f[0].posture, straight.state().f[1].posture});
            for (const Event& e : straight.events()) {
                if (e.kind == Event::Kind::clash) ++events[0];
                if (e.kind == Event::Kind::deflect) ++events[1];
                if (e.kind == Event::Kind::guard_break) ++events[2];
                if (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter) ++events[3];
                if (e.kind == Event::Kind::block) ++blocks;
                if (e.kind == Event::Kind::cinematic) ++cinematics;
            }
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
        check(events[0] > 0 && events[1] > 0 && events[2] > 0 && cinematics > 0,
              std::format("clashes {}, deflects {}, guard breaks {}, cinematics {}", events[0], events[1], events[2], cinematics));
        check(seen[0] > 0 && seen[1] > 0 && seen[2] > 0 && seen[3] > 0 && seen[4] > 0 && seen[5] > 0,
              std::format("resets {}, thrown {}, dashes {}, supers {}, flashes {}, projectiles {}", seen[0], seen[1], seen[2], seen[3], seen[4], seen[5]));
        std::println("    rules run: clashes {}, deflects {}, guard breaks {}, hits {}, blocks {} (posture up to {}); resets {}", events[0], events[1], events[2], events[3], blocks,
                     most_posture, seen[0]);
        std::println("    rules run hash {:016x}", hashes.back());
        check(hashes.back() == 0x5ebf172e964842d7ULL, std::format("run hash {:016x}", hashes.back()));  // GCC, LLVM and wasm
    });

    return s.run(argc, argv);
}
