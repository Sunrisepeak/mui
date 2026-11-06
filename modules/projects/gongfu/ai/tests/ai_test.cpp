// The bot and its rounds: a fighter's kit read from its data; the pads that
// ask for a motion, a charge and a rotation, facing either way; a round that
// plays the same twice; a bot that beats one standing still, guards most of
// what it sees coming, and is even with itself over many rounds.
import std;
import gongfu;
import gongfu.ai;
import openmui.testing;

using namespace omui::testing;
using namespace gongfu;

namespace {

constexpr std::uint16_t LP = bit(Button::lp), MP = bit(Button::mp), HP = bit(Button::hp), LK = bit(Button::lk), HK = bit(Button::hk);

HitDef strike(Frames active, Box box, std::int32_t damage, std::int32_t stun, Level level = Level::mid) {
    return {.active = active, .boxes = {box}, .damage = damage, .hitstun = stun, .blockstun = stun - 4, .hitstop = 6, .pushback = 2_fx, .level = level};
}

// A plain fighter: three normals, a sweep, a fireball, an anti-air, a throw, a super.
const Fighter& plain() {
    static const Fighter f = [] {
        FighterDef d;
        d.name = "Plain";
        d.walk_forward = 4_fx;
        d.walk_back = 3_fx;
        d.gravity = 1_fx;
        d.jump_up = 19_fx;
        d.jump_forward = 5_fx;
        d.push = {-30_fx, 0_fx, 60_fx, 160_fx};
        d.hurt_standing = {-35_fx, 0_fx, 70_fx, 180_fx};
        d.hurt_crouching = {-35_fx, 0_fx, 70_fx, 110_fx};
        d.hurt_air = {-30_fx, 40_fx, 60_fx, 130_fx};
        MoveDef jab{.name = "5LP", .command = {.buttons = LP}, .total = 12, .meter_gain = 20};
        jab.hits.push_back(strike({4, 5}, {20_fx, 110_fx, 60_fx, 30_fx}, 300, 14));
        d.moves.push_back(jab);
        MoveDef mid{.name = "5MP", .command = {.buttons = MP}, .total = 20, .meter_gain = 40};
        mid.hits.push_back(strike({6, 8}, {20_fx, 100_fx, 90_fx, 30_fx}, 600, 18));
        d.moves.push_back(mid);
        MoveDef heavy{.name = "5HP", .command = {.buttons = HP}, .total = 30, .meter_gain = 60};
        heavy.hits.push_back(strike({10, 12}, {25_fx, 95_fx, 120_fx, 40_fx}, 900, 22));
        d.moves.push_back(heavy);
        MoveDef sweep{.name = "2HK", .command = {.direction = 2, .buttons = HK}, .stance = Stance::crouching, .total = 32, .meter_gain = 50};
        sweep.hits.push_back(strike({9, 11}, {15_fx, 0_fx, 120_fx, 30_fx}, 800, 20, Level::low));
        sweep.hits[0].knockdown = true;
        d.moves.push_back(sweep);
        MoveDef jump{.name = "j.HP", .command = {.buttons = HP}, .stance = Stance::air, .total = 20};
        jump.hits.push_back(strike({6, 10}, {10_fx, 20_fx, 80_fx, 60_fx}, 800, 20, Level::overhead));
        d.moves.push_back(jump);
        MoveDef ball{.name = "236P", .command = {.motion = quarter_forward, .buttons = punches}, .total = 40, .priority = 10, .meter_gain = 40};
        ball.projectile = ProjectileDef{.spawn = 12, .offset = {50_fx, 100_fx}, .velocity = {7_fx, 0_fx}, .life = 200, .limit = 1};
        ball.projectile->hit = strike({1, 200}, {-20_fx, -20_fx, 40_fx, 40_fx}, 600, 20);
        d.moves.push_back(ball);
        MoveDef rise{.name = "623P", .command = {.motion = dragon, .buttons = punches}, .total = 40, .priority = 11};
        rise.invulnerable.push_back({.frames = {1, 6}, .to = to_air});
        rise.hits.push_back(strike({4, 9}, {0_fx, 80_fx, 70_fx, 120_fx}, 1100, 30));
        rise.hits[0].knockdown = true;
        d.moves.push_back(rise);
        MoveDef grab{.name = "throw", .command = {.buttons = static_cast<std::uint16_t>(LP | LK), .together = true}, .total = 30, .priority = 20};
        grab.throw_ = ThrowDef{.active = {5, 6}, .range = {0_fx, 0_fx, 80_fx, 150_fx}, .damage = 1200};
        d.moves.push_back(grab);
        MoveDef super{.name = "236236P", .command = {.motion = motion({2, 3, 6, 2, 3, 6}, 24), .buttons = punches}, .total = 50, .priority = 20,
                      .meter_cost = 1000, .freeze = 30, .level = 1};
        super.hits.push_back(strike({8, 12}, {20_fx, 80_fx, 120_fx, 60_fx}, 2000, 30));
        super.hits[0].knockdown = true;
        d.moves.push_back(super);
        auto c = compile(d);
        check(c.has_value(), c ? "" : c.error().front().text);
        return c ? *c : Fighter{};
    }();
    return f;
}

SystemDef rules() { return SystemDef{}; }

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gongfu.ai"};

