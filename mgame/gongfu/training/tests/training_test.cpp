// Training mode's readings against the data: the frame meter counts a move's
// startup, active and recovery frames as declared (hitstop not counted), the
// frame advantage after a hit and a block is the stun minus what the attacker
// has left, a combo's hits and damage add up, the input history keeps what
// was held and for how long, and the dummy plays a recording back exactly.
import std;
import gongfu.training;
import openmui.testing;

using namespace omui::testing;
using namespace gongfu;
namespace tr = gongfu::training;

namespace {

constexpr std::uint16_t LP = bit(Button::lp), HP = bit(Button::hp);

FighterDef body() {
    FighterDef f;
    f.name = "Drill";
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
    jab.hits.push_back({.active = {4, 5}, .boxes = {{20_fx, 110_fx, 70_fx, 30_fx}}, .damage = 300, .hitstun = 15, .blockstun = 11, .hitstop = 6,
                        .pushback = 2_fx});
    jab.cancels.push_back({.frames = {4, 8}, .into = {"236P"}});
    f.moves.push_back(jab);
    MoveDef heavy{.name = "5HP", .command = {.buttons = HP}, .total = 24};
    heavy.hits.push_back({.active = {7, 9}, .boxes = {{20_fx, 100_fx, 90_fx, 40_fx}}, .damage = 800, .hitstun = 20, .blockstun = 16, .hitstop = 10,
                          .pushback = 3_fx});
    f.moves.push_back(heavy);
    MoveDef fireball{.name = "236P", .command = {.motion = quarter_forward, .buttons = punches}, .total = 40, .priority = 10};
    fireball.projectile = ProjectileDef{.spawn = 8, .offset = {50_fx, 100_fx}, .velocity = {8_fx, 0_fx}, .life = 150};
    fireball.projectile->hit = {.active = {1, 150}, .boxes = {{-20_fx, -20_fx, 40_fx, 40_fx}}, .damage = 600, .chip = 60, .hitstun = 18,
                                .blockstun = 14, .hitstop = 7, .pushback = 3_fx};
    f.moves.push_back(fireball);
    return f;
}

const Fighter& drill() {
    static const Fighter f = *compile(body());
    return f;
}

SystemDef rules() {
    SystemDef s;
    s.round_frames = 1 << 20;
    s.deflect_window = -1;  // a guard starting with the attack is a plain block here
    return s;
}

Match close(Fixed gap = 90_fx) {
    Match m(rules(), drill(), drill());
    State s = m.state();
    const Fixed mid = m.system().stage_width / 2;
    s.f[0].pos.x = mid - gap / 2;
    s.f[1].pos.x = mid + (gap - gap / 2);
    m.restore(s);
    return m;
}

// The meter's runs: (phase, frames) in order.
std::vector<std::pair<tr::Phase, int>> runs(std::span<const tr::Phase> meter) {
    std::vector<std::pair<tr::Phase, int>> out;
    for (tr::Phase p : meter) {
        if (!out.empty() && out.back().first == p) ++out.back().second;
        else out.emplace_back(p, 1);
    }
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gongfu.training"};

    s.test("the frame meter: a jab's 3 startup, 2 active and 7 recovery frames, on whiff and on hit (hitstop not counted)", [] {
        for (const Fixed gap : {400_fx, 90_fx}) {
            Match m = close(gap);
            tr::Readout r;
            for (int f = 0; f < 30; ++f) {
                const Pad p = f == 0 ? pad(5, LP) : pad(5);
                m.tick(p, pad(5));
                r.record(m, p, pad(5));
            }
            const auto a = runs(r.meter(0));
            const std::vector<std::pair<tr::Phase, int>> want{
                {tr::Phase::startup, 3}, {tr::Phase::active, 2}, {tr::Phase::recovery, 7}, {tr::Phase::free, 30 - 12 - (gap == 90_fx ? 6 : 0)}};
            check(a == want, std::format("gap {}: {} runs, first {} frames of startup", gap.to_double(), a.size(), a.empty() ? 0 : a[0].second));
            check(r.startup(0) == std::optional<std::int32_t>(4), "startup 4");
            if (gap == 90_fx) {
                const auto d = runs(r.meter(1));
                check(d.size() >= 2 && d[0].first == tr::Phase::free && d[1].first == tr::Phase::hitstun && d[1].second == 15,
                      std::format("the defender's hitstun: {} frames", d.size() >= 2 ? d[1].second : 0));
            }
        }
    });

    s.test("frame advantage after a hit and a block: the stun minus what the attacker has left", [] {
        for (const char* move : {"5LP", "5HP"}) {
            const MoveDef& m_def = drill().def.moves[static_cast<std::size_t>(drill().find(move))];
            for (const bool block : {false, true}) {
                Match m = close();
                tr::Readout r;
                const Pad defender = block ? pad(6) : pad(5);  // facing left: 6 is back
                const Pad press = pad(5, m_def.command.buttons);
                for (int f = 0; f < 60; ++f) {
                    const Pad p = f == 0 ? press : pad(5);
                    m.tick(p, defender);
                    r.record(m, p, defender);
                }
                const HitDef& h = m_def.hits[0];
                const int want = (block ? h.blockstun : h.hitstun) - (m_def.total - h.active.from);
                check(r.advantage() == std::optional<std::int32_t>(want) && r.advantage_attacker() == 0,
                      std::format("{} on {}: {} measured, {} declared", move, block ? "block" : "hit", r.advantage().value_or(-99), want));
            }
        }
    });

    s.test("a combo's hits and damage: a jab cancelled into a fireball", [] {
        Match m = close();
        tr::Readout r;
        const std::vector<Pad> script{pad(5, LP), pad(2), pad(2), pad(3), pad(3), pad(6), pad(6, LP)};
        bool seen_going = false;
        for (std::size_t f = 0; f < 120; ++f) {
            const Pad p = f < script.size() ? script[f] : pad(5);
            m.tick(p, pad(5));
            r.record(m, p, pad(5));
            seen_going = seen_going || (r.combo().going && r.combo().hits == 2);
        }
        check(seen_going, "two hits while it went on");
        check(r.combo().attacker == 0 && r.combo().hits == 2 && r.combo().damage == 900 && !r.combo().going,
              std::format("{} hits, {} damage", r.combo().hits, r.combo().damage));
    });

    s.test("the input history: what was held, and for how long; a press its own line", [] {
        Match m = close(400_fx);
        tr::Readout r;
        std::vector<Pad> script(10, pad(6));
        script.push_back(pad(6, LP));
        for (int i = 0; i < 5; ++i) script.push_back(pad(5));
        for (const Pad p : script) {
            m.tick(p, pad(5));
            r.record(m, p, pad(5));
        }
        const auto in = r.inputs(0);
        check(in.size() == 3 && in[0].pad == pad(6) && in[0].frames == 10 && in[1].pad == pad(6, LP) && in[1].frames == 1 && in[2].frames == 5,
              std::format("{} lines", in.size()));
        check_eq(tr::direction(pad(9)), 9);
        check_eq(tr::direction(Pad{.directions = Pad::left | Pad::right}), 5);
        // The history keeps its last lines.
        for (int i = 0; i < 40; ++i) {
            const Pad p = pad(i % 2 ? 4 : 6);
            m.tick(p, pad(5));
            r.record(m, p, pad(5));
        }
        check_eq(r.inputs(0).size(), tr::Readout::history);
    });

    s.test("the dummy: a recording played back exactly, looped", [] {
        tr::Recorder rec;
        std::vector<Pad> given;
        for (int i = 0; i < 25; ++i) given.push_back(pad(1 + i % 9, i % 4 == 0 ? LP : 0));
        rec.start_recording();
        for (const Pad p : given) check(rec.step(p) == p);
        rec.stop();
        check_eq(rec.length(), given.size());
        rec.start_playback();
        bool same = true;
        for (std::size_t i = 0; i < 3 * given.size(); ++i) same = same && rec.step(pad(5)) == given[i % given.size()];
        check(same && rec.playing(), "three loops of the recording, whatever the player holds");
        rec.stop();
        check(rec.step(pad(3)) == pad(3), "stopped: the player's own pad");
        tr::Recorder empty;
        empty.start_playback();
        check(!empty.playing(), "nothing recorded: nothing to play");
    });

    s.test("the readout keeps the latest clash, deflect and guard break, and counts them", [] {
        // Two 5HPs at once, out of each other's bodies' reach: a perfect clash.
        Match m = close(180_fx);
        tr::Readout r;
        for (int f = 0; f < 30; ++f) {
            const Pad p = f == 0 ? pad(5, HP) : pad(5);
            m.tick(p, p);
            r.record(m, p, p);
        }
        check(r.exchange().kind == Event::Kind::clash && r.exchange().by == -1, "a perfect clash, nobody's");
        check(r.tally(0).clashes == 1 && r.tally(0).perfect == 1 && r.tally(1).perfect == 1);
        // A fresh back tap just before a 5HP: a deflect, by player 2.
        SystemDef with = rules();
        with.deflect_window = 3;
        Match d(with, drill(), drill());
        State st = d.state();
        st.f[0].pos.x = 900_fx;
        st.f[1].pos.x = 1020_fx;
        d.restore(st);
        tr::Readout rd;
        for (int f = 0; f < 30; ++f) {
            const Pad a = f == 0 ? pad(5, HP) : pad(5), b = f >= 5 ? pad(6) : pad(5);
            d.tick(a, b);
            rd.record(d, a, b);
        }
        check(rd.exchange().kind == Event::Kind::deflect && rd.exchange().by == 1 && rd.tally(1).deflects == 1, "deflected by player 2");
    });

    return s.run(argc, argv);
}
