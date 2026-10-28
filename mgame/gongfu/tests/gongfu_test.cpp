// The fighting framework's core (D-85) held to what a serious fighting game
// needs: fixed-point numbers with the same bits everywhere; inputs read as
// fighting games read them (motions in their windows, charges, buttons
// together, SOCD); characters checked as data; frame data that the
// simulation measures as the definition declares it, on whiff, on hit and on
// block; blocking high, low and overhead; throws and their breaks;
// projectiles; cancels and combo scaling; and rollback — ten thousand
// rollbacks of random depth leaving every frame's state the bytes of a run
// without them, its hash pinned for every platform.
import std;
import gongfu;
import openmui.testing;

using namespace omui::testing;
using namespace gongfu;

namespace {

constexpr std::uint16_t LP = bit(Button::lp), MP = bit(Button::mp), HP = bit(Button::hp), LK = bit(Button::lk), HK = bit(Button::hk);

// A training fighter: a jab that cancels, a crouching low, an overhead, a
// heavy, a fireball, an invulnerable uppercut, a throw and a jumping kick.
FighterDef kata() {
    FighterDef f;
    f.name = "Kata";
    f.health = 10000;
    f.walk_forward = 4_fx;
    f.walk_back = 3_fx;
    f.prejump = 4;
    f.jump_up = 20_fx;
    f.jump_forward = 5_fx;
    f.gravity = 1_fx;
    f.landing = 3;
    f.push = {-30_fx, 0_fx, 60_fx, 160_fx};
    f.hurt_standing = {-35_fx, 0_fx, 70_fx, 180_fx};
    f.hurt_crouching = {-35_fx, 0_fx, 70_fx, 110_fx};
    f.hurt_air = {-30_fx, 40_fx, 60_fx, 130_fx};

    MoveDef jab{.name = "5LP", .command = {.buttons = LP}, .total = 12};
    jab.hits.push_back({.active = {4, 5}, .boxes = {{20_fx, 110_fx, 70_fx, 30_fx}}, .damage = 300, .chip = 0, .hitstun = 15, .blockstun = 11,
                        .hitstop = 6, .pushback = 2_fx, .impact = 1});
    jab.cancels.push_back({.frames = {4, 8}, .into = {"236P"}});
    f.moves.push_back(jab);

    MoveDef low{.name = "2LK", .command = {.direction = 2, .buttons = LK}, .stance = Stance::crouching, .total = 14, .priority = 1};
    low.hits.push_back({.active = {5, 6}, .boxes = {{10_fx, 0_fx, 90_fx, 30_fx}}, .damage = 200, .hitstun = 12, .blockstun = 10, .hitstop = 5,
                        .pushback = 2_fx, .level = Level::low, .impact = 2});
    f.moves.push_back(low);

    MoveDef overhead{.name = "6HP", .command = {.direction = 6, .buttons = HP}, .total = 26, .priority = 1};
    overhead.hits.push_back({.active = {16, 17}, .boxes = {{10_fx, 60_fx, 80_fx, 70_fx}}, .damage = 600, .hitstun = 16, .blockstun = 12,
                             .hitstop = 9, .pushback = 3_fx, .level = Level::overhead, .impact = 3});
    f.moves.push_back(overhead);

    MoveDef heavy{.name = "5HP", .command = {.buttons = HP}, .total = 24};
    heavy.hits.push_back({.active = {7, 9}, .boxes = {{20_fx, 100_fx, 90_fx, 40_fx}}, .damage = 800, .chip = 0, .hitstun = 20, .blockstun = 16,
                          .hitstop = 10, .pushback = 3_fx, .impact = 4});
    f.moves.push_back(heavy);

    MoveDef fireball{.name = "236P", .command = {.motion = quarter_forward, .buttons = punches}, .total = 40, .priority = 10};
    fireball.projectile = ProjectileDef{.spawn = 8, .offset = {50_fx, 100_fx}, .velocity = {8_fx, 0_fx}, .life = 150};
    fireball.projectile->hit = {.active = {1, 150}, .boxes = {{-20_fx, -20_fx, 40_fx, 40_fx}}, .damage = 600, .chip = 60, .hitstun = 18,
                                .blockstun = 14, .hitstop = 7, .pushback = 3_fx, .impact = 5};
    f.moves.push_back(fireball);

    MoveDef uppercut{.name = "623P", .command = {.motion = dragon, .buttons = punches}, .total = 48, .priority = 11};
    uppercut.invulnerable = {{1, 6}};
    uppercut.steps = {{4, {2_fx, 16_fx}}, {14, {0_fx, -16_fx}}};
    uppercut.hits.push_back({.active = {4, 8}, .boxes = {{0_fx, 80_fx, 70_fx, 120_fx}}, .damage = 1200, .hitstun = 30, .blockstun = 20,
                             .hitstop = 12, .pushback = 2_fx, .knockdown = true, .launch = {3_fx, 14_fx}, .impact = 6});
    f.moves.push_back(uppercut);

    MoveDef grab{.name = "throw", .command = {.buttons = static_cast<std::uint16_t>(LP | LK), .together = true}, .total = 30, .priority = 20};
    grab.throw_ = ThrowDef{.active = {3, 4}, .range = {0_fx, 0_fx, 80_fx, 150_fx}, .damage = 1200, .tech_window = 7};
    f.moves.push_back(grab);

    MoveDef jumpkick{.name = "j.HK", .command = {.buttons = HK}, .stance = Stance::air, .total = 20};
    jumpkick.hits.push_back({.active = {6, 12}, .boxes = {{10_fx, -20_fx, 70_fx, 50_fx}}, .damage = 700, .hitstun = 18, .blockstun = 14,
                             .hitstop = 8, .pushback = 2_fx, .level = Level::overhead, .impact = 7});
    f.moves.push_back(jumpkick);
    return f;
}

const Fighter& kata_fighter() {
    static const Fighter f = [] {
        auto c = compile(kata());
        check(c.has_value(), "the training fighter compiles");
        return c ? *c : Fighter{};
    }();
    return f;
}

SystemDef long_round() {
    SystemDef s;
    s.round_frames = 1 << 20;
    s.deflect_window = -1;  // system 0.2's rules: a guard starting with the attack is a plain block (rules_test has deflects)
    return s;
}

// The two fighters `gap` units apart (origin to origin), in the middle.
void place_at(Match& m, Fixed gap) {
    State s = m.state();
    const Fixed mid = m.system().stage_width / 2;
    s.f[0].pos.x = mid - gap / 2;
    s.f[1].pos.x = mid + (gap - gap / 2);
    m.restore(s);
}

// Ticks with these pads until `until` holds or `limit` frames pass; the frames played.
int run(Match& m, Pad p0, Pad p1, const std::function<bool()>& until, int limit = 600) {
    for (int i = 1; i <= limit; ++i) {
        m.tick(p0, p1);
        if (until()) return i;
    }
    return -1;
}

bool has(const Match& m, Event::Kind k) {
    return std::ranges::any_of(m.events(), [&](const Event& e) { return e.kind == k; });
}

// A tiny deterministic generator (the same sequence everywhere).
struct Rng {
    std::uint64_t s;
    std::uint32_t next() {
        s ^= s << 13, s ^= s >> 7, s ^= s << 17;
        return static_cast<std::uint32_t>(s >> 11);
    }
};

Pad random_pad(Rng& r) {
    const int dirs[] = {5, 5, 5, 4, 6, 2, 1, 3, 8, 9, 7, 6, 4};
    const std::uint32_t v = r.next();
    std::uint16_t buttons = 0;
    if (v % 4 == 0) buttons = static_cast<std::uint16_t>(1u << ((v >> 3) % 6));
    if (v % 29 == 0) buttons = static_cast<std::uint16_t>(LP | LK);
    return pad(dirs[(v >> 8) % std::size(dirs)], buttons);
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gongfu"};

    s.test("fixed point: Q16.16, products floored, quotients truncated, literals at compile time", [] {
        static_assert((2.5_fx).bits() == 163840);
        static_assert((1.5_fx * 2_fx) == 3_fx);
        check_eq((Fixed::of(7) / 2_fx).bits(), 229376);  // 3.5
        check_eq((-(1.5_fx)).floor(), -2);
        check_eq(Fixed::ratio(1, 3).bits(), 21845);
        check(((-1.5_fx) * Fixed::ratio(1, 3)).bits() == -32768, "a negative product floors (−32767.5 steps is −32768)");
        const Rect right = place({10_fx, 0_fx, 20_fx, 5_fx}, {100_fx, 0_fx}, 1), left = place({10_fx, 0_fx, 20_fx, 5_fx}, {100_fx, 0_fx}, -1);
        check(right.left == 110_fx && right.right == 130_fx && left.left == 70_fx && left.right == 90_fx, "a box is mirrored for the other side");
    });

    s.test("the input log: directions relative to the facing, presses as edges, SOCD", [] {
        InputLog log;
        log.record(pad(6, LP), 1, Socd::up_wins);
        check_eq(log.direction(0), 6);
        check(log.pressed(0) == LP && log.held(0) == LP);
        log.record(pad(6, LP), -1, Socd::up_wins);
        check(log.direction(0) == 4 && log.pressed(0) == 0, "facing left, right is back; a held button is not pressed again");
        // Left and right together: neither, or the last pressed.
        InputLog a, b;
        a.record(pad(4), 1, Socd::neutral);
        a.record({static_cast<std::uint16_t>(Pad::left | Pad::right), 0}, 1, Socd::neutral);
        check_eq(a.direction(0), 5);
        b.record(pad(4), 1, Socd::last_wins);
        b.record({static_cast<std::uint16_t>(Pad::left | Pad::right), 0}, 1, Socd::last_wins);
        check_eq(b.direction(0), 6);
        InputLog c;
        c.record({static_cast<std::uint16_t>(Pad::up | Pad::down), 0}, 1, Socd::up_wins);
        check_eq(c.direction(0), 8);
        check_eq(c.direction(5), 5);  // never recorded: neutral
    });

    s.test("commands: motions in their window, charges, buttons together, the buffer", [] {
        const Command fireball{.motion = quarter_forward, .buttons = punches};
        InputLog log;
        for (int d : {2, 3, 6}) log.record(pad(d), 1, Socd::up_wins);
        log.record(pad(6, LP), 1, Socd::up_wins);
        check(recognise(log, fireball).has_value(), "2, 3, 6, then a punch");
        InputLog slow;
        slow.record(pad(2), 1, Socd::up_wins);
        for (int i = 0; i < 14; ++i) slow.record(pad(3), 1, Socd::up_wins);
        slow.record(pad(6, LP), 1, Socd::up_wins);
        check(!recognise(slow, fireball), "too slow: outside its 12 frames");
        InputLog other_side;
        for (int d : {2, 1, 4}) other_side.record(pad(d), -1, Socd::up_wins);  // facing left, 1 and 4 are forward
        other_side.record(pad(4, MP), -1, Socd::up_wins);
        check(recognise(other_side, fireball).has_value(), "the motion is the fighter's, not the screen's");
        // The buffer: a press 3 frames ago still counts, 4 does not.
        InputLog late;
        late.record(pad(5, LP), 1, Socd::up_wins);
        for (int i = 0; i < 3; ++i) late.record(pad(5), 1, Socd::up_wins);
        check(recognise(late, {.buttons = LP}).has_value());
        late.record(pad(5), 1, Socd::up_wins);
        check(!recognise(late, {.buttons = LP}));
        // Together: within two frames of each other, both held.
        InputLog both;
        both.record(pad(5, LP), 1, Socd::up_wins);
        both.record(pad(5, static_cast<std::uint16_t>(LP | LK)), 1, Socd::up_wins);
        check(recognise(both, {.buttons = static_cast<std::uint16_t>(LP | LK), .together = true}).has_value());
        // A charge: back 40 frames, then forward and the button.
        const Command sonic{.charge = {.from = 4, .frames = 40}, .buttons = punches};
        InputLog charge;
        for (int i = 0; i < 39; ++i) charge.record(pad(4), 1, Socd::up_wins);
        charge.record(pad(6, HP), 1, Socd::up_wins);
        check(!recognise(charge, sonic), "39 frames is not a charge");
        InputLog full;
        for (int i = 0; i < 40; ++i) full.record(pad(1), 1, Socd::up_wins);
        full.record(pad(6, HP), 1, Socd::up_wins);
        check(recognise(full, sonic).has_value(), "down-back charges back");
        // A direction held at the press.
        InputLog crouch;
        crouch.record(pad(3, LK), 1, Socd::up_wins);
        check(recognise(crouch, {.direction = 2, .buttons = LK}).has_value() && !recognise(crouch, {.direction = 6, .buttons = LK}));
    });

    s.test("characters are checked as data: every problem, named with its move and field", [] {
        FighterDef bad = kata();
        bad.moves[0].hits[0].active = {4, 30};
        bad.moves[1].hits[0].boxes.clear();
        bad.moves[0].cancels[0].into = {"236X"};
        bad.moves.push_back(bad.moves[3]);
        auto c = compile(bad);
        check(!c.has_value());
        if (!c) {
            std::vector<std::string> got;
            for (const Problem& p : c.error()) got.push_back(p.where + ": " + p.text);
            check(std::ranges::contains(got, std::string("Kata/5LP.hits[0].active: frames 4–30 of a move of 12 (from 1, in order, within the move)")),
                  std::format("{}", got));
            check(std::ranges::contains(got, std::string("Kata/2LK.hits[0].boxes: a hit with no box hits nothing")));
            check(std::ranges::contains(got, std::string("Kata/5LP.cancels[0]: no move is called `236X`")));
            check(std::ranges::contains(got, std::string("Kata/5HP: two moves are called `5HP`")));
        }
        SystemDef sys;
        sys.scaling = {100, 0};
        auto problems = check(sys);
        check(problems.size() == 1 && problems[0].where == "system.scaling[1]", std::format("{} problems", problems.size()));
    });

    s.test("moving: walking, a jump's arc, pushed apart, kept on the stage, turning to face", [] {
        Match m(long_round(), kata_fighter(), kata_fighter());
        const Fixed x0 = m.state().f[0].pos.x;
        for (int i = 0; i < 10; ++i) m.tick(pad(6), pad(5));
        check_eq(m.state().f[0].pos.x - x0, 40_fx);
        // A neutral jump: 4 frames of prejump, then up and back down.
        int frames = run(m, pad(8), pad(5), [&] { return m.state().f[0].mode == Mode::air; });
        check_eq(frames, 4);
        Fixed top{};
        frames = run(m, pad(5), pad(5), [&] {
            top = max(top, m.state().f[0].pos.y);
            return m.state().f[0].mode == Mode::landing;
        });
        check(frames == 39 && top == 190_fx, std::format("in the air {} frames, top {}", frames, top.to_double()));  // 19 up, the top, 19 down
        // Walking into the other: push boxes never overlap.
        for (int i = 0; i < 200; ++i) m.tick(pad(6), pad(4));
        const auto& st = m.state();
        check(st.f[1].pos.x - st.f[0].pos.x >= 60_fx, std::format("{} apart", (st.f[1].pos.x - st.f[0].pos.x).to_double()));
        check(st.f[1].pos.x + 30_fx <= m.system().stage_width, "the stage's edge holds");
        // Jumping over the other turns both around once they land.
        Match over(long_round(), kata_fighter(), kata_fighter());
        place_at(over, 100_fx);
        run(over, pad(9), pad(5), [&] { return over.state().f[0].mode == Mode::air; });
        run(over, pad(5), pad(5), [&] { return over.state().f[0].mode == Mode::landing; });
        for (int i = 0; i < 5; ++i) over.tick(pad(5), pad(5));
        check(over.state().f[0].facing == -1 && over.state().f[1].facing == 1 && over.state().f[0].pos.x > over.state().f[1].pos.x,
              "crossed over, they face each other again");
    });

    s.test("frame data as declared: startup, active and recovery on whiff", [] {
        const Fighter& k = kata_fighter();
        for (const char* name : {"5LP", "5HP", "6HP"}) {
            Match m(long_round(), k, k);
            place_at(m, 1000_fx);  // out of reach
            const std::int32_t id = k.find(name);
            const MoveDef& md = k.def.moves[static_cast<std::size_t>(id)];
            const Pad press = md.command.direction == 6 ? pad(6, md.command.buttons) : pad(5, md.command.buttons);
            m.tick(press, pad(5));
            check(m.state().f[0].mode == Mode::move && m.state().f[0].move == id, name);
            // The press's frame is the move's first; each frame after it, the next.
            int first_active = -1, last_active = -1, frames = 0;
            for (int f = 1; !m.recovered(0); ++f) {
                for (const HitDef& h : md.hits)
                    if (h.active.contains(m.state().f[0].move_frame)) {
                        if (first_active < 0) first_active = f;
                        last_active = f;
                    }
                frames = f;
                m.tick(pad(5), pad(5));
            }
            check(first_active == md.hits[0].active.from && last_active == md.hits.back().active.to && frames + 1 == md.total,
                  std::format("{}: startup {}, last active {}, {} frames", name, first_active, last_active, frames + 1));
            m.tick(pad(5, LP), pad(5));
            check(m.state().f[0].mode == Mode::move && m.state().f[0].move == k.find("5LP") && m.state().f[0].move_frame == 1,
                  "a new move starts on the frame after the last");
        }
    });

    s.test("frame advantage on hit and on block is what the data says: stun minus what the attacker has left", [] {
        const Fighter& k = kata_fighter();
        for (const bool block : {false, true}) {
            Match m(long_round(), k, k);
            place_at(m, 90_fx);
            const MoveDef& jab = k.def.moves[static_cast<std::size_t>(k.find("5LP"))];
            // The defender faces left: holding back is screen-right, 6.
            const Pad defender = block ? pad(6) : pad(5);
            m.tick(pad(5, LP), defender);
            int hit_frame = -1;
            for (int f = 0; f < 30 && hit_frame < 0; ++f) {
                m.tick(pad(5), defender);
                if (has(m, block ? Event::Kind::block : Event::Kind::hit)) hit_frame = m.state().f[0].move_frame;
            }
            check(hit_frame == jab.hits[0].active.from, std::format("connected on frame {}", hit_frame));
            int attacker_free = -1, defender_free = -1;
            for (int f = 1; f < 80 && (attacker_free < 0 || defender_free < 0); ++f) {
                m.tick(pad(5), defender);
                if (attacker_free < 0 && m.recovered(0)) attacker_free = f;
                if (defender_free < 0 && m.recovered(1)) defender_free = f;
            }
            const int stun = block ? jab.hits[0].blockstun : jab.hits[0].hitstun;
            const int expected = stun - (jab.total - hit_frame);
            check(defender_free - attacker_free == expected,
                  std::format("{}: +{} measured, {} declared", block ? "block" : "hit", defender_free - attacker_free, expected));
        }
    });

    s.test("blocking: mid either way, low only crouching, overhead only standing; chip through a block", [] {
        const Fighter& k = kata_fighter();
        auto outcome = [&](const char* move, int defender_dir) {
            Match m(long_round(), k, k);
            place_at(m, 90_fx);
            const MoveDef& md = k.def.moves[static_cast<std::size_t>(k.find(move))];
            m.tick(pad(md.command.direction ? md.command.direction : 5, md.command.buttons), pad(defender_dir));
            for (int f = 0; f < 40; ++f) {
                m.tick(pad(5), pad(defender_dir));
                if (has(m, Event::Kind::block)) return std::string("block");
                if (has(m, Event::Kind::hit) || has(m, Event::Kind::counter)) return std::string("hit");
            }
            return std::string("none");
        };
        // The defender faces left: back is screen-right (6), down-back 3.
        check_eq(outcome("5HP", 6), std::string("block"));
        check_eq(outcome("5HP", 3), std::string("block"));
        check_eq(outcome("2LK", 6), std::string("hit"));
        check_eq(outcome("2LK", 3), std::string("block"));
        check_eq(outcome("6HP", 3), std::string("hit"));
        check_eq(outcome("6HP", 6), std::string("block"));
        check_eq(outcome("5HP", 5), std::string("hit"));
    });

    s.test("cancels and combos: a jab cancelled into a fireball on hit; each hit scaled", [] {
        const Fighter& k = kata_fighter();
        Match m(long_round(), k, k);
        place_at(m, 90_fx);
        // 5LP, then 2-3-6 + P inside the jab's cancel window.
        const std::vector<Pad> script{pad(5, LP), pad(2), pad(2), pad(3), pad(3), pad(6), pad(6, LP)};
        std::vector<Event> hits;
        std::vector<std::int32_t> combo;  // the defender's combo count after each hit
        for (std::size_t f = 0; f < 120; ++f) {
            m.tick(f < script.size() ? script[f] : pad(5), pad(5));
            for (const Event& e : m.events())
                if (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter) hits.push_back(e), combo.push_back(m.state().f[1].combo);
        }
        check(combo == std::vector<std::int32_t>{1, 2}, std::format("combo {}: the fireball hit before the jab's hitstun ended", combo));
        check(hits.size() == 2, std::format("{} hits", hits.size()));
        if (hits.size() == 2) {
            check(hits[0].move == k.find("5LP") && hits[1].move == k.find("236P"), "the jab, then the fireball it was cancelled into");
            check(hits[0].damage == 300 && hits[1].damage == 600, std::format("{} then {} (the second hit at 100%)", hits[0].damage, hits[1].damage));
        }
        check_eq(m.state().f[1].health, 10000 - 900);
        // A third hit in a combo is scaled to 80%.
        SystemDef steep = long_round();
        steep.scaling = {100, 50};
        Match n(steep, k, k);
        place_at(n, 90_fx);
        std::vector<std::int32_t> damage;
        for (std::size_t f = 0; f < 120; ++f) {
            n.tick(f < script.size() ? script[f] : pad(5), pad(5));
            for (const Event& e : n.events())
                if (e.kind == Event::Kind::hit) damage.push_back(e.damage);
        }
        check(damage == std::vector<std::int32_t>{300, 300}, std::format("{}", damage));
    });

    s.test("counter hits: a hit in the defender's startup does more and stuns longer", [] {
        const Fighter& k = kata_fighter();
        Match m(long_round(), k, k);
        place_at(m, 110_fx);
        m.tick(pad(5), pad(5, HP));  // the defender starts a heavy (startup 7)
        m.tick(pad(5, LP), pad(5));  // the jab is active on its 4th frame: the heavy is still starting
        bool counter = false;
        std::int32_t dealt = 0;
        for (int f = 0; f < 10 && !counter; ++f) {
            m.tick(pad(5), pad(5));
            for (const Event& e : m.events())
                if (e.kind == Event::Kind::counter) counter = true, dealt = e.damage;
        }
        check(counter && dealt == 360, std::format("counter {} for {}", counter, dealt));
    });

    s.test("throws: a throw takes hold and does its damage; pressed back in time, it is broken", [] {
        const Fighter& k = kata_fighter();
        const Pad grab = pad(5, static_cast<std::uint16_t>(LP | LK));
        Match m(long_round(), k, k);
        place_at(m, 70_fx);
        m.tick(grab, pad(5));
        bool thrown = false;
        for (int f = 0; f < 40; ++f) {
            m.tick(pad(5), pad(5));
            thrown = thrown || has(m, Event::Kind::throw_);
        }
        check(thrown && m.state().f[1].health == 10000 - 1200, std::format("thrown {}, health {}", thrown, m.state().f[1].health));
        Match t(long_round(), k, k);
        place_at(t, 70_fx);
        t.tick(grab, pad(5));
        bool broken = false;
        for (int f = 0; f < 40; ++f) {
            const bool held = t.state().f[1].mode == Mode::thrown;
            t.tick(pad(5), held && t.state().f[1].tech < 5 ? grab : pad(5));
            broken = broken || has(t, Event::Kind::tech);
        }
        check(broken && t.state().f[1].health == 10000, std::format("broken {}, health {}", broken, t.state().f[1].health));
    });

    s.test("projectiles: one travels and hits; two meet and both are gone", [] {
        const Fighter& k = kata_fighter();
        const std::vector<Pad> motion{pad(2), pad(3), pad(6, LP)};
        Match m(long_round(), k, k);
        place_at(m, 600_fx);
        bool hit = false;
        for (std::size_t f = 0; f < 120 && !hit; ++f) {
            m.tick(f < motion.size() ? motion[f] : pad(5), pad(5));
            hit = has(m, Event::Kind::hit);
        }
        check(hit && m.state().f[1].health == 9400, "a fireball across the stage");
        Match c(long_round(), k, k);
        place_at(c, 600_fx);
        const std::vector<Pad> mirrored{pad(2), pad(1), pad(4, LP)};  // the right fighter faces left
        bool clash = false;
        for (std::size_t f = 0; f < 120 && !clash; ++f) {
            c.tick(f < motion.size() ? motion[f] : pad(5), f < mirrored.size() ? mirrored[f] : pad(5));
            clash = has(c, Event::Kind::projectile_clash);
        }
        check(clash && c.state().f[0].health == 10000 && c.state().f[1].health == 10000, "they cancel out");
    });

    s.test("an invulnerable uppercut goes through a jab and launches", [] {
        const Fighter& k = kata_fighter();
        Match m(long_round(), k, k);
        place_at(m, 90_fx);
        const std::vector<Pad> dp{pad(4), pad(4), pad(1), pad(2), pad(1, LP)};  // facing left: 6-2-3 is 4-2-1
        m.tick(pad(5), dp[0]);
        m.tick(pad(5), dp[1]);
        m.tick(pad(5), dp[2]);
        m.tick(pad(5, LP), dp[3]);  // the jab starts
        m.tick(pad(5), dp[4]);      // the uppercut starts: invulnerable on frames 1–6
        bool launched = false;
        std::int32_t attacker = -1;
        for (int f = 0; f < 30 && !launched; ++f) {
            m.tick(pad(5), pad(5));
            for (const Event& e : m.events())
                if (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter) launched = true, attacker = e.attacker;
        }
        check(launched && attacker == 1, std::format("hit by fighter {}", attacker));
        for (int f = 0; f < 16; ++f) m.tick(pad(5), pad(5));  // the hitstop, then the launch
        check(m.state().f[0].pos.y > Fixed{} && m.state().f[0].mode == Mode::hitstun, "launched into the air");
    });

    s.test("rollback: 10,000 rollbacks of random depth leave every frame's state as without them; the run's hash pinned", [] {
        const Fighter& k = kata_fighter();
        constexpr int frames = 10000;
        std::vector<std::pair<Pad, Pad>> inputs;
        Rng rng{0x9e3779b97f4a7c15ULL};
        for (int i = 0; i < frames; ++i) {
            // One after the other: the order a function's arguments are evaluated in is unspecified.
            const Pad a = random_pad(rng);
            const Pad b = random_pad(rng);
            inputs.emplace_back(a, b);
        }
        SystemDef sys = long_round();
        FighterDef tough = kata();
        tough.health = 1 << 24;
        const Fighter t = *compile(tough);
        // Straight through.
        Match straight(sys, t, t);
        std::vector<std::uint64_t> hashes;
        for (const auto& [a, b] : inputs) {
            straight.tick(a, b);
            hashes.push_back(straight.checksum());
        }
        // The same inputs, rolled back every frame by 1–10 frames and replayed.
        Match rolled(sys, t, t);
        std::vector<State> saved;
        Rng depth{12345};
        int rollbacks = 0, mismatches = 0;
        for (int i = 0; i < frames; ++i) {
            saved.push_back(rolled.state());
            rolled.tick(inputs[static_cast<std::size_t>(i)].first, inputs[static_cast<std::size_t>(i)].second);
            const int back = 1 + static_cast<int>(depth.next() % 10);
            if (back <= i + 1) {
                rolled.restore(saved[static_cast<std::size_t>(i + 1 - back)]);
                for (int j = i + 1 - back; j <= i; ++j) rolled.tick(inputs[static_cast<std::size_t>(j)].first, inputs[static_cast<std::size_t>(j)].second);
                ++rollbacks;
            }
            if (rolled.checksum() != hashes[static_cast<std::size_t>(i)]) ++mismatches;
        }
        check(rollbacks >= 9990 && mismatches == 0, std::format("{} rollbacks, {} frames differ", rollbacks, mismatches));
        // The run touched the whole of it: moves, hits, blocks, throws, projectiles.
        std::println("    gongfu run hash {:016x}, frame {}, health {} / {}", hashes.back(), straight.state().frame, straight.state().f[0].health,
                     straight.state().f[1].health);
        check(straight.state().f[0].health < (1 << 24) && straight.state().f[1].health < (1 << 24), "both were hit");
        // Pinned: every platform and compiler plays these inputs into these bytes.
        check(hashes.back() == 0x358f33ed234aae0aULL, std::format("run hash {:016x}", hashes.back()));
    });

    return s.run(argc, argv);
}
