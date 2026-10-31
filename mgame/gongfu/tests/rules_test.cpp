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

    // 岳's: command grabs by rotation, armour, an anti-air throw (G-16..G-18).
    MoveDef spin{.name = "360P", .command = {.motion = rotation(1), .buttons = punches}, .total = 40, .priority = 30, .clash = false};
    spin.throw_ = ThrowDef{.active = {1, 2}, .range = {0_fx, 0_fx, 95_fx, 150_fx}, .damage = 2200, .tech_window = 0};
    f.moves.push_back(spin);
    MoveDef spin2{.name = "720K", .command = {.motion = rotation(2), .buttons = kicks}, .total = 40, .priority = 31, .clash = false};
    spin2.throw_ = ThrowDef{.active = {1, 2}, .range = {0_fx, 0_fx, 95_fx, 150_fx}, .damage = 3000, .tech_window = 0};
    f.moves.push_back(spin2);
    MoveDef elbow{.name = "6HK", .command = {.direction = 6, .buttons = HK}, .total = 34, .priority = 2};
    elbow.armor = ArmorDef{.frames = {3, 15}, .hits = 1};
    elbow.hits.push_back(strike({16, 18}, {20_fx, 90_fx, 90_fx, 50_fx}, 900, 24));
    f.moves.push_back(elbow);
    MoveDef catcher{.name = "623K", .command = {.motion = dragon, .buttons = kicks}, .total = 36, .priority = 11, .clash = false};
    catcher.throw_ = ThrowDef{.active = {5, 8}, .range = {0_fx, 60_fx, 90_fx, 160_fx}, .damage = 1600, .tech_window = 0, .air = true};
    f.moves.push_back(catcher);

    // 璃's: panes (set pieces, G-15) placed, shattered, warped to; a gallery
    // that turns projectiles back; shards that only panes start.
    auto shard = [](std::string name, std::int32_t damage) {
        MoveDef m{.name = std::move(name), .total = 1, .asked = false};
        m.projectile = ProjectileDef{.spawn = 1, .offset = {0_fx, 60_fx}, .velocity = {9_fx, 0_fx}, .life = 80,
                                     .hit = strike({1, 80}, {-15_fx, -10_fx, 30_fx, 20_fx}, damage, 18, 12)};
        return m;
    };
    f.moves.push_back(shard("shard", 200));
    f.moves.push_back(shard("glint", 330));
    MoveDef pane{.name = "236K", .command = {.motion = quarter_forward, .buttons = kicks}, .total = 34, .priority = 10};
    pane.pieces.push_back({.act = PieceAct::place, .frame = 14, .at = {120_fx}, .shard = "shard"});
    f.moves.push_back(pane);
    MoveDef burst{.name = "214K", .command = {.motion = quarter_back, .buttons = kicks}, .total = 28, .priority = 10};
    burst.pieces.push_back({.act = PieceAct::shatter, .frame = 8});
    f.moves.push_back(burst);
    MoveDef step{.name = "22K", .command = {.motion = motion({2, 5, 2}, 12), .buttons = kicks}, .total = 30, .priority = 10, .needs_piece = true};
    step.pieces.push_back({.act = PieceAct::warp, .frame = 12});
    step.invulnerable.push_back({.frames = {6, 16}, .to = to_strike});
    f.moves.push_back(step);
    MoveDef gallery{.name = "22P", .command = {.motion = motion({2, 5, 2}, 12), .buttons = punches}, .total = 40, .priority = 15,
                    .meter_cost = 1000, .freeze = 30, .level = 1};
    gallery.pieces.push_back({.act = PieceAct::place, .frame = 10, .at = {100_fx, 200_fx, 300_fx}, .limit = 3, .reflects = 1, .shard = "shard"});
    f.moves.push_back(gallery);
    MoveDef glints{.name = "214214K", .command = {.motion = motion({2, 1, 4, 2, 1, 4}, 24), .buttons = kicks}, .total = 40, .priority = 20,
                   .meter_cost = 2000, .freeze = 40, .level = 2};
    glints.pieces.push_back({.act = PieceAct::shatter, .frame = 10, .at = {60_fx}, .at_opponent = true, .shard = "glint", .shards = 3});
    f.moves.push_back(glints);

    // 刻's: a hand that comes back, a stall, a mark, a rewind to it, a clone (G-19..G-22).
    MoveDef hand{.name = "214P", .command = {.motion = quarter_back, .buttons = punches}, .total = 36, .priority = 10};
    hand.projectile = ProjectileDef{.spawn = 10, .offset = {50_fx, 110_fx}, .velocity = {7_fx, 0_fx}, .life = 200, .limit = 1, .turn = 40};
    hand.projectile->hit = strike({1, 200}, {-25_fx, -10_fx, 50_fx, 20_fx}, 350, 18, 6);
    f.moves.push_back(hand);
    MoveDef pendulum{.name = "6MK", .command = {.direction = 6, .buttons = MK}, .total = 26, .priority = 2};
    pendulum.hits.push_back(strike({9, 10}, {10_fx, 70_fx, 100_fx, 60_fx}, 500, 20));
    pendulum.hits[0].stall = 20;
    f.moves.push_back(pendulum);
    MoveDef engrave{.name = "4MK", .command = {.direction = 4, .buttons = MK}, .total = 20, .priority = 2};
    engrave.time.push_back({.act = TimeAct::mark, .frame = 6});
    f.moves.push_back(engrave);
    MoveDef rewind{.name = "63214K", .command = {.motion = half_back, .buttons = kicks}, .total = 12, .priority = 25, .meter_cost = 1000, .freeze = 30,
                   .level = 1};
    rewind.time.push_back({.act = TimeAct::rewind, .frame = 1});
    f.moves.push_back(rewind);
    MoveDef echo{.name = "41236K", .command = {.motion = motion({4, 1, 2, 3, 6}, 20), .buttons = kicks}, .total = 30, .priority = 25,
                 .meter_cost = 2000, .freeze = 40, .level = 2};
    echo.time.push_back({.act = TimeAct::clone, .frame = 1});
    f.moves.push_back(echo);

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

    s.test("G-16 armour: a strike on its frames does its damage and the move goes on; a second one stops it; a super goes through", [] {
        // Fighter 2 in 6HK's armour; fighter 1's jab lands on it.
        auto armoured = [](int jabs) {
            Match m = apart(80_fx);
            State st = m.state();
            st.f[1].mode = Mode::move;
            st.f[1].move = rule().find("6HK");
            st.f[1].move_frame = 3;
            st.f[1].facing = -1;
            m.restore(st);
            std::vector<Event> all;
            for (int f = 0; f < 30; ++f) {
                m.tick(f == 0 || (jabs > 1 && f == 6) ? pad(5, LP) : pad(5), pad(5));  // the second within the jab's cancel
                all.insert(all.end(), m.events().begin(), m.events().end());
            }
            return std::pair{all, m.state()};
        };
        auto [one, after_one] = armoured(1);
        check(std::ranges::count(one, Event::Kind::armor, &Event::kind) == 1 && after_one.f[1].health == 10000 - 300, "the jab's damage, taken");
        check(std::ranges::any_of(one, [](const Event& e) { return e.kind == Event::Kind::hit && e.attacker == 1; }), "and the elbow still lands");
        auto [two, after_two] = armoured(2);
        check(std::ranges::any_of(two, [](const Event& e) { return (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter) && e.attacker == 0; }),
              "a second jab: the armour is spent, it is hit (a counter: the elbow is still starting)");
    });

    s.test("G-17 rotations: 360 by the four sides, 720 twice round, up not a jump; a half circle is not 360", [] {
        // A long stick motion goes through up: done where a jump cannot start (here, blockstun), as players do a 720.
        auto grabbed = [](std::vector<Pad> stick, int stun = 0) {
            Match m = apart(80_fx);
            if (stun > 0) {
                State st = m.state();
                st.f[0].mode = Mode::blockstun;
                st.f[0].stun = stun;
                m.restore(st);
            }
            std::vector<Event> all;
            for (std::size_t f = 0; f < stick.size() + 10; ++f) {
                m.tick(f < stick.size() ? stick[f] : pad(5), pad(5));
                all.insert(all.end(), m.events().begin(), m.events().end());
                if (std::ranges::any_of(m.events(), [](const Event& e) { return e.kind == Event::Kind::throw_; })) break;
                if (m.state().f[1].mode == Mode::thrown) return std::string(rule().def.moves[static_cast<std::size_t>(m.state().f[0].move)].name);
            }
            return std::string("-");
        };
        check_eq(grabbed({pad(6), pad(3), pad(2), pad(1), pad(4), pad(7), pad(8), pad(9, LP)}), std::string("360P"));
        check_eq(grabbed({pad(2), pad(1), pad(4), pad(7), pad(8), pad(9), pad(6, LP)}), std::string("360P"));
        check(grabbed({pad(6), pad(3), pad(2), pad(1), pad(4, LP)}) != "360P", "a half circle is not 360");
        check_eq(grabbed({pad(6), pad(3), pad(2), pad(1), pad(4), pad(7), pad(8), pad(9), pad(6), pad(3), pad(2), pad(1), pad(4), pad(7), pad(8), pad(9, LK)}, 15),
                 std::string("720K"));
        check(grabbed({pad(6), pad(3), pad(2), pad(1), pad(4), pad(7), pad(8), pad(9, LK)}, 7) != "720K", "once round is not 720");
    });

    s.test("G-17 a grab super on its first frame takes hold on the frame after its flash, never during it; a grounded opponent cannot walk out", [] {
        FighterDef def = rule().def;
        MoveDef& spin = def.moves[static_cast<std::size_t>(rule().find("360P"))];
        spin.level = 1;
        spin.meter_cost = 1000;
        spin.freeze = 30;
        const Fighter f = *compile(def);
        Match m(rules(), f, f);
        State st = m.state();
        const Fixed mid = m.system().stage_width / 2;
        st.f[0].pos.x = mid - 40_fx;
        st.f[1].pos.x = mid + 40_fx;
        st.f[0].mode = Mode::blockstun;  // where the stick may go round through up
        st.f[0].stun = 7;
        st.f[0].meter = 1000;
        m.restore(st);
        const std::array stick{pad(6), pad(3), pad(2), pad(1), pad(4), pad(7), pad(8), pad(9, LP)};
        play(m, stick);
        check(m.state().freeze == 30 && m.state().f[0].move_frame == 0, std::format("the flash on the frame it is asked for: freeze {}, frame {}", m.state().freeze, m.state().f[0].move_frame));
        int during = 0;
        while (m.state().freeze > 0) {
            m.tick(pad(5), pad(4));  // the other walks away through the flash: it is kept for after
            during += m.state().f[1].mode == Mode::thrown;
        }
        check_eq(during, 0);
        m.tick(pad(5), pad(4));
        check(m.state().f[1].mode == Mode::thrown && m.state().f[0].move_frame == 1, std::format("thrown on frame {}", m.state().f[0].move_frame));
    });

    s.test("G-18 an anti-air throw takes a jumping fighter, not a standing one", [] {
        for (const bool jumping : {true, false}) {
            Match m = apart(80_fx);
            State st = m.state();
            if (jumping) {
                st.f[1].mode = Mode::air;
                st.f[1].pos.y = 100_fx;
                st.f[1].vel = {0_fx, -1_fx};
            }
            m.restore(st);
            const std::array dp{pad(6), pad(2), pad(3, LK)};
            bool thrown = false;
            for (int f = 0; f < 20; ++f) {
                m.tick(f < 3 ? dp[static_cast<std::size_t>(f)] : pad(5), pad(5));
                thrown = thrown || m.state().f[1].mode == Mode::thrown;
            }
            check(thrown == jumping, std::format("{}: thrown {}", jumping ? "jumping" : "standing", thrown));
        }
    });

    s.test("landing: a ground move that rises and comes down past the ground lasts its frames; an air move ends where it lands, at the ground or past it", [] {
        FighterDef def = rule().def;
        MoveDef kick{.name = "rising", .command = {.direction = 6, .buttons = LK}, .total = 38, .priority = 3};
        kick.steps = {{3, {2_fx, 12_fx}}, {12, {0_fx, -8_fx}}};  // up 108, down 8 a frame: past the ground on its 25th frame
        kick.hits.push_back(strike({6, 11}, {0_fx, 80_fx, 70_fx, 120_fx}, 500, 20));
        def.moves.push_back(kick);
        MoveDef flat = kick;  // the same, on the ground throughout
        flat.name = "flat";
        flat.command = {.direction = 4, .buttons = LK};
        flat.steps.clear();
        def.moves.push_back(flat);
        const Fighter f = *compile(def);
        auto lasts = [&](Pad press) {
            Match m(rules(), f, f);
            m.tick(press, pad(5));
            int frames = 1;
            while (m.state().f[0].mode == Mode::move && frames < 80) {
                m.tick(pad(5), pad(5));
                ++frames;
            }
            return frames;
        };
        check_eq(lasts(pad(6, LK)), lasts(pad(4, LK)));
        // A jump's arc comes back to the ground exactly: a jumping heavy pressed at the top ends as it lands.
        Match j(rules(), rule(), rule());
        j.tick(pad(8), pad(5));
        while (j.state().f[0].vel.y > Fixed{} || j.state().f[0].mode != Mode::air) j.tick(pad(5), pad(5));
        j.tick(pad(5, HP), pad(5));
        while (j.state().f[0].pos.y > Fixed{}) j.tick(pad(5), pad(5));
        check(j.state().f[0].mode != Mode::move, std::format("landed: mode {}", static_cast<int>(j.state().f[0].mode)));
    });

    s.test("G-15 a pane stops the other side's projectiles, one of its durability a hit, and is gone at none; strikes and bodies pass it, a strike costing it once", [] {
        Match m = apart(500_fx);
        const std::array pane{pad(2), pad(3), pad(6, LK)};
        play(m, pane);
        for (int f = 0; f < 14; ++f) m.tick(pad(5), pad(5));
        auto standing = [&] { return std::ranges::count_if(m.state().pieces, [](const PieceState& q) { return q.alive != 0; }); };
        check(standing() == 1 && m.state().pieces[0].durability == 2 && m.state().pieces[0].pos.x == m.state().f[0].pos.x + 120_fx,
              std::format("one pane, 120 in front: {}", standing()));
        // Fighter 2 (facing left: 214 on the screen is its 236) throws two glyphs; the pane takes both, and is gone.
        int stopped = 0;
        for (int shot = 0; shot < 2; ++shot) {
            const std::array fireball{pad(2), pad(1), pad(4, LP)};
            play(m, {}, fireball);
            for (int f = 0; f < 90; ++f) {
                m.tick(pad(5), pad(5));
                stopped += count(m, Event::Kind::piece_hit);
            }
        }
        check(stopped == 2 && standing() == 0 && m.state().f[0].health == 10000, std::format("stopped {}, standing {}, health {}", stopped, standing(), m.state().f[0].health));
        // Another; fighter 2 walks through it, then jabs fighter 1 through it: the jab lands, and costs the pane 1, once.
        play(m, pane);
        for (int f = 0; f < 14; ++f) m.tick(pad(5), pad(5));
        const Fixed pane_x = m.state().pieces[0].pos.x;
        while (m.state().f[1].pos.x - m.state().f[0].pos.x > 80_fx) m.tick(pad(5), pad(4));
        check(m.state().f[1].pos.x < pane_x, "walked past the pane");
        State st = m.state();
        st.pieces[0].pos.x = st.f[0].pos.x + 40_fx;  // between them, in the jab's reach
        m.restore(st);
        std::vector<Event> seen;
        for (int f = 0; f < 20; ++f) {
            m.tick(pad(5), f == 0 ? pad(5, LP) : pad(5));
            seen.insert(seen.end(), m.events().begin(), m.events().end());
        }
        check(any_kind(seen, Event::Kind::hit) && std::ranges::count(seen, Event::Kind::piece_hit, &Event::kind) == 1 && m.state().pieces[0].durability == 1,
              std::format("pane at {}", m.state().pieces[0].durability));
    });

    s.test("G-15 her own strike shatters her pane into three shards that fly at the other fighter; 214K shatters them all; the oldest gives way; a pane's life", [] {
        Match m = apart(300_fx);
        const std::array pane{pad(2), pad(3), pad(6, LK)};
        play(m, pane);
        for (int f = 0; f < 30; ++f) m.tick(pad(5), pad(5));
        State st = m.state();
        st.pieces[0].pos.x = st.f[0].pos.x + 50_fx;  // in her jab's reach
        m.restore(st);
        std::vector<Event> seen;
        int shards = 0;
        for (int f = 0; f < 60; ++f) {
            m.tick(f == 0 ? pad(5, LP) : pad(5), pad(5));
            seen.insert(seen.end(), m.events().begin(), m.events().end());
            shards = std::max<int>(shards, static_cast<int>(std::ranges::count_if(m.state().p, [](const ProjectileState& p) { return p.alive && p.from == 0; })));
        }
        check(any_kind(seen, Event::Kind::shatter) && shards == 3, std::format("shards {}", shards));
        check(std::ranges::count_if(seen, [](const Event& e) { return e.kind == Event::Kind::hit && e.attacker == 0; }) >= 1, "a shard hit");
        // Three placed, two stand (the limit): the first is gone; 214K shatters both.
        Match n = apart(600_fx);
        std::array<std::int32_t, 3> placed{};
        for (int k = 0; k < 3; ++k) {
            play(n, pane);
            for (int f = 0; f < 36; ++f) n.tick(pad(5), pad(5));
            placed[static_cast<std::size_t>(k)] = n.state().frame;
        }
        const auto alive = [](const Match& x) { return std::ranges::count_if(x.state().pieces, [](const PieceState& q) { return q.alive != 0; }); };
        check(alive(n) == 2, std::format("{} standing", alive(n)));
        const std::array burst{pad(2), pad(1), pad(4, LK)};
        play(n, burst);
        int bursts = 0;
        for (int f = 0; f < 12; ++f) {
            n.tick(pad(5), pad(5));
            bursts += count(n, Event::Kind::shatter);
        }
        check(bursts == 2 && alive(n) == 0, std::format("{} shattered", bursts));
        // Left alone, a pane is gone after its 480 frames.
        Match o = apart(900_fx);
        play(o, pane);
        int life = 0;
        for (int f = 0; f < 600 && (life == 0 || alive(o) > 0); ++f) {
            o.tick(pad(5), pad(5));
            if (alive(o) > 0) ++life;
        }
        check_eq(life, 480);
    });

    s.test("G-15 a gallery's pane turns a projectile back once — it hits its thrower, and gives no one meter — then shatters; a super's is stopped, not turned", [] {
        FighterDef def = rule().def;
        def.moves[static_cast<std::size_t>(rule().find("22P"))].pieces[0].shards = 0;  // no shards here: only the reflected glyph hits
        const Fighter f = *compile(def);
        auto gallery = [&](std::vector<Pad> other) {
            Match m(rules(), f, f);
            State st = m.state();
            st.f[0].pos.x = 500_fx;
            st.f[1].pos.x = 1100_fx;
            st.f[0].meter = 1000;
            st.f[1].meter = 1000;
            m.restore(st);
            const std::array call{pad(2), pad(5), pad(2, LP)};
            play(m, call);
            while (m.state().freeze > 0 || m.state().f[0].mode == Mode::move) m.tick(pad(5), pad(5));
            check(std::ranges::count_if(m.state().pieces, [](const PieceState& q) { return q.alive && q.reflects == 1; }) == 3, "three panes that turn one back");
            play(m, {}, other);
            std::vector<Event> seen;
            for (int fr = 0; fr < 200; ++fr) {
                m.tick(pad(5), pad(5));
                seen.insert(seen.end(), m.events().begin(), m.events().end());
            }
            return std::pair{m.state(), seen};
        };
        const auto [after, seen] = gallery({pad(2), pad(1), pad(4, LP)});
        check(any_kind(seen, Event::Kind::reflect) && any_kind(seen, Event::Kind::shatter), "turned back; the pane shattered");
        check(after.f[1].health == 10000 - 600 && after.f[0].health == 10000, std::format("the thrower hit by its own glyph: {}", after.f[1].health));
        check(after.f[0].meter == 0 && after.f[1].meter == 1000, std::format("meter {} / {}: none from the reflected hit", after.f[0].meter, after.f[1].meter));
        // A super's projectile (strength 2): stopped by the panes, never turned back.
        const auto [after_super, seen_super] = gallery({pad(2), pad(1), pad(4), pad(2), pad(1), pad(4, LP)});
        check(!any_kind(seen_super, Event::Kind::reflect) && any_kind(seen_super, Event::Kind::piece_hit) && after_super.f[1].health == 10000,
              std::format("stopped, not turned: thrower at {}", after_super.f[1].health));
    });

    s.test("G-15 22K takes her to the far side of the pane nearest the other fighter, which shatters; without a pane it is not done", [] {
        Match m = apart(500_fx);
        const std::array pane{pad(2), pad(3), pad(6, LK)};
        play(m, pane);
        for (int f = 0; f < 40; ++f) m.tick(pad(5), pad(5));
        const Fixed pane_x = m.state().pieces[0].pos.x;
        const std::array step{pad(2), pad(5), pad(2, LK)};
        play(m, step);
        check_eq(moving(m, 0), std::string_view("22K"));
        bool shattered = false;
        while (m.state().f[0].mode == Mode::move && m.state().f[0].move_frame < 12) {
            m.tick(pad(5), pad(5));
            shattered = shattered || has(m, Event::Kind::shatter);
        }
        check(shattered && m.state().f[0].pos.x == pane_x - 40_fx, std::format("at {} (the pane at {})", m.state().f[0].pos.x.to_double(), pane_x.to_double()));
        Match n = apart(500_fx);
        play(n, step);
        check(moving(n, 0) != "22K", "no pane: not done");
    });

    s.test("G-15 the level 2 with no pane standing places one before the other fighter and shatters it: three glints; a full pool takes no more", [] {
        Match m = apart(500_fx);
        State st = m.state();
        st.f[0].meter = 2000;
        m.restore(st);
        const std::array call{pad(2), pad(1), pad(4), pad(2), pad(1), pad(4, LK)};
        play(m, call);
        int glints = 0, hits = 0;
        for (int f = 0; f < 120; ++f) {
            m.tick(pad(5), pad(5));
            glints = std::max<int>(glints, static_cast<int>(std::ranges::count_if(m.state().p, [](const ProjectileState& p) { return p.alive != 0; })));
            hits += static_cast<int>(std::ranges::count_if(m.events(), [](const Event& e) { return e.kind == Event::Kind::hit && e.attacker == 0; }));
        }
        check(glints == 3 && hits == 3, std::format("glints {}, hits {}", glints, hits));
        // Every slot taken: a shatter adds none, and moves none of those flying.
        Match full = apart(500_fx);
        play(full, std::array{pad(2), pad(3), pad(6, LK)});
        for (int f = 0; f < 30; ++f) full.tick(pad(5), pad(5));
        st = full.state();
        for (ProjectileState& p : st.p) p = {1, 1, 1, rule().find("shard"), 1, -1, {1800_fx, 300_fx}, 1, 1, 0, 0, 0};
        full.restore(st);
        const std::array burst{pad(2), pad(1), pad(4, LK)};
        play(full, burst);
        for (int f = 0; f < 10; ++f) full.tick(pad(5), pad(5));
        check(std::ranges::all_of(full.state().p, [](const ProjectileState& p) { return p.from == 1; }), "the pool as it was");
    });

    s.test("G-19 a hand that comes back: it hits going, turns on its frame, comes back to her wherever she has gone, hits again, and is caught", [] {
        Match m = apart(300_fx);
        play(m, std::array{pad(2), pad(1), pad(4, LP)});
        int hits = 0;
        bool turned = false, caught = false;
        Fixed thrower;
        for (int f = 0; f < 240 && !caught; ++f) {
            m.tick(f < 50 ? pad(5) : pad(4), pad(5));  // she walks back as it comes: it follows her
            hits += static_cast<int>(std::ranges::count_if(m.events(), [](const Event& e) { return e.kind == Event::Kind::hit && e.attacker == 0; }));
            const bool flying = std::ranges::any_of(m.state().p, [](const ProjectileState& p) { return p.alive != 0; });
            turned = turned || std::ranges::any_of(m.state().p, [](const ProjectileState& p) { return p.alive && p.turned; });
            if (turned && !flying) caught = true, thrower = m.state().f[0].pos.x;
        }
        check(hits == 2 && turned && caught, std::format("hits {}, turned {}, caught {}", hits, turned, caught));
    });

    s.test("G-20 a mark, and a rewind to it (used up); with none, to where she stood 60 frames ago; on the ground, then 8 frames of landing", [] {
        Match m = apart(500_fx);
        State st = m.state();
        st.f[0].meter = 3000;
        m.restore(st);
        const Fixed start = m.state().f[0].pos.x;
        play(m, std::array{pad(4, MK)});
        for (int f = 0; f < 25; ++f) m.tick(pad(5), pad(5));
        check(m.state().history[0].marked == 1 && m.state().history[0].mark.pos.x == start, "marked where she stood");
        for (int f = 0; f < 40; ++f) m.tick(pad(6), pad(5));
        check(m.state().f[0].pos.x > start + 100_fx, "walked away from it");
        std::map<std::int32_t, Fixed> after;  // where she was after each frame
        auto rewind = [&] {
            const std::array call{pad(6), pad(3), pad(2), pad(1), pad(4, LK)};
            for (const Pad p : call) {
                m.tick(p, pad(5));
                after[m.state().frame] = m.state().f[0].pos.x;
            }
            std::int32_t at = -1;
            for (int f = 0; f < 60 && at < 0; ++f) {
                m.tick(pad(5), pad(5));
                if (has(m, Event::Kind::rewind)) at = m.state().frame;
                after[m.state().frame] = m.state().f[0].pos.x;
            }
            return at;
        };
        const std::int32_t first = rewind();
        check(first > 0 && after[first] == start && m.state().history[0].marked == 0, std::format("back at the mark: {}", after[first].to_double()));
        check(m.state().f[0].mode == Mode::landing || m.state().f[0].mode == Mode::idle, "landing after");
        // No mark: where she stood at the start of the frame 60 before.
        for (int f = 0; f < 90; ++f) {
            m.tick(pad(6), pad(5));
            after[m.state().frame] = m.state().f[0].pos.x;
        }
        const std::int32_t second = rewind();
        check(second > 0 && after[second] == after[second - 61], std::format("{} then, {} now", after[second - 61].to_double(), after[second].to_double()));
        int landing = 0;
        while (m.state().f[0].mode == Mode::landing) m.tick(pad(5), pad(5)), ++landing;
        check_eq(landing, 8);
    });

    s.test("G-21 a clone plays her last 120 frames again from where she stood: its jab lands; touched by the other side it is gone; so when she is hit; and after its frames", [] {
        // She jabs, walks back, then calls the clone (its flash counts as time too): it starts
        // where she stood 120 frames before and does it all again — the jab lands.
        auto setup = [](Match& m) {
            State st = m.state();
            st.f[0].meter = 3000;
            st.f[1].meter = 0;
            m.restore(st);
            for (int f = 0; f < 9; ++f) m.tick(pad(5), pad(5));
            m.tick(pad(5, LP), pad(5));
            for (int f = 0; f < 30; ++f) m.tick(pad(4), pad(5));
            const std::array call{pad(4), pad(1), pad(2), pad(3), pad(6, LK)};
            play(m, call);
            while (m.state().freeze > 0 || !m.state().clones[0].alive) m.tick(pad(5), pad(5));
        };
        Match m = apart(80_fx);
        const Fixed began = m.state().f[0].pos.x;
        setup(m);
        check(m.state().clones[0].alive && abs(m.state().clones[0].body.pos.x - began) < 10_fx,
              std::format("out where she stood: {} (she began at {})", m.state().clones[0].body.pos.x.to_double(), began.to_double()));
        int clone_hits = 0, frames = 0;
        while (m.state().clones[0].alive && frames < 200) {
            m.tick(pad(5), pad(5));
            ++frames;
            clone_hits += static_cast<int>(std::ranges::count_if(m.events(), [](const Event& e) { return e.kind == Event::Kind::hit && e.attacker == 0; }));
        }
        check(clone_hits == 1, std::format("its jab landed: {}", clone_hits));
        check(frames > 100 && frames <= 120, std::format("gone after its frames: {}", frames));
        // Touched by the other fighter's jab: gone.
        Match t = apart(80_fx);
        setup(t);
        for (int f = 0; f < 40; ++f) t.tick(pad(5), f == 20 ? pad(5, LP) : pad(5));
        check(!t.state().clones[0].alive, "struck, gone");
        // She is hit: gone.
        Match h = apart(80_fx);
        setup(h);
        State st = h.state();
        st.f[1].pos.x = st.f[0].pos.x + 70_fx;  // the other walks up to her
        h.restore(st);
        for (int f = 0; f < 10; ++f) h.tick(pad(5), f == 0 ? pad(5, LP) : pad(5));
        check(!h.state().clones[0].alive, "she was hit: gone");
    });

    s.test("G-22 a stall: twenty more frames of hitstun the first time in a combo, not the second", [] {
        Match m = apart(90_fx);
        const std::uint16_t hitstun = rule().def.moves[static_cast<std::size_t>(rule().find("6MK"))].hits[0].hitstun;
        std::vector<std::int32_t> stuns;
        m.tick(pad(6, MK), pad(5));
        for (int f = 0; f < 80 && stuns.size() < 2; ++f) {
            const bool again = f == 34;  // the first recovered (its hitstop too), the other still in its stall
            m.tick(again ? pad(6, MK) : pad(5), pad(5));
            if (std::ranges::any_of(m.events(), [](const Event& e) { return e.kind == Event::Kind::hit || e.kind == Event::Kind::counter; })) {
                stuns.push_back(m.state().f[1].stun);
                State st = m.state();
                st.f[1].push = Fixed{};  // kept in reach: the rule, not the spacing, is tested
                m.restore(st);
            }
        }
        check(stuns.size() == 2 && stuns[0] == hitstun + 20 && stuns[1] == hitstun && m.state().f[1].combo == 2,
              std::format("stuns {}", stuns));
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

    s.test("all of it rolled back: 5,000 frames of motions, dashes, supers, juggles, throws, clashes, deflects, guard breaks, a cinematic, panes and time, every rollback the bytes of a straight run; the hash pinned", [] {
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
                    case 8: for (Pad p : {pad(2), pad(3), pad(6, LK)}) out.push_back(p); break;  // a pane (G-15)
                    case 9: for (Pad p : {pad(2), pad(1), pad(4, LK)}) out.push_back(p); break;  // shattered
                    case 10: for (Pad p : {pad(2), pad(5), pad(2, LK)}) out.push_back(p); break;  // a step to it
                    case 11: for (Pad p : {pad(2), pad(5), pad(2, LP)}) out.push_back(p); break;  // the gallery
                    case 12: for (Pad p : {pad(2), pad(1), pad(4, LP)}) out.push_back(p); break;  // a hand that comes back (G-19)
                    case 13: out.push_back(pad(6, MK)); break;                                     // a stall (G-22)
                    case 14: out.push_back(pad(4, MK)); break;                                     // a mark (G-20)
                    case 15: for (Pad p : {pad(6), pad(3), pad(2), pad(1), pad(4, LK)}) out.push_back(p); break;  // back to it
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
        for (int g = 0; g < 2400; ++g) {
            Pad pa = pad(6), pb = pad(5);
            // The level 3 first, while they stand close; then the rest; last,
            // the right one's gallery, and a glyph thrown into it.
            if (g < 200) {
                a[static_cast<std::size_t>(g)] = g < 6 ? std::array{pad(2), pad(3), pad(6), pad(2), pad(3), pad(6, LK)}[static_cast<std::size_t>(g)] : pad(5);
                b[static_cast<std::size_t>(g)] = pad(5);
                continue;
            }
            if (g >= 2200) {  // the left one marks where it stands, walks off, rewinds to it; then a stall, and another in the combo
                const int k = g - 2200;
                b[static_cast<std::size_t>(g)] = pad(5);
                a[static_cast<std::size_t>(g)] = k == 0                ? pad(4, MK)
                                               : k >= 25 && k < 85    ? pad(6)
                                               : k >= 90 && k < 95    ? std::array{pad(6), pad(3), pad(2), pad(1), pad(4, LK)}[static_cast<std::size_t>(k - 90)]
                                               : k >= 140 && k < 150  ? pad(6)
                                               : k == 150 || k == 184 ? pad(6, MK)
                                                                      : pad(5);
                continue;
            }
            if (g >= 2000) {  // the right one's clone (facing left: its 41236 is 63214 on the screen), the left one pressing
                const int k = g - 2000;
                b[static_cast<std::size_t>(g)] = k < 5 ? std::array{pad(6), pad(3), pad(2), pad(1), pad(4, LK)}[static_cast<std::size_t>(k)] : pad(5);
                a[static_cast<std::size_t>(g)] = k % 24 == 12 ? pad(5, LP) : pad(6);
                continue;
            }
            if (g >= 1800) {
                const int k = g - 1800;
                b[static_cast<std::size_t>(g)] = k >= 40 && k < 43 ? std::array{pad(2), pad(5), pad(2, LP)}[static_cast<std::size_t>(k - 40)] : pad(5);
                a[static_cast<std::size_t>(g)] = k < 120               ? pad(4)
                                               : k >= 130 && k < 133 ? std::array{pad(2), pad(3), pad(6, LP)}[static_cast<std::size_t>(k - 130)]
                                               : k >= 180 && k < 183 ? std::array{pad(2), pad(1), pad(4, LP)}[static_cast<std::size_t>(k - 180)]  // a hand that comes back
                                                                     : pad(5);
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
        for (MoveDef& m : tough.moves)  // meter enough for supers, rewinds and clones as they come
            if (m.level == 0) m.meter_gain = std::max(m.meter_gain, 250);
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
        std::array<int, 4> panes{};  // placed, stopped or struck, turned back, shattered
        std::array<int, 6> times{};  // marks, rewinds, clones, clones gone, hands coming back, stalls
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
            times[4] += static_cast<int>(std::ranges::count_if(straight.state().p, [](const ProjectileState& p) { return p.alive && p.turned; }));
            times[5] += straight.state().f[0].stalled + straight.state().f[1].stalled;
            seen[5] += static_cast<int>(std::ranges::count_if(straight.state().p, [](const ProjectileState& p) { return p.alive != 0; }));
            most_posture = std::max({most_posture, straight.state().f[0].posture, straight.state().f[1].posture});
            for (const Event& e : straight.events()) {
                if (e.kind == Event::Kind::clash) ++events[0];
                if (e.kind == Event::Kind::deflect) ++events[1];
                if (e.kind == Event::Kind::guard_break) ++events[2];
                if (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter) ++events[3];
                if (e.kind == Event::Kind::block) ++blocks;
                if (e.kind == Event::Kind::cinematic) ++cinematics;
                if (e.kind == Event::Kind::piece) ++panes[0];
                if (e.kind == Event::Kind::piece_hit) ++panes[1];
                if (e.kind == Event::Kind::reflect) ++panes[2];
                if (e.kind == Event::Kind::shatter) ++panes[3];
                if (e.kind == Event::Kind::mark) ++times[0];
                if (e.kind == Event::Kind::rewind) ++times[1];
                if (e.kind == Event::Kind::clone) ++times[2];
                if (e.kind == Event::Kind::vanish) ++times[3];
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
        std::println("    panes: placed {}, stopped or struck {}, turned back {}, shattered {}", panes[0], panes[1], panes[2], panes[3]);
        check(panes[0] > 0 && panes[1] > 0 && panes[2] > 0 && panes[3] > 0, "panes placed, struck, turning back, shattered");
        std::println("    time: marks {}, rewinds {}, clones {} (gone {}), hands coming back {}, stalled {}", times[0], times[1], times[2], times[3], times[4], times[5]);
        check(std::ranges::all_of(times, [](int n) { return n > 0; }), "marks, rewinds, a clone and its end, hands coming back, stalls");
        std::println("    rules run hash {:016x}", hashes.back());
        check(hashes.back() == 0x50d0046d4a870b49ULL, std::format("run hash {:016x}", hashes.back()));  // GCC, LLVM and wasm
    });

    return s.run(argc, argv);
}
