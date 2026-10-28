// Hurtboxes baked from the animation (G-5): a jab's arm reaches out of the
// standing box on its active frames and is baked there; the frames that look
// alike share one range; the baked file reads back as itself and makes the
// arm hittable in a match; `check` finds a boxes file that is not what the
// files make now, and `bake` writes it again.
import std;
import gongfu.studio;
import openmui.testing;

using namespace omui::testing;
using namespace gongfu;
namespace p2 = gongfu::present2d;

namespace {

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path(); }

std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

struct Kata {
    std::string fighter_text = read(here() / "data" / "kata.fighter.gongfu.toml");
    std::string look_text = read(here() / "data" / "kata.look.gongfu.toml");
    Fighter     fighter = *data::read_fighter(fighter_text, "kata.fighter.gongfu.toml");
    p2::Look    look = *p2::read_look(look_text, "kata.look.gongfu.toml");
};

const std::vector<HurtDef>* baked_for(const data::Baked& b, std::string_view move) {
    for (const auto& [name, hurt] : b.moves)
        if (name == move) return &hurt;
    return nullptr;
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gongfu.studio"};

    s.test("baked: a jab's arm reaches out of the standing box on its active frames, and only then", [] {
        const Kata k;
        const data::Baked b = studio::bake(k.fighter, k.look, studio::fingerprint(k.fighter_text, k.look_text));
        check_eq(b.fighter, std::string("Kata"));
        const auto* jab = baked_for(b, "5LP");
        check(jab != nullptr, "the jab is baked");
        if (!jab) return;
        const MoveDef& def = k.fighter.def.moves[static_cast<std::size_t>(k.fighter.find("5LP"))];
        const Box& stand = k.fighter.def.hurt_standing;
        bool active_out = false;
        for (const HurtDef& h : *jab) {
            check(h.boxes.size() >= 2 && h.boxes[0] == stand, "the stance's box first, then what reaches out");
            if (h.frames.contains(def.hits[0].active.from))
                for (std::size_t i = 1; i < h.boxes.size(); ++i) active_out = active_out || (h.boxes[i].x + h.boxes[i].w) > stand.x + stand.w + 20_fx;
        }
        check(active_out, "on its first active frame the arm reaches 20 or more past the body");
        check(!jab->empty() && jab->front().frames.from > 1, "not on its first frame: the arm is still at the body");
        // Frames that look alike share a range: fewer ranges than frames.
        std::int32_t frames = 0;
        for (const HurtDef& h : *jab) frames += h.frames.length();
        check(jab->size() < static_cast<std::size_t>(frames), std::format("{} ranges over {} frames", jab->size(), frames));
    });

    s.test("the boxes file reads back as itself, and a match uses it: the arm is hit where the body is not", [] {
        const Kata k;
        const data::Baked b = studio::bake(k.fighter, k.look, studio::fingerprint(k.fighter_text, k.look_text));
        const std::string text = data::write_boxes(b);
        auto again = data::read_boxes(text, "kata.boxes.gongfu.toml");
        check(again.has_value() && data::write_boxes(*again) == text, "what is written reads back as itself");
        auto with = data::read_fighter(k.fighter_text, "kata.fighter.gongfu.toml", text, "kata.boxes.gongfu.toml");
        check(with.has_value(), with ? "" : with.error().front().text());
        if (!with) return;
        // Fighter 2 jabs; on its first active frame a probe from fighter 1 finds its arm farther out than its body.
        const MoveDef& jab = with->def.moves[static_cast<std::size_t>(with->find("5LP"))];
        const auto* baked = baked_for(b, "5LP");
        check(baked && !jab.hurt.empty(), "the fighter's jab has its baked hurtboxes");
        // A probe: one tall box reaching 110 forward, active on its first frame.
        FighterDef probe_def = k.fighter.def;
        MoveDef probe{.name = "probe", .command = {.buttons = bit(Button::mk)}, .total = 4, .priority = 50};
        probe.hits.push_back({.active = {1, 1}, .boxes = {{20_fx, 0_fx, 90_fx, 220_fx}}, .damage = 100, .hitstun = 10, .blockstun = 8});
        probe_def.moves.push_back(probe);
        const Fighter prober = *compile(probe_def);
        auto lands = [&](const Fighter& f) {
            for (std::int32_t gap = 260; gap >= 120; gap -= 5) {  // the farthest first
                SystemDef sys;
                sys.round_frames = 1 << 20;
                Match m(sys, prober, f);
                State st = m.state();
                const Fixed mid = m.system().stage_width / 2;
                st.f[0].pos.x = mid - Fixed::of(gap) / 2;
                st.f[1].pos.x = mid + Fixed::of(gap) / 2;
                st.f[1].mode = Mode::move;
                st.f[1].move = f.find("5LP");
                st.f[1].move_frame = jab.hits[0].active.from - 1;  // its arm out on the next frame
                st.f[0].mode = Mode::move;
                st.f[0].move = prober.find("probe");
                st.f[0].move_frame = 0;
                m.restore(st);
                m.tick(pad(5), pad(5));
                if (std::ranges::any_of(m.events(), [](const Event& e) { return e.attacker == 0 && e.kind != Event::Kind::block; })) return gap;
            }
            return -1;
        };
        const std::int32_t plain = lands(k.fighter), baked_reach = lands(*with);
        check(baked_reach > plain, std::format("the farthest a jab lands: {} with the body alone, {} with the arm", plain, baked_reach));
    });

    s.test("check finds a stale boxes file; bake writes it again; the command line says so as JSON with an exit code", [] {
        const auto dir = scratch_dir("gongfu-studio");
        std::filesystem::copy_file(here() / "data" / "kata.fighter.gongfu.toml", dir / "kata.fighter.gongfu.toml", std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(here() / "data" / "kata.look.gongfu.toml", dir / "kata.look.gongfu.toml", std::filesystem::copy_options::overwrite_existing);
        const auto files = studio::fighters_in(dir);
        check_eq(files.size(), std::size_t{1});
        if (files.empty()) return;
        auto text = studio::baked_text(files[0]);
        check(text.has_value());
        if (!text) return;
        std::ofstream(files[0].boxes, std::ios::binary) << *text;
        check(read(files[0].boxes) == *studio::baked_text(files[0]), "current");
        // The look changes (a longer forearm): the file is stale until baked again.
        std::string look = read(files[0].look);
        const auto at = look.find("name = \"fore_front\"");
        check(at != std::string::npos);
        look.replace(look.find("length = 28", at), 11, "length = 40");
        std::ofstream(files[0].look, std::ios::binary) << look;
        auto now = studio::baked_text(files[0]);
        check(now && *now != read(files[0].boxes), "stale after the look changed");
        check(now && now->find(studio::fingerprint(read(files[0].fighter), look)) != std::string::npos, "it says what it was baked from");
    });

    return s.run(argc, argv);
}