    s.test("a fighter's kit, read from its moves' data", [] {
        const ai::Kit k = ai::read_kit(plain());
        const Fighter& f = plain();
        check(k.pokes.size() == 4 && f.def.moves[static_cast<std::size_t>(k.pokes.front().move)].name == "5LP", "four pokes, the jab nearest");
        check(k.pokes.back().reach == 145 && k.pokes.back().move == f.find("5HP"), std::format("the farthest reaches {}", k.pokes.back().reach));
        check(std::ranges::any_of(k.pokes, [](const ai::Kit::Poke& p) { return p.low; }), "the sweep is low");
        check(k.projectiles == std::vector<std::int32_t>{f.find("236P")} && k.anti_air == f.find("623P") && k.throw_ == f.find("throw"));
        check(k.jump_ins == std::vector<std::int32_t>{f.find("j.HP")} && k.supers[1] == std::vector<std::int32_t>{f.find("236236P")});
        check_eq(k.throw_reach, std::int32_t{80});
    });

    s.test("the pads that ask for a move: a motion either way round, a charge held, a rotation's laps", [] {
        const Command ball = plain().def.moves[static_cast<std::size_t>(plain().find("236P"))].command;
        check(ai::pads_for(ball, 1) == std::vector<Pad>{pad(2), pad(3), pad(6, LP)}, "facing right");
        check(ai::pads_for(ball, -1) == std::vector<Pad>{pad(2), pad(1), pad(4, LP)}, "facing left: forward is 4");
        const Command sonic{.charge = {.from = 4, .frames = 30}, .buttons = punches};
        const auto held = ai::pads_for(sonic, 1);
        check(held.size() == 33 && held.front() == pad(4) && held.back() == pad(6, LP), "back 32 frames, then forward and a punch");
        const Command spin{.motion = rotation(2), .buttons = kicks};
        const auto laps = ai::pads_for(spin, 1);
        check(laps.size() == 16 && laps.back() == pad(9, LK), "twice round, the kick on the last");
        // What it asks for is read as that move.
        Match m(rules(), plain(), plain());
        for (const Pad p : ai::pads_for(ball, 1)) m.tick(p, pad(5));
        check(m.state().f[0].mode == Mode::move && m.state().f[0].move == plain().find("236P"), "read as the fireball");
    });

    s.test("a round plays the same twice; a bot beats one standing still", [] {
        const ai::Round a = ai::play(rules(), plain(), plain(), 42), b = ai::play(rules(), plain(), plain(), 42);
        check(a.winner == b.winner && a.frames == b.frames && a.damage == b.damage && a.supers == b.supers, "the same round");
        // Against a dummy that never moves.
        Match m(rules(), plain(), plain());
        ai::Bot bot(plain(), 0, 7);
        int frames = 0;
        while (!m.state().over && frames < 99 * 60) {
            m.tick(bot.next(m), pad(5));
            ++frames;
        }
        check(m.state().over && m.state().winner == 0 && frames < 60 * 60, std::format("won in {} frames", frames));
    });

    s.test("it guards most of the heavies it sees coming, and none it has no time to see", [] {
        // The other walks in and swings a heavy every 50 frames; with time to see it, the bot blocks most.
        auto blocked = [](std::int32_t reaction) {
            Match m(rules(), plain(), plain());
            State st = m.state();
            st.f[0].pos.x = 900_fx;
            st.f[1].pos.x = 1020_fx;
            m.restore(st);
            ai::Bot bot(plain(), 0, 11, {.reaction = reaction, .aggression = 0, .guard = 100, .anti_air = 0, .punish = 0, .offense = 0});
            int hits = 0, blocks = 0;
            for (int f = 0; f < 1500; ++f) {
                m.tick(bot.next(m), f % 50 == 0 ? pad(5, HP) : pad(4));
                for (const Event& e : m.events()) {
                    if (e.attacker == 1 && (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter)) ++hits;
                    if (e.attacker == 1 && (e.kind == Event::Kind::block || e.kind == Event::Kind::deflect)) ++blocks;
                }
                State keep = m.state();  // kept apart enough for the heavy to reach, never pushed out
                keep.f[1].pos.x = keep.f[0].pos.x + 120_fx;
                m.restore(keep);
            }
            return std::pair{hits, blocks};
        };
        const auto [hits, blocks] = blocked(6);
        check(blocks > 2 * hits, std::format("seen in time: {} blocked, {} hit", blocks, hits));
        // Seen 45 frames late, after the swing: it guards on what it saw last, and is hit more.
        const auto [late_hits, late_blocks] = blocked(45);
        check(late_hits > hits && late_blocks < blocks, std::format("seen late: {} blocked, {} hit", late_blocks, late_hits));
    });

    s.test("even with itself: a mirror over 40 rounds, sides swapped, is near half and half", [] {
        const ai::Record r = ai::versus(rules(), plain(), plain(), 40, 1);
        check_eq(r.rounds, std::int32_t{40});
        check(r.win_rate(0) >= 0.3 && r.win_rate(0) <= 0.7, std::format("{:.2f}", r.win_rate(0)));
        check(r.damage[0] > 0 && r.damage[1] > 0 && r.frames > 0);
        std::println("    mirror: {} / {} / {} draws, {:.0f} frames a round", r.wins[0], r.wins[1], r.draws, static_cast<double>(r.frames) / r.rounds);
    });

    return s.run(argc, argv);
}
