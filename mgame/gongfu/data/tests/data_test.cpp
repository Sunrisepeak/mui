// Characters and rules as files: numpad notation both ways, a fighter file
// that plays as its C++ definition does, and every problem — the format's
// and the compiler's — at its line and column, a misspelt key with the
// nearest right one.
import std;
import gongfu.data;
import openmui.testing;

using namespace omui::testing;
using namespace gongfu;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path(); }

std::string read(const std::filesystem::path& f) {
    std::ifstream in(f, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::vector<std::string> texts(const std::vector<data::Diagnostic>& ds) {
    std::vector<std::string> out;
    for (const auto& d : ds) out.push_back(d.text());
    return out;
}

bool contains(const std::vector<std::string>& lines, std::string_view text) {
    return std::ranges::any_of(lines, [&](const std::string& l) { return l.find(text) != std::string::npos; });
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gongfu.data"};

    s.test("numpad notation: read and written back", [] {
        for (const char* n : {"LP", "2LK", "6HP", "236P", "623K", "63214HP", "[4]6P", "[2]8K", "[4:60]6HP", "LP+LK", "214MK", "K"}) {
            auto c = data::parse_command(n);
            check(c.has_value(), c ? std::string(n) : c.error());
            if (c) check_eq(data::notation(*c), std::string(n));
        }
        auto fireball = *data::parse_command("236P");
        check(fireball.motion.length == 3 && fireball.motion.window == 12 && fireball.buttons == punches && !fireball.together);
        auto grab = *data::parse_command("LP+LK");
        check(grab.together && grab.buttons == (bit(Button::lp) | bit(Button::lk)));
        auto sonic = *data::parse_command("[4]6P");
        check(sonic.charge.from == 4 && sonic.charge.frames == 40 && sonic.motion.length == 0);
        auto bad = [](const char* n) { return data::parse_command(n).error(); };
        check_eq(bad("236X"), std::string("`236X`: `X` is not a button (LP MP HP LK MK HK, P any punch, K any kick)"));
        check_eq(bad("[6]4P"), std::string("`[6]4P`: a charge is held back [4] or down [2]"));
        check_eq(bad("[4]8P"), std::string("`[4]8P`: a [4] charge is released 6"));
        check_eq(bad("5LP"), std::string("`5LP`: 5 is neutral: write the button alone"));
        check_eq(bad("236"), std::string("`236`: no button"));
    });

    s.test("a fighter file plays: the jab cancelled into the fireball on hit, as the C++ definition does", [] {
        auto kata = data::read_fighter(read(here() / "data" / "kata.fighter.gongfu.toml"), "kata.fighter.gongfu.toml");
        check(kata.has_value(), kata ? "" : std::format("{}", texts(kata.error())));
        if (!kata) return;
        check(kata->def.name == "Kata" && kata->def.moves.size() == 8 && kata->def.walk_forward == 4_fx && kata->def.gravity == 1_fx);
        const MoveDef& dp = kata->def.moves[static_cast<std::size_t>(kata->find("623P"))];
        check(dp.command.motion.length == 3 && dp.invulnerable.size() == 1 && dp.steps.size() == 2 && dp.hits[0].launch == Vec{3_fx, 14_fx});
        check(kata->def.moves[static_cast<std::size_t>(kata->find("throw"))].throw_.has_value());
        // Impacts are named (the look draws them), each name once, in the order first named.
        check(kata->def.impacts == std::vector<std::string>{"light", "low", "overhead", "heavy", "fireball", "rising", "air"},
              std::format("{}", kata->def.impacts));
        check_eq(kata->def.impacts[dp.hits[0].impact], std::string("rising"));
        auto system = data::read_system(read(here() / "data" / "arcade.system.gongfu.toml"), "arcade.system.gongfu.toml");
        check(system.has_value(), system ? "" : std::format("{}", texts(system.error())));
        if (!system) return;
        check(system->round_frames == 99 * 60 && system->counter_damage == 120 && system->socd == Socd::up_wins);
        Match m(*system, *kata, *kata);
        State st = m.state();
        st.f[0].pos.x = 915_fx;
        st.f[1].pos.x = 1005_fx;
        m.restore(st);
        const std::vector<Pad> script{pad(5, bit(Button::lp)), pad(2), pad(2), pad(3), pad(3), pad(6), pad(6, bit(Button::lp))};
        std::vector<std::int32_t> damage;
        for (std::size_t f = 0; f < 120; ++f) {
            m.tick(f < script.size() ? script[f] : pad(5), pad(5));
            for (const Event& e : m.events())
                if (e.kind == Event::Kind::hit) damage.push_back(e.damage);
        }
        check(damage == std::vector<std::int32_t>{300, 600}, std::format("{}", damage));
    });

    s.test("problems at their line and column: the format's, with the nearest key, and the compiler's", [] {
        const std::string text = R"(gongfu = "0.1"
name = "Broken"
health = 10000
walk = { forward = 4, back = 3 }
jump = { prejump = 4, up = 20, forward = 5, gravity = 1, landing = 3 }
push = [-30, 0, 60, 160]
hurt = { standing = [-35, 0, 70, 180], crouching = [-35, 0, 70, 110], air = [-30, 40, 60, 130] }

[[move]]
name = "5LP"
input = "LP"
total = 12
[[move.hit]]
active = [4, 5]
boxes = [[20, 110, 70, 30]]
damgae = 300
hitstun = 15
blockstun = 11
impact = 3
)";
        auto bad = data::read_fighter(text, "broken.fighter.gongfu.toml");
        check(!bad.has_value());
        if (!bad) {
            const auto t = texts(bad.error());
            check(contains(t, "broken.fighter.gongfu.toml:16:1: a hit has no key `damgae` (did you mean `damage`?)"), std::format("{}", t));
            check(contains(t, "broken.fighter.gongfu.toml:19:10: `impact` names a profile of the fighter's look"), std::format("{}", t));
        }
        // Well formed, but the compiler finds the frames outside the move: at the line of `active`.
        const std::string frames = R"(gongfu = "0.1"
name = "Short"
jump = { prejump = 4, up = 20, forward = 5, gravity = 1, landing = 3 }
push = [-30, 0, 60, 160]
hurt = { standing = [-35, 0, 70, 180], crouching = [-35, 0, 70, 110], air = [-30, 40, 60, 130] }

[[move]]
name = "jab"
input = "LP"
total = 6
[[move.hit]]
active = [4, 9]
boxes = [[20, 110, 70, 30]]
damage = 300
hitstun = 15
blockstun = 11

[[move]]
name = "2LP"
input = "2LP"
total = 8
[[move.cancel]]
frames = [3, 6]
into = ["jab", "236P"]
)";
        auto out = data::read_fighter(frames, "short.fighter.gongfu.toml");
        check(!out.has_value());
        if (!out) {
            const auto t = texts(out.error());
            check(contains(t, "short.fighter.gongfu.toml:12:10: Short/jab.hits[0].active: frames 4–9 of a move of 6"), std::format("{}", t));
            check(contains(t, "short.fighter.gongfu.toml:22:1: Short/2LP.cancels[0]: no move is called `236P`"), std::format("{}", t));
        }
        // A malformed file: TOML's own error, where it is.
        auto broken = data::read_fighter("gongfu = \"0.1\"\nname = \n", "x.fighter.gongfu.toml");
        check(!broken && broken.error().size() == 1 && broken.error()[0].line == 2, "the parse error's line");
        // A system file's problems too.
        auto sys = data::read_system("gongfu = \"0.1\"\nscaling = [100, 0]\nsocd = \"both\"\n", "s.system.gongfu.toml");
        check(!sys.has_value());
        if (!sys) {
            const auto t = texts(sys.error());
            check(contains(t, "s.system.gongfu.toml:3:8: `both` is not an SOCD rule (neutral, last_wins or up_wins)"), std::format("{}", t));
        }
        auto steps = data::read_system("gongfu = \"0.1\"\nscaling = [100, 0]\n", "s.system.gongfu.toml");
        check(!steps && texts(steps.error()) == std::vector<std::string>{"s.system.gongfu.toml:2:17: system.scaling[1]: a step is 1–100 percent"},
              steps ? "" : std::format("{}", texts(steps.error())));
        auto version = data::read_system("gongfu = \"0.2\"\n", "v.system.gongfu.toml");
        check(!version && contains(texts(version.error()), "this reads gongfu 0.1 files; the file says 0.2"));
    });

    return s.run(argc, argv);
}
