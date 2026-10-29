// A gongfu match drawn in 2D: poses by clip (a jab's arm reaches on its
// active frames), mirrored for the fighter facing the other way, the camera
// keeping both in view, sparks where hits land, and a frame drawn after a
// rollback the same bytes as before it. The pictures are pinned: the same
// display list, the same pixels, everywhere.
import std;
import gongfu.present2d;
import hanim;
import openmui.geom;
import openmui.raster;
import openmui.render.vector;
import openmui.image;
import openmui.testing;

using namespace omui::testing;
using namespace gongfu;
namespace p2 = gongfu::present2d;

namespace {

using omui::geom::Color;
constexpr std::uint16_t LP = bit(Button::lp);

FighterDef body() {
    FighterDef f;
    f.name = "Shade";
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
                        .pushback = 2_fx, .impact = 0});
    f.moves.push_back(jab);
    f.impacts = {"light"};
    return f;
}

const Fighter& fighter() {
    static const Fighter f = *compile(body());
    return f;
}

enum Bones : std::int32_t { hip, torso, head, upper_back, fore_back, upper_front, fore_front, thigh_back, shin_back, thigh_front, shin_front };

hanim::Track<double> keys(std::initializer_list<std::pair<int, double>> frame_values, double scale = 1) {
    hanim::Track<double> t;
    for (const auto& [frame, value] : frame_values) t.key(hanim::seconds(frame / 60.0), value * scale, hanim::ease::out_cubic);
    return t;
}

// A look's angles are degrees; these were written in radians.
constexpr double to_deg = 180 / 3.141592653589793;
hanim::Track<double> turn(std::initializer_list<std::pair<int, double>> frame_values) { return keys(frame_values, to_deg); }
float deg(double radians) { return static_cast<float>(radians * to_deg); }

// An ink-and-paper fighter: eleven bones, capsules and an ellipse head.
p2::Look look(Color body_colour, Color limb_colour) {
    p2::Look l;
    l.name = "shade";
    const double up = 1.5707963267948966, down = 3.141592653589793;
    l.bones = {
        {"hip", -1, {0, 92}, 10, deg(up)},
        {"torso", hip, {10, 0}, 56, 0},
        {"head", torso, {58, 0}, 24, 0},
        {"upper_back", torso, {50, 7}, 30, deg(-2.5)},
        {"fore_back", upper_back, {30, 0}, 28, deg(1.1)},
        {"upper_front", torso, {50, -7}, 30, deg(-2.3)},
        {"fore_front", upper_front, {30, 0}, 28, deg(1.6)},
        {"thigh_back", hip, {0, 7}, 46, deg(down - 0.3)},
        {"shin_back", thigh_back, {46, 0}, 48, deg(-0.25)},
        {"thigh_front", hip, {0, -7}, 46, deg(down + 0.35)},
        {"shin_front", thigh_front, {46, 0}, 48, deg(0.35)},
    };
    l.parts = {
        {upper_back, p2::Shape::capsule, 9, 7, {}, limb_colour, 0},  {fore_back, p2::Shape::capsule, 7, 6, {}, limb_colour, 0},
        {thigh_back, p2::Shape::capsule, 11, 9, {}, limb_colour, 0}, {shin_back, p2::Shape::capsule, 9, 7, {}, limb_colour, 0},
        {torso, p2::Shape::capsule, 18, 15, {}, body_colour, 1},     {hip, p2::Shape::capsule, 16, 16, {}, body_colour, 1},
        {head, p2::Shape::ellipse, 15, 13, {}, body_colour, 2},      {thigh_front, p2::Shape::capsule, 11, 9, {}, body_colour, 3},
        {shin_front, p2::Shape::capsule, 9, 7, {}, body_colour, 3},  {upper_front, p2::Shape::capsule, 9, 7, {}, body_colour, 4},
        {fore_front, p2::Shape::capsule, 7, 6, {}, body_colour, 4},
    };
    p2::Clip idle{.frames = 60, .loop = true};
    idle.rise = keys({{0, 0}, {30, -2}, {60, 0}});
    idle.angles.resize(l.bones.size());
    idle.angles[torso] = turn({{0, 0}, {30, 0.04}, {60, 0}});
    l.clips["idle"] = idle;
    p2::Clip jab{.frames = 12};
    jab.angles.resize(l.bones.size());
    jab.angles[upper_front] = turn({{0, 0}, {3, 0.75}, {5, 0.75}, {11, 0}});  // the shoulder swings forward
    jab.angles[fore_front] = turn({{0, 0}, {3, -1.55}, {5, -1.55}, {11, 0}});  // the elbow straightens
    jab.angles[torso] = turn({{0, 0}, {3, -0.12}, {11, 0}});
    l.clips["5LP"] = jab;
    p2::Clip hurt{.frames = 15};
    hurt.angles.resize(l.bones.size());
    hurt.angles[torso] = turn({{0, 0.35}, {15, 0}});
    hurt.angles[head] = turn({{0, 0.3}, {15, 0}});
    l.clips["hitstun"] = hurt;
    return l;
}

SystemDef rules() {
    SystemDef s;
    s.round_frames = 1 << 20;
    return s;
}

std::uint64_t picture(const omui::raster::Surface& s) {
    Hash h;
    for (int y = 0; y < s.height(); ++y) h.bytes(std::as_bytes(std::span(s.row(y), static_cast<std::size_t>(s.width()))));
    return h.value();
}

std::filesystem::path here() { return std::filesystem::path(std::source_location::current().file_name()).parent_path(); }

std::string read(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), {}};
}

std::vector<std::string> texts(const std::vector<gongfu::data::Diagnostic>& ds) {
    std::vector<std::string> out;
    for (const auto& d : ds) out.push_back(d.text());
    return out;
}

bool has(const std::vector<std::string>& texts, std::string_view piece) {
    return std::ranges::any_of(texts, [&](const std::string& t) { return t.find(piece) != std::string::npos; });
}

// Two fighters close enough for the jab to land on its first active frame.
Match close_match() {
    Match m(rules(), fighter(), fighter());
    State st = m.state();
    st.f[0].pos.x = 915_fx;
    st.f[1].pos.x = 1005_fx;
    m.restore(st);
    return m;
}

const std::filesystem::path& frames_dir() {
    static const std::filesystem::path d = scratch_dir("gongfu-present2d");
    return d;
}

void save(const omui::raster::Surface& s, std::string_view name) {
    const omui::imageio::View v{reinterpret_cast<const std::uint8_t*>(s.row(0)), s.width(), s.height(), s.width() * 4,
                                omui::imageio::Alpha::premultiplied};
    if (auto png = omui::imageio::encode_png(v)) {
        std::ofstream out(frames_dir() / std::format("{}.png", name), std::ios::binary);
        out.write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
    }
}

}  // namespace

int main(int argc, char** argv) {
    Suite s{"gongfu.present2d"};
    const p2::Look ink = look(Color::hex(0x1f2430), Color::hex(0x3a4152));
    const p2::Look paper = look(Color::hex(0xe8dcc4), Color::hex(0xb7aa90));

    s.test("the clip is what the fighter does: its move by name, else its mode", [&] {
        Match m(rules(), fighter(), fighter());
        check_eq(p2::clip_name(fighter(), m.state().f[0]), std::string_view("idle"));
        m.tick(pad(5, LP), pad(6));  // the right fighter faces left: 6 is back
        check_eq(p2::clip_name(fighter(), m.state().f[0]), std::string_view("5LP"));
        check_eq(p2::clip_name(fighter(), m.state().f[1]), std::string_view("walk_back"));
    });

    s.test("a jab's arm reaches on its active frames; the other side is a mirror", [&] {
        Match m(rules(), fighter(), fighter());
        const auto rest = p2::pose(ink, fighter(), m.state().f[0], 0);
        m.tick(pad(5, LP), pad(5));
        for (int f = 0; f < 3; ++f) m.tick(pad(5), pad(5));  // frame 4: active
        const auto out = p2::pose(ink, fighter(), m.state().f[0], m.state().frame);
        const float reach_rest = rest[fore_front].tip.x - static_cast<float>(m.state().f[0].pos.x.to_double()), reach = out[fore_front].tip.x - static_cast<float>(m.state().f[0].pos.x.to_double());
        check(reach > reach_rest + 20, std::format("the fist {} ahead, {} at rest", reach, reach_rest));
        // Two fighters standing apart: one pose the other's mirror around the middle.
        Match q(rules(), fighter(), fighter());
        const auto a = p2::pose(ink, fighter(), q.state().f[0], 0), b = p2::pose(ink, fighter(), q.state().f[1], 0);
        const float mid = static_cast<float>((q.state().f[0].pos.x + q.state().f[1].pos.x).to_double() / 2);
        bool mirrored = true;
        for (std::size_t i = 0; i < a.size(); ++i)
            mirrored = mirrored && std::abs((a[i].tip.x - mid) + (b[i].tip.x - mid)) < 1e-3f && std::abs(a[i].tip.y - b[i].tip.y) < 1e-3f;
        check(mirrored, "facing the other way mirrors every bone");
    });

    s.test("the frames: the stage, both fighters, a spark where the jab lands — pinned", [&] {
        Match m(rules(), fighter(), fighter());
        State st = m.state();
        st.f[0].pos.x = 915_fx;
        st.f[1].pos.x = 1005_fx;
        m.restore(st);
        p2::Effects fx;
        const p2::Scene scene;
        omui::render::CpuRenderer cpu;
        std::vector<std::uint64_t> lists, pixels;
        for (int f = 0; f < 8; ++f) {
            m.tick(f == 0 ? pad(5, LP) : pad(5), pad(5));
            fx.record(m);
            if (f == 0 || f == 3 || f == 5) {
                omui::render::DisplayList dl;
                p2::draw(m, ink, paper, fx, scene, dl);
                omui::raster::Surface px(static_cast<int>(scene.width), static_cast<int>(scene.height));
                cpu.render(dl, px, {}, {.scale = 1});
                lists.push_back(dl.hash());
                pixels.push_back(picture(px));
                save(px, std::format("frame-{}", m.state().frame));
            }
        }
        check(std::ranges::any_of(fx.kept(), [](const p2::Effects::Kept& k) { return k.event.kind == Event::Kind::hit; }), "the jab landed");
        check(pixels[0] != pixels[1] && pixels[1] != pixels[2], "each frame differs");
        std::println("    present2d lists {:016x} {:016x} {:016x} pixels {:016x} {:016x} {:016x}", lists[0], lists[1], lists[2], pixels[0], pixels[1], pixels[2]);
        check(lists == std::vector<std::uint64_t>{0x2bc57d3dbc61c26bULL, 0x80e98231d906210aULL, 0x60ca589059e2ffb2ULL}, "lists pinned");
        check(pixels == std::vector<std::uint64_t>{0x63cbb3e2c177a1bcULL, 0x94b2608af7e6ac66ULL, 0xa8ef2d37eb4b6200ULL}, "pixels pinned");
    });

    s.test("after a rollback the frame is drawn again the same: the picture is the state's", [&] {
        Match m(rules(), fighter(), fighter());
        State st = m.state();
        st.f[0].pos.x = 915_fx;
        st.f[1].pos.x = 1005_fx;
        m.restore(st);
        p2::Effects fx;
        const p2::Scene scene;
        std::vector<State> saved;
        std::vector<std::pair<Pad, Pad>> inputs;
        for (int f = 0; f < 40; ++f) inputs.emplace_back(f % 13 == 0 ? pad(5, LP) : pad(f % 7 < 3 ? 6 : 5), pad(f % 11 < 4 ? 4 : 5));
        for (const auto& [a, b] : inputs) {
            saved.push_back(m.state());
            m.tick(a, b);
            fx.record(m);
        }
        omui::render::DisplayList before;
        p2::draw(m, ink, paper, fx, scene, before);
        // Back to frame 25 and forward again with the same inputs.
        m.restore(saved[25]);
        fx.rewind(saved[25].frame + 1);
        for (std::size_t f = 25; f < inputs.size(); ++f) {
            m.tick(inputs[f].first, inputs[f].second);
            fx.record(m);
        }
        omui::render::DisplayList after;
        p2::draw(m, ink, paper, fx, scene, after);
        check(before.hash() == after.hash(), "the same display list");
    });

    s.test("a look file reads, writes back, and reads again the same: the same picture", [] {
        auto look = p2::read_look(read(here() / "data" / "kata.look.gongfu.toml"), "kata.look.gongfu.toml");
        check(look.has_value(), look ? "" : std::format("{}", texts(look.error())));
        if (!look) return;
        check_eq(look->bones.size(), std::size_t{11});
        check(look->slots == std::vector<std::string>{"body", "limb"} && look->palettes.size() == 2, "two palettes of two slots");
        check(look->clips.contains("j.HK") && look->clips.contains("5LP") && look->impacts.size() == 10);
        const std::string text = p2::write_look(*look);
        auto again = p2::read_look(text, "written.look.gongfu.toml");
        check(again.has_value(), again ? "" : std::format("{}\n{}", texts(again.error()), text));
        if (!again) return;
        check(p2::write_look(*again) == text, "what is written reads back as itself");
        Match m = close_match();
        p2::Effects fx;
        omui::render::DisplayList a, b;
        for (int f = 0; f < 6; ++f) {
            m.tick(f == 0 ? pad(5, LP) : pad(5), pad(5));
            fx.record(m);
        }
        p2::draw(m, *look, *look, fx, {}, a);
        p2::draw(m, *again, *again, fx, {}, b);
        check(a.hash() == b.hash(), "the same picture");
    });

    s.test("a look's glyphs and a level 3's cinematic read back as themselves; a cinematic naming no clip or glyph is a problem", [] {
        std::string text = "footfall = { every = 24, dip = 3, frames = 10 }\n" + read(here() / "data" / "kata.look.gongfu.toml");
        text += R"(
[glyph."一"]
strokes = [{ points = [[10, 50], [50, 52], [90, 50]], width = [8, 6, 10] }]

[cinematic.finale]
attacker = "5HP"
defender = "hitstun"
backdrop = { color = "#efe6d2", in = [0, 16], out = [186, 200], clouds = { color = "#ffffff", count = 6, between = [250, 900] } }
camera = [[0, 1, 85, 110], [10, 3, 18, 182]]
glyph = { name = "一", at = [175, 115], size = 280, from = 30, ends = [40], color = "#101114" }
seal = { frame = 172, at = [270, 30], size = 56, color = "#c0392b" }
flash = [[180, "white", 6]]
attacker_path = [[0, 0, 0], [90, 10, 420]]
defender_path = [[0, 80, 0], [20, 15, 195]]
quake = [[158, 30, 40]]
ring = [[158, 70, 0, 700, 30]]
)";
        auto look = p2::read_look(text, "kata.look.gongfu.toml");
        check(look.has_value(), look ? "" : std::format("{}", texts(look.error())));
        if (!look) return;
        const auto& c = look->cinematics.at("finale");
        check(c.camera.size() == 2 && c.glyph == "一" && c.glyph_ends == std::vector<std::int32_t>{40} && c.seal_frame == 172 && c.flashes.size() == 1);
        check(c.clouds.count == 6 && c.clouds.high == 900 && c.attacker_path.size() == 2 && c.defender_path.size() == 2 && c.quakes.size() == 1 && c.rings.size() == 1,
              "clouds, paths, a quake and a ring");
        check(look->footfall.dip == 3 && look->footfall.frames == 10, "a footfall");
        const std::string written = p2::write_look(*look);
        auto again = p2::read_look(written, "written.look.gongfu.toml");
        check(again && p2::write_look(*again) == written, "written back as itself");
        auto bad = p2::read_look(read(here() / "data" / "kata.look.gongfu.toml") + "\n[cinematic.finale]\nattacker = \"5XP\"\nglyph = { name = \"永\" }\n", "bad.look.gongfu.toml");
        check(!bad && has(texts(bad.error()), "plays `5XP`, which is no clip") && has(texts(bad.error()), "writes `永`, which is no glyph"),
              bad ? "read" : std::format("{}", texts(bad.error())));
    });

    s.test("a cinematic's bodies: turned, eased along a path, held reacting from each hold's start, standing in a blow's hitstop, launched (the heavy hit where a look has none), afterimages — files read back as themselves", [&] {
        // The keys read and write back.
        const std::string text = read(here() / "data" / "kata.look.gongfu.toml") + R"(
[cinematic.finale]
attacker = "5HP"
defender = "hitstun"
attacker_path = [[0, 0, 0], [20, 60, 0, "linear"], [40, 60, 200, "out_quad"]]
defender_path = [[0, 80, 0], [30, 80, 300, "out_quad"], [50, 120, 0, "in_quad"]]
attacker_turn = [[20, 40]]
defender_turn = [[45, 60]]
hitstop = [[10, 6], [30, 8]]
afterimages = [[12, 40]]
in_front = [[10, 20]]
reactions = ["hitstun.body", "launched", "slammed"]
)";
        auto read_back = p2::read_look(text, "kata.look.gongfu.toml");
        check(read_back.has_value(), read_back ? "" : std::format("{}", texts(read_back.error())));
        if (read_back) {
            const auto& c = read_back->cinematics.at("finale");
            check(c.attacker_path.size() == 3 && !c.attacker_path[0].ease && c.attacker_path[1].ease && c.defender_path[2].ease, "a path key's ease");
            check(c.attacker_turn.size() == 1 && c.defender_turn.size() == 1 && c.hitstops.size() == 2 && c.hitstops[1][1] == 8 && c.afterimages.size() == 1 && c.in_front.size() == 1);
            check(c.reactions.size() == 3 && c.reactions[1] == "launched", "taken reactions are reactions");
            const std::string written = p2::write_look(*read_back);
            auto again = p2::read_look(written, "written.look.gongfu.toml");
            check(again && p2::write_look(*again) == written, "written back as itself");
        }
        auto bad = p2::read_look(read(here() / "data" / "kata.look.gongfu.toml") + "\n[cinematic.finale]\nattacker = \"5HP\"\nreactions = [\"lanched\"]\n"
                                                                                       "defender_path = [[0, 0, 0, \"bouncy\"]]\nattacker_turn = [[40, 20]]\n",
                                 "bad.look.gongfu.toml");
        check(!bad && has(texts(bad.error()), "`lanched` is not a hit reaction") && has(texts(bad.error()), "`bouncy` is not an ease") &&
                  has(texts(bad.error()), "`attacker_turn` ends after it begins"),
              bad ? "read" : std::format("{}", texts(bad.error())));

        // Drawn: a fighter whose level 3 lands twice in its cinematic (frames 10 and 30).
        static const Fighter super = [] {
            FighterDef f = body();
            MoveDef m{.name = "236236K", .command = {.buttons = bit(Button::hk)}, .total = 30};
            m.level = 3;
            m.hits.push_back({.active = {4, 5}, .boxes = {{20_fx, 110_fx, 70_fx, 30_fx}}, .damage = 100, .hitstun = 20, .blockstun = 10, .hitstop = 6,
                              .pushback = 2_fx, .impact = 0});
            m.cinematic = CinematicDef{.frames = 60, .hits = {{10, 100, 0}, {30, 100, 0}}};
            f.moves.push_back(m);
            return *compile(f);
        }();
        const std::int32_t move = super.find("236236K");
        check(move >= 0);
        p2::Look a = ink, d = paper;
        a.clips["still"] = p2::Clip{.frames = 1, .loop = true};
        for (p2::Look* l : {&a, &d}) {
            p2::Clip heavy{.frames = 24};
            heavy.angles.resize(l->bones.size());
            heavy.angles[torso] = turn({{0, 0.6}, {24, 0}});
            l->clips["hitstun.heavy"] = heavy;
        }
        p2::Look launchable = d;
        p2::Clip up{.frames = 24};
        up.angles.resize(d.bones.size());
        up.angles[hip] = turn({{0, 1.2}, {24, 2.4}});
        launchable.clips["launched"] = up;
        // The lock at a frame, drawn by a cinematic (the attacker facing `facing`): its display list.
        auto drawn = [&](const p2::CinematicLook& c, std::int32_t frame, const p2::Look& defender, std::int32_t facing = 1, const p2::Look* attacker = nullptr) {
            p2::Look by = attacker ? *attacker : a;
            by.cinematics["236236K"] = c;
            Match m(rules(), super, super);
            State st = m.state();
            st.f[0].pos.x = 915_fx;
            st.f[1].pos.x = 1005_fx;
            st.f[0].facing = facing;
            st.f[1].facing = -1;
            for (auto& f : st.f) f.mode = Mode::cinematic;
            st.lock = {0, move, frame, 1, 915_fx, 0_fx};
            m.restore(st);
            omui::render::DisplayList dl;
            p2::draw(m, by, defender, p2::Effects{}, {}, dl);
            return dl.hash();
        };
        p2::CinematicLook plain{.attacker_clip = "still", .defender_clip = "hitstun"};
        // Turned: drawn as if it faced the other way.
        p2::CinematicLook turned = plain;
        turned.attacker_turn = {{0, 50}};
        check(drawn(turned, 20, d) == drawn(plain, 20, d, -1) && drawn(turned, 20, d) != drawn(plain, 20, d), "a turn faces the attacker the other way");
        check(drawn(turned, 50, d) == drawn(plain, 50, d), "and back after it");
        // A path key's ease: linear, a quarter of the way at a quarter of the time (eased in and out, less).
        p2::CinematicLook linear = plain, smooth = plain, there = plain;
        linear.defender_path = {{0, {0, 0}, {}}, {20, {100, 0}, hanim::ease::linear}};
        smooth.defender_path = {{0, {0, 0}, {}}, {20, {100, 0}, {}}};
        there.defender_path = {{0, {25, 0}, {}}, {20, {25, 0}, {}}};
        check(drawn(linear, 5, d) == drawn(there, 5, d) && drawn(smooth, 5, d) != drawn(there, 5, d), "linear along the path");
        // Held, the hold's clip plays from the hold's first frame.
        p2::CinematicLook early = plain, late = plain;
        early.holds = {{.from = 10, .to = 30, .bone = fore_front, .along = 28, .body = {0, 110}, .clip = "5LP"}};
        late.holds = {{.from = 20, .to = 40, .bone = fore_front, .along = 28, .body = {0, 110}, .clip = "5LP"}};
        p2::Look jabbing = d;
        jabbing.clips["5LP"] = ink.clips.at("5LP");
        check(drawn(early, 13, jabbing) == drawn(late, 23, jabbing) && drawn(early, 13, jabbing) != drawn(early, 16, jabbing), "a hold's clip from its start");
        // A hitstop: the struck shakes in it, and its reaction waits till it is over.
        p2::CinematicLook stopped = plain;
        stopped.hitstops = {{10, 6}};
        check(drawn(stopped, 19, d) == drawn(plain, 13, d), "the reaction three frames in, after the six stood");
        check(drawn(stopped, 12, d) != drawn(plain, 12, d), "in the hitstop the struck stands and shakes");
        // Launched: the look's own, or the heavy hit's where it has none.
        p2::CinematicLook launched = plain, heavy = plain;
        launched.reactions = {"launched"};
        heavy.reactions = {"hitstun.heavy"};
        check(drawn(launched, 14, d) == drawn(heavy, 14, d), "no `launched`: the heavy hit");
        check(drawn(launched, 14, launchable) != drawn(heavy, 14, launchable), "its own `launched`");
        // Launched again (a juggle): it goes on from the first, not again from its start.
        p2::CinematicLook juggled = plain, restarted = plain, kept = plain;
        juggled.reactions = {"launched", "launched"};
        restarted.reactions = {"hitstun.heavy", "launched"};
        kept.reactions = {"launched", "hitstun"};
        check(drawn(juggled, 29, launchable) == drawn(kept, 29, launchable) && drawn(juggled, 33, launchable) != drawn(restarted, 33, launchable),
              "launched twice: one turn going on");
        // In front: the attacker drawn over the defender — the same picture as with the sides swapped.
        p2::CinematicLook over = plain;
        over.in_front = {{0, 50}};
        check(drawn(over, 20, d) != drawn(plain, 20, d) && drawn(over, 50, d) == drawn(plain, 50, d), "the attacker over the defender over its frames only");
        // Afterimages: the attacker again as it was its trail's frames before.
        p2::Look trailing = a;
        trailing.trail.frames = {3, 6};
        p2::CinematicLook jab = plain, ghosts = plain;
        jab.attacker_clip = ghosts.attacker_clip = "5LP";
        ghosts.afterimages = {{0, 30}};
        check(drawn(ghosts, 8, d, 1, &trailing) != drawn(jab, 8, d, 1, &trailing) && drawn(ghosts, 40, d, 1, &trailing) == drawn(jab, 40, d, 1, &trailing),
              "afterimages over their frames only");
    });

    s.test("drawn as an artist would: curves, a light (shadow and rim), drawings swapped at a key, a sleeve that follows its swing", [] {
        const std::string text = R"(gongfu = "0.1"
name = "drawn"
light = { from = [-0.6, -0.8], shade = 6, darken = 0.5, shadow = "#000000", rim = 3, rim_color = "#ffffff" }

[[palette]]
name = "ink"
cloth = "#808080"

[[bone]]
name = "arm"
joint = [0, 100]
length = 60
rest = 0

[[bone]]
name = "sleeve"
parent = "arm"
joint = [20, 0]
length = 40
rest = -90
follow = { lag = 4, amount = 0.8, max = 50 }

[[part]]
name = "body"
bone = "arm"
shape = "curve"
points = [[0, -24], [60, -24], [60, 24], [0, 24]]
variants = [[[0, -40], [90, -30], [90, 30], [0, 40]]]
color = "cloth"

[[part]]
name = "cuff"
bone = "sleeve"
shape = "capsule"
size = [8, 6]
color = "#c0392b"
shade = false

[clip.swing]
frames = 20
ease = "linear"
angle.arm = [[0, 0], [8, 90], [20, 90]]
swap.body = [[0, 0], [8, 1], [12, 0]]
)";
        auto look = p2::read_look(text, "drawn.look.gongfu.toml");
        check(look.has_value(), look ? "" : std::format("{}", texts(look.error())));
        if (!look) return;
        check(look->light.shade == 6 && look->parts[0].shape == p2::Shape::curve && look->parts[0].variants.size() == 1 && !look->parts[1].shade, "read");
        check(look->bones[1].follow.lag == 4 && look->clips.at("swing").swaps.size() == 2, "a follow and a swap");
        const std::string written = p2::write_look(*look);
        auto again = p2::read_look(written, "written.look.gongfu.toml");
        check(again && p2::write_look(*again) == written, again ? written : std::format("{}", texts(again.error())));
        p2::use_palette(*look, 0);
        const Fighter f = *compile(body());
        FighterState st;
        st.facing = 1;
        p2::Painter painter;
        auto draw_at = [&](const p2::Look& l, std::int32_t t) {
            omui::render::DisplayList dl;
            dl.fill_rect({0, 0, 400, 300}, Color::hex(0xffffff));
            painter.body(l, f, st, 0, p2::Scene{.width = 400, .height = 300, .ground = 280}, p2::Camera{0, 0, 1}, dl, "swing", t);
            omui::raster::Surface px(400, 300);
            omui::render::CpuRenderer{}.render(dl, px, {}, {.scale = 1});
            return px;
        };
        // At rest (frame 0): the body's upper-left edge has the rim (lighter), its lower-right the shadow (darker), its middle its own grey.
        const auto rest = draw_at(*look, 0);
        save(rest, "drawn-rest");
        auto grey = [&](const omui::raster::Surface& px, int x, int y) { return static_cast<int>(px.at(x, y).r); };
        // The arm lies along +x from (0, 100) on the stage: on the picture, from the middle, 100 px above the ground line.
        const int cx = 230, cy = 180;  // the body's middle
        check(grey(rest, cx, cy) > 110 && grey(rest, cx, cy) < 150, std::format("the middle: {}", grey(rest, cx, cy)));
        // Up and down the column from the middle to the outline (dark): the body's pixels just inside it.
        int first = cy, last = cy;
        while (first > cy - 45 && grey(rest, cx + 30, first - 1) > 40) --first;
        while (last < cy + 45 && grey(rest, cx + 30, last + 1) > 40) ++last;
        first += 1, last -= 1;  // inside the outline's antialiased edge
        check(first > 0 && grey(rest, cx + 30, first) > grey(rest, cx, cy) + 20, std::format("the rim above: {} over {}", first > 0 ? grey(rest, cx + 30, first) : -1, grey(rest, cx, cy)));
        check(last > 0 && grey(rest, cx + 30, last) + 20 < grey(rest, cx, cy), std::format("the shadow below: {} under {}", last > 0 ? grey(rest, cx + 30, last) : -1, grey(rest, cx, cy)));
        // The swap: frames 8–11 draw the variant (bigger), 0–7 and 12– the points.
        Hash h0, h7, h9, h12;
        auto hash = [&](const omui::raster::Surface& px, Hash& h) {
            for (int y = 0; y < px.height(); ++y) h.bytes(std::as_bytes(std::span(px.row(y), static_cast<std::size_t>(px.width()))));
        };
        p2::Look still = *look;  // no follow: only the swap changes the picture between 8 and 12
        still.bones[1].follow = {};
        hash(draw_at(still, 9), h9);
        hash(draw_at(still, 12), h12);
        hash(draw_at(still, 13), h7);
        check(h9.hex() != h12.hex(), "swapped at 8");
        check(h12.hex() == h7.hex(), "back at 12: the arm still, the picture the same");
        // The sleeve follows: while the arm swings (frame 6) it lags, so the picture differs from the one without; at rest it does not.
        Hash with_swing, without_swing, with_rest, without_rest;
        hash(draw_at(*look, 6), with_swing);
        hash(draw_at(still, 6), without_swing);
        hash(draw_at(*look, 18), with_rest);
        hash(draw_at(still, 18), without_rest);
        check(with_swing.hex() != without_swing.hex(), "lagging while it swings");
        check(with_rest.hex() == without_rest.hex(), "hanging still once it stops");
        // Problems: a swap of no part, a variant it does not have, a follow without a lag.
        std::string bad = text;
        bad.replace(bad.find("swap.body"), 9, "swap.bdy");
        auto b1 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b1 && has(texts(b1.error()), "swaps `bdy`, which is no part's name"), b1 ? "read" : std::format("{}", texts(b1.error())));
        bad = text;
        bad.replace(bad.find("[8, 1], [12, 0]"), 6, "[8, 2]");
        auto b2 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b2 && has(texts(b2.error()), "chooses 2"), b2 ? "read" : std::format("{}", texts(b2.error())));
        bad = text;
        bad.replace(bad.find("lag = 4, "), 9, "");
        auto b3 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b3 && has(texts(b3.error()), "a follow has a `lag`"), b3 ? "read" : std::format("{}", texts(b3.error())));
    });

    s.test("drawn art on bones: a picture's region skinned to a bone and bent at the next one's joint; a file that reads back as itself", [&] {
        const std::string text = R"(gongfu = "0.1"
name = "art"

[[picture]]
name = "arm"
file = "arm.png"
size = [40, 200]

[[bone]]
name = "upper"
joint = [0, 160]
length = 80
rest = -90

[[bone]]
name = "fore"
parent = "upper"
joint = [80, 0]
length = 80

[[part]]
name = "arm"
bone = "upper"
shape = "image"
picture = "arm"
source = [0, 0, 40, 200]
anchor = [20, 20]
tip = [20, 100]
mesh = [1, 8]
bend = ["fore"]
blend = 20

[clip.bend]
frames = 10
ease = "linear"
angle.fore = [[0, 0], [10, 90]]
)";
        auto look = p2::read_look(text, "art.look.gongfu.toml");
        check(look.has_value(), look ? "" : std::format("{}", texts(look.error())));
        if (!look) return;
        check(look->pictures.size() == 1 && look->parts[0].shape == p2::Shape::image && look->parts[0].bend.size() == 1, "read");
        const std::string written = p2::write_look(*look);
        auto again = p2::read_look(written, "written.look.gongfu.toml");
        check(again && p2::write_look(*again) == written, again ? written : std::format("{}", texts(again.error())));
        // The picture: a red upper half, a blue lower half (where the forearm's art is).
        omui::render::ImageStore images;
        std::vector<omui::raster::Rgba8> px(40 * 200);
        for (int y = 0; y < 200; ++y)
            for (int x = 4; x < 36; ++x) px[static_cast<std::size_t>(y * 40 + x)] = y < 100 ? omui::raster::Rgba8{220, 40, 40, 255} : omui::raster::Rgba8{40, 40, 220, 255};
        look->pictures[0].ref = images.add(40, 200, std::move(px));
        const Fighter f = *compile(body());
        FighterState st;
        st.facing = 1;
        p2::Painter painter;
        auto draw_at = [&](std::int32_t t) {
            omui::render::DisplayList dl;
            dl.fill_rect({0, 0, 400, 300}, Color::hex(0xffffff));
            painter.body(*look, f, st, 0, p2::Scene{.width = 400, .height = 300, .ground = 200}, p2::Camera{0, 60, 1}, dl, "bend", t);
            omui::raster::Surface out(400, 300);
            omui::render::CpuRenderer{}.render(dl, out, {&images, nullptr}, {.scale = 1});
            return out;
        };
        const auto straight = draw_at(0), bent = draw_at(10);
        save(straight, "art-straight");
        save(bent, "art-bent");
        // Straight down, the blue lower half lies below the joint; bent 90° at the elbow, it goes out sideways.
        auto blue = [](const omui::raster::Surface& s, int x, int y) { return s.at(x, y).b > 150 && s.at(x, y).r < 100; };
        int below = 0, beside = 0;
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 400; ++x) {
                below += blue(straight, x, y);
                beside += blue(bent, x, y);
            }
        check(below > 1500 && beside > 1500, std::format("the lower half drawn: {} straight, {} bent", below, beside));
        int moved = 0;
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 400; ++x) moved += blue(straight, x, y) != blue(bent, x, y);
        check(moved > 1500, std::format("bent, the forearm's art moved: {} pixels", moved));
        // Problems: a picture not declared, a source outside it.
        std::string bad = text;
        bad.replace(bad.find("picture = \"arm\""), 15, "picture = \"leg\"");
        auto b1 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b1 && has(texts(b1.error()), "no picture is called `leg`"), b1 ? "read" : std::format("{}", texts(b1.error())));
        bad = text;
        bad.replace(bad.find("source = [0, 0, 40, 200]"), 24, "source = [0, 0, 40, 300]");
        auto b2 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b2 && has(texts(b2.error()), "is not inside picture"), b2 ? "read" : std::format("{}", texts(b2.error())));
    });

    s.test("lit drawn art: a picture's relief and a material light a part from the stage's side; an afterimage is the part's shape in its tint; files read back as themselves", [&] {
        const std::string text = R"(gongfu = "0.1"
name = "lit"

[[picture]]
name = "torso"
file = "torso.png"
size = [80, 160]
relief = { bevel = 24, detail = 0, smooth = 2, strength = 1 }

[material.body]
bands = 2
softness = 0.1
shadow = "#1a1430"
shadow_strength = 0.7
rim = "#ffffff"
rim_strength = 0
rim_width = 0.3
light_z = 0.4

[[bone]]
name = "torso"
joint = [0, 40]
length = 120
rest = 90

[[part]]
name = "torso"
bone = "torso"
shape = "image"
picture = "torso"
source = [0, 0, 80, 160]
anchor = [40, 150]
tip = [40, 10]
mesh = [1, 1]
)";
        auto look = p2::read_look(text, "lit.look.gongfu.toml");
        check(look.has_value(), look ? "" : std::format("{}", texts(look.error())));
        if (!look) return;
        check(look->pictures[0].relief && look->pictures[0].relief->bevel == 24 && look->materials.contains("body") && look->materials.at("body").bands == 2, "read");
        const std::string written = p2::write_look(*look);
        auto again = p2::read_look(written, "written.look.gongfu.toml");
        check(again && p2::write_look(*again) == written, again ? written : std::format("{}", texts(again.error())));
        // A grey slab; its relief (a bevel) made as a stage's carrier makes it, once.
        omui::imageio::Image slab{.width = 80, .height = 160, .alpha = omui::imageio::Alpha::straight, .rgba = std::vector<std::uint8_t>(80 * 160 * 4, 0)};
        for (int y = 6; y < 154; ++y)
            for (int x = 6; x < 74; ++x)
                for (int c = 0; c < 4; ++c) slab.rgba[static_cast<std::size_t>((y * 80 + x) * 4 + c)] = c == 3 ? 255 : 180;
        const omui::imageio::Image normal = omui::imageio::normal_map(slab, {.bevel = 24});
        auto pixels = [](const omui::imageio::Image& img) {
            std::vector<omui::raster::Rgba8> px(static_cast<std::size_t>(img.width * img.height));
            std::memcpy(px.data(), img.rgba.data(), img.rgba.size());
            return px;
        };
        omui::render::ImageStore images;
        look->pictures[0].ref = images.add(80, 160, pixels(slab));
        const Fighter f = *compile(body());
        FighterState st;
        st.facing = 1;
        p2::Painter painter;
        auto draw = [&](const p2::Stage* world, std::optional<Color> tint = std::nullopt) {
            omui::render::DisplayList dl;
            dl.fill_rect({0, 0, 300, 300}, Color::hex(0x000000));
            painter.body(*look, f, st, 0, p2::Scene{.width = 300, .height = 300, .ground = 260, .stage = world}, p2::Camera{0, 100, 1}, dl, "", 0, tint);
            omui::raster::Surface out(300, 300);
            omui::render::CpuRenderer{}.render(dl, out, {&images, nullptr}, {.scale = 1});
            return out;
        };
        // Brightness of the slab's left and right thirds.
        auto sides = [](const omui::raster::Surface& s) {
            std::array<long, 2> sum{};
            for (int y = 0; y < 300; ++y)
                for (int x = 0; x < 300; ++x)
                    if (s.at(x, y).a > 0 && s.at(x, y).g > 20) sum[x < 150 ? 0 : 1] += s.at(x, y).g;
            return sum;
        };
        const auto flat = draw(nullptr);
        look->pictures[0].normal = images.add(80, 160, pixels(normal));
        p2::Stage from_left, from_right;
        from_left.light_from = p2::Point{-1, 0};
        from_right.light_from = p2::Point{1, 0};
        const auto left = draw(&from_left), right = draw(&from_right);
        save(flat, "lit-flat");
        save(left, "lit-left");
        save(right, "lit-right");
        const auto [fl, fr] = sides(flat);
        const auto [ll, lr] = sides(left);
        const auto [rl, rr] = sides(right);
        check(std::abs(fl - fr) * 100 < fl, std::format("unlit, both sides alike: {} {}", fl, fr));
        check(ll > lr * 11 / 10 && rr > rl * 11 / 10, std::format("lit from the left {} > {}; from the right {} > {}", ll, lr, rr, rl));
        // An afterimage: the part's shape, all in its tint.
        const auto ghost = draw(&from_left, Color::hex(0xff3ea5));
        int pink = 0, other = 0;
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 300; ++x) {
                const auto p = ghost.at(x, y);
                if (p.r + p.g + p.b == 0) continue;
                (p.r > 200 && p.g < 90 && p.b > 130 ? pink : other) += 1;
            }
        check(pink > 5000 && other < pink / 10, std::format("the afterimage in its tint: {} pink, {} other", pink, other));
        // Problems: a material not declared; a material on a shape that is not drawn art.
        std::string bad = text;
        bad += "material = \"skin\"\n";
        auto b1 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b1 && has(texts(b1.error()), "no material is called `skin`"), b1 ? "read" : std::format("{}", texts(b1.error())));
        bad = text;
        bad += "\n[[part]]\nbone = \"torso\"\nshape = \"capsule\"\nmaterial = \"body\"\n";
        auto b2 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b2 && has(texts(b2.error()), "lights an image part"), b2 ? "read" : std::format("{}", texts(b2.error())));
    });

    s.test("a grip (two-bone IK): the back hand stays on the staff wherever the front arm swings it; let go, the arm hangs as the clip has it; a file that reads back as itself", [] {
        const std::string text = R"(gongfu = "0.1"
name = "grip"

[[bone]]
name = "body"
joint = [0, 100]
length = 60
rest = 90

[[bone]]
name = "front"
parent = "body"
joint = [50, -6]
length = 40
rest = -120

[[bone]]
name = "staff"
parent = "front"
joint = [40, 0]
length = 120
rest = 60

[[bone]]
name = "upper"
parent = "body"
joint = [50, 6]
length = 40
rest = 180

[[bone]]
name = "lower"
parent = "upper"
joint = [40, 0]
length = 40

[[bone]]
name = "thigh"
parent = "body"
joint = [0, 0]
length = 55
rest = 180

[[bone]]
name = "shin"
parent = "thigh"
joint = [55, 0]
length = 55

[[grip]]
name = "hands"
upper = "upper"
lower = "lower"
target = "staff"
at = 20
bend = -1

[[grip]]
name = "foot"
upper = "thigh"
lower = "shin"
target = "ground"
at = 10

[clip.aimed]
frames = 10
ease = "linear"
aim.front = [[0, -30], [10, 10]]
grip_at.foot = [[0, 10], [10, 30]]

[clip.swing]
frames = 20
ease = "linear"
angle.front = [[0, 0], [20, 80]]
grip.hands = [[0, 1], [10, 1], [20, 0]]
)";
        auto look = p2::read_look(text, "grip.look.gongfu.toml");
        check(look.has_value(), look ? "" : std::format("{}", texts(look.error())));
        if (!look) return;
        const std::string written = p2::write_look(*look);
        auto again = p2::read_look(written, "written.look.gongfu.toml");
        check(again && p2::write_look(*again) == written, again ? written : std::format("{}", texts(again.error())));
        for (const std::int32_t facing : {1, -1}) {
            FighterState st;
            st.facing = facing;
            p2::Pose pose;
            for (const std::int32_t t : {0, 5, 10}) {
                p2::pose_clip(*look, "swing", t, st, pose);
                const p2::BonePlace& staff = pose[2];
                const double hx = staff.joint.x + 20 * std::cos(staff.angle), hy = staff.joint.y + 20 * std::sin(staff.angle);
                const double miss = std::hypot(pose[4].tip.x - hx, pose[4].tip.y - hy);
                check(miss < 0.01, std::format("facing {}, frame {}: the hand is {} from its place on the staff", facing, t, miss));
                check(std::hypot(pose[4].joint.x - pose[3].tip.x, pose[4].joint.y - pose[3].tip.y) < 0.01, "the forearm hangs from the elbow");
            }
            // The foot on the ground where the fighter stands, `at` ahead (stepping in clip `aimed`).
            for (const auto& [clip, t, ahead] : {std::tuple{"swing", 0, 10.0}, std::tuple{"aimed", 5, 20.0}, std::tuple{"aimed", 10, 30.0}}) {
                p2::pose_clip(*look, clip, t, st, pose);
                check(std::hypot(pose[6].tip.x - ahead * facing, pose[6].tip.y) < 0.01, std::format("facing {}: the foot at ({}, {})", facing, pose[6].tip.x, pose[6].tip.y));
            }
            // Aimed: the arm's angle to the world, whatever the body does (mirrored facing left).
            p2::pose_clip(*look, "aimed", 0, st, pose);
            const double want = facing > 0 ? -std::numbers::pi / 6 : std::numbers::pi + std::numbers::pi / 6;
            check(std::abs(std::remainder(pose[1].angle - want, 2 * std::numbers::pi)) < 1e-9, std::format("aimed: {}", pose[1].angle));
            p2::pose_clip(*look, "swing", 20, st, pose);  // let go: the arm as the clip has it (straight down the back)
            check(std::abs(std::remainder(pose[3].angle + std::numbers::pi / 2, 2 * std::numbers::pi)) < 1e-6,
                  std::format("let go, the upper arm hangs straight down: {}", pose[3].angle));
        }
        std::string bad = text;
        bad.replace(bad.find("lower = \"lower\""), 15, "lower = \"staff\"");
        auto b1 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b1 && has(texts(b1.error()), "is not a child of `upper`"), b1 ? "read" : std::format("{}", texts(b1.error())));
        bad = text;
        bad.replace(bad.find("grip.hands"), 10, "grip.hand");
        auto b2 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b2 && has(texts(b2.error()), "holds `hand`, which is no grip"), b2 ? "read" : std::format("{}", texts(b2.error())));
    });

    s.test("key poses by name: a clip keyed by poses makes the tracks they say, a key may change its pose; files read back as themselves", [] {
        const std::string text = R"(gongfu = "0.1"
name = "poses"

[[bone]]
name = "body"
joint = [0, 80]
length = 60
rest = 90

[[bone]]
name = "arm"
parent = "body"
joint = [50, 0]
length = 40
rest = -90

[[part]]
name = "torso"
bone = "body"
shape = "capsule"
size = [10, 10]

[pose.stance]
aim = { body = 90, arm = -60 }
rise = 0

[pose.jab]
from = "stance"
aim = { arm = 0 }

[clip.punch]
frames = 12
ease = "linear"
poses = [[0, "stance"], [4, "jab", "out_expo"], [6, "jab", { aim = { body = 80 }, rise = -4 }], [12, "stance"]]
)";
        auto look = p2::read_look(text, "poses.look.gongfu.toml");
        check(look.has_value(), look ? "" : std::format("{}", texts(look.error())));
        if (!look) return;
        const std::string written = p2::write_look(*look);
        auto again = p2::read_look(written, "written.look.gongfu.toml");
        check(again && p2::write_look(*again) == written, again ? written : std::format("{}", texts(again.error())));
        FighterState st;
        st.facing = 1;
        p2::Pose pose;
        p2::pose_clip(*look, "punch", 4, st, pose);
        check(std::abs(pose[1].angle) < 1e-9, std::format("the jab's arm straight out: {}", pose[1].angle));
        p2::pose_clip(*look, "punch", 6, st, pose);
        check(std::abs(pose[0].angle - 80 * std::numbers::pi / 180) < 1e-9 && std::abs(pose[0].joint.y - 76) < 1e-4, "the key's change: the body leaning, lower");
        p2::pose_clip(*look, "punch", 0, st, pose);
        check(std::abs(pose[1].angle + 60 * std::numbers::pi / 180) < 1e-9, "the stance's arm");
        auto bad = [&](std::string_view from, std::string_view to, std::string_view want) {
            std::string b = text;
            b.replace(b.find(from), from.size(), to);
            auto r = p2::read_look(b, "bad.look.gongfu.toml");
            check(!r && has(texts(r.error()), want), r ? std::string("read") : std::format("{}", texts(r.error())));
        };
        bad("[4, \"jab\"", "[4, \"jbb\"", "no pose is called `jbb`");
        bad("from = \"stance\"", "from = \"jab\"", "is built on itself");
        bad("aim = { body = 90, arm = -60 }", "aim = { arm = -60 }", "does not aim `body`");
    });

    s.test("effects from a look's pictures: a sprite grows and fades, a stroke is drawn out along its curve, droplets fly and fall; a cut-in band slides in — files read back as themselves", [] {
        const std::string text = R"(gongfu = "0.1"
name = "ink"

[[picture]]
name = "fx"
file = "fx.png"
size = [64, 32]

[[bone]]
name = "body"
joint = [0, 80]
length = 60
rest = 90

[impact.hit]
fx = "splash"

[cutin.L3]
picture = "fx"
source = [0, 0, 64, 32]
from = 0
to = 30
y = 0.5
height = 0.4

[[fx.splash.sprite]]
picture = "fx"
source = [0, 0, 32, 32]
frames = 10
size = [10, 60]
alpha = [1, 0]

[[fx.splash.spray]]
picture = "fx"
sources = [[0, 0, 32, 32]]
count = 6
frames = 20
speed = [2, 4]
angle = [0, 180]
size = [4, 6]

[[fx.slash.stroke]]
picture = "fx"
source = [32, 0, 32, 32]
points = [[-80, 0], [0, 40], [80, 0]]
width = 24
draw = 4
hold = 4
fade = 4

[[fx.pass.sprite]]
picture = "fx"
source = [0, 0, 32, 32]
at = [-100, 0]
to = [100, 0]
lift = 60
frames = 11
size = [20, 20]
alpha = [1, 1]
ease = "linear"
)";
        auto look = p2::read_look(text, "ink.look.gongfu.toml");
        check(look.has_value(), look ? "" : std::format("{}", texts(look.error())));
        if (!look) return;
        const std::string written = p2::write_look(*look);
        auto again = p2::read_look(written, "written.look.gongfu.toml");
        check(again && p2::write_look(*again) == written, again ? written : std::format("{}", texts(again.error())));
        check(look->fx.at("splash").length() == 20 && look->fx.at("slash").length() == 12, "lengths");
        check(look->impacts.size() == 1 && look->impacts[0].fx == "splash" && look->cutins.contains("L3"), "named");
        omui::render::ImageStore images;
        look->pictures[0].ref = images.add(64, 32, std::vector<omui::raster::Rgba8>(64 * 32, omui::raster::Rgba8{20, 20, 20, 255}));
        p2::Painter painter;
        const p2::Scene scene{.width = 400, .height = 300, .ground = 250};
        const p2::Camera cam{0, 0, 1};
        auto inked = [&](std::string_view name, double age) {
            omui::render::DisplayList dl;
            dl.fill_rect({0, 0, 400, 300}, Color::hex(0xffffff));
            painter.fx(*look, look->fx.at(std::string(name)), age, {0, 100}, 1, 1, scene, cam, dl);
            omui::raster::Surface out(400, 300);
            omui::render::CpuRenderer{}.render(dl, out, {&images, nullptr}, {.scale = 1});
            int n = 0;
            for (int y = 0; y < 300; ++y)
                for (int x = 0; x < 400; ++x) n += out.at(x, y).r < 128;
            return std::pair{n, out.hash()};
        };
        const auto s0 = inked("splash", 0), s5 = inked("splash", 5), s12 = inked("splash", 12), s30 = inked("splash", 30);
        check(s0.first > 0 && s5.first > s0.first, std::format("the splash grows: {} then {} pixels", s0.first, s5.first));
        check(s12.first > 0 && s30.first == 0, std::format("droplets fly on after the splash, then nothing: {}, {}", s12.first, s30.first));
        const auto k0 = inked("slash", 0), k3 = inked("slash", 3), k7 = inked("slash", 7);
        check(k0.first > 0 && k3.first > 2 * k0.first && k7.first == k3.first, std::format("drawn out along its curve, then held: {}, {}, {}", k0.first, k3.first, k7.first));
        check(inked("slash", 3).second == k3.second, "the same picture every time");
        // A sprite travelling: from its place to its destination along an arc `lift` high at the middle.
        auto centre = [&](double age) {
            omui::render::DisplayList dl;
            dl.fill_rect({0, 0, 400, 300}, Color::hex(0xffffff));
            painter.fx(*look, look->fx.at("pass"), age, {0, 100}, 1, 1, scene, cam, dl);
            omui::raster::Surface out(400, 300);
            omui::render::CpuRenderer{}.render(dl, out, {&images, nullptr}, {.scale = 1});
            double sx = 0, sy = 0, n = 0;
            for (int y = 0; y < 300; ++y)
                for (int x = 0; x < 400; ++x)
                    if (out.at(x, y).r < 128) sx += x, sy += y, ++n;
            return std::pair{n > 0 ? sx / n : -1.0, n > 0 ? sy / n : -1.0};
        };
        const auto [x0, y0] = centre(0);
        const auto [x5, y5] = centre(5);
        const auto [x10, y10] = centre(10);
        // The picture's middle is x 200; the stage's 100 up is y 150 (ground 250).
        check(std::abs(x0 - 100) < 2 && std::abs(x10 - 300) < 2 && std::abs(x5 - 200) < 2, std::format("along: {:.1f} {:.1f} {:.1f}", x0, x5, x10));
        check(std::abs(y0 - y10) < 1 && std::abs((y0 - y5) - 60) < 2, std::format("over the arc: {:.1f} {:.1f} {:.1f}", y0, y5, y10));
        // The cut-in: sliding in, then across the band (the middle 40%).
        omui::render::DisplayList dl;
        dl.fill_rect({0, 0, 400, 300}, Color::hex(0xffffff));
        painter.panel(*look, look->cutins.at("L3"), 10, 1, scene, dl);
        omui::raster::Surface out(400, 300);
        omui::render::CpuRenderer{}.render(dl, out, {&images, nullptr}, {.scale = 1});
        check(out.at(200, 150).r < 60 && out.at(200, 20).r > 200 && out.at(200, 280).r > 200, "a band across the middle");
        std::string bad = text;
        bad.replace(bad.find("fx = \"splash\""), 14, "fx = \"splosh\"");
        auto b1 = p2::read_look(bad, "bad.look.gongfu.toml");
        check(!b1 && has(texts(b1.error()), "plays `splosh`, which is no effect of the look"), b1 ? "read" : std::format("{}", texts(b1.error())));
    });

    s.test("a stage's painted layers: a picture's region standing in a layer, moving by its parallax, mirrored when flipped; files read back as themselves", [&] {
        const std::string text = R"(gongfu = "0.1"
name = "painted"
sky = ["#000000", "#000000"]
floor = "#000000"
line = "#000000"
marks = false

[[picture]]
name = "far"
file = "far.webp"
size = [64, 32]

[[layer]]
name = "far"
parallax = 0.25
[[layer.image]]
picture = "far"
source = [0, 0, 64, 32]
at = [760, 100]
width = 400
)";
        auto st = p2::read_stage(text, "painted.stage.gongfu.toml");
        check(st.has_value(), st ? "" : std::format("{}", texts(st.error())));
        if (!st) return;
        check(st->pictures.size() == 1 && st->layers[0].images.size() == 1 && st->layers[0].images[0].width == 400, "read");
        const std::string written = p2::write_stage(*st);
        auto again = p2::read_stage(written, "written.stage.gongfu.toml");
        check(again && p2::write_stage(*again) == written, again ? written : std::format("{}", texts(again.error())));
        // The fighters' contact shadows in the stage's colour: read, and written back.
        auto shaded = p2::read_stage(text.substr(0, text.find("\n[[picture]]")) + "contact = \"#10203080\"\n", "shaded.stage.gongfu.toml");
        check(shaded && std::abs(shaded->contact.a - 128 / 255.0f) < 1e-6f && p2::write_stage(*shaded).contains("contact = "), "contact read and written");
        // A picture: its left half red, its right half blue.
        omui::render::ImageStore images;
        std::vector<omui::raster::Rgba8> px(64 * 32);
        for (int y = 0; y < 32; ++y)
            for (int x = 0; x < 64; ++x) px[static_cast<std::size_t>(y * 64 + x)] = x < 32 ? omui::raster::Rgba8{230, 30, 30, 255} : omui::raster::Rgba8{30, 30, 230, 255};
        st->pictures[0].ref = images.add(64, 32, std::move(px));
        auto draw = [&](const p2::Stage& stage, double camera_x) {
            Match m(rules(), fighter(), fighter());
            const p2::Scene scene{.stage = &stage};
            omui::render::DisplayList dl;
            p2::Painter painter;
            painter.stage_layers(stage, false, scene, p2::Camera{camera_x, 0, 1}, 960, 0, 1, dl);
            omui::raster::Surface out(960, 540);
            omui::render::CpuRenderer{}.render(dl, out, {&images, nullptr}, {.scale = 1});
            return out;
        };
        // Where its red half's left edge is on the picture's row through it.
        auto red_from = [](const omui::raster::Surface& s) {
            for (int x = 0; x < 960; ++x)
                if (s.at(x, 300).r > 150 && s.at(x, 300).b < 100) return x;
            return -1;
        };
        auto blue_from = [](const omui::raster::Surface& s) {
            for (int x = 0; x < 960; ++x)
                if (s.at(x, 300).b > 150 && s.at(x, 300).r < 100) return x;
            return -1;
        };
        const auto middle = draw(*st, 960), moved = draw(*st, 1360);
        save(middle, "painted-middle");
        // Bottom left at 760 along: 280 px left of the middle (480 − 200), 100 up from the ground.
        check(std::abs(red_from(middle) - 280) <= 1, std::format("its left edge at {}", red_from(middle)));  // within its edge's smoothing
        check(blue_from(middle) > red_from(middle), "red, then blue");
        // The camera 400 units on: a layer at parallax 0.25 goes 100 the other way.
        check(std::abs(red_from(moved) - 180) <= 1, std::format("moved by its parallax to {}", red_from(moved)));
        p2::Stage flipped = *st;
        flipped.layers[0].images[0].flip = true;
        const auto mirror = draw(flipped, 960);
        check(std::abs(blue_from(mirror) - 280) <= 1 && red_from(mirror) > blue_from(mirror) + 100, std::format("flipped: blue from {}, then red from {}", blue_from(mirror), red_from(mirror)));
        // Problems: a picture not declared, a source outside it.
        std::string bad = text;
        bad.replace(bad.find("picture = \"far\"\nsource"), 15, "picture = \"fur\"");
        auto b1 = p2::read_stage(bad, "bad.stage.gongfu.toml");
        check(!b1 && has(texts(b1.error()), "no picture is called `fur`"), b1 ? "read" : std::format("{}", texts(b1.error())));
        bad = text;
        bad.replace(bad.find("source = [0, 0, 64, 32]"), 23, "source = [0, 0, 64, 64]");
        auto b2 = p2::read_stage(bad, "bad.stage.gongfu.toml");
        check(!b2 && has(texts(b2.error()), "is not inside picture"), b2 ? "read" : std::format("{}", texts(b2.error())));
    });

    s.test("a stage's art: layers by parallax, generators from a seed, sway and flicker by the frame, the world's light; a file that reads back as itself", [&] {
        const std::string text = R"(gongfu = "0.1"
name = "test"
sky = ["#f3eadb", "#e4d5bb"]
floor = "#6d5240"
line = "#2e2119"
marks = false
light = { from = [1, -1], shadow = "#000000" }

[[layer]]
name = "far"
parallax = 0.2
[[layer.lattice]]
at = [-200, 100]
size = [400, 260]
cells = [4, 3]
bar = 6
color = "#5b3a24"
paper = "#f7efe0"

[[layer]]
name = "bamboo"
parallax = 0.5
[[layer.bamboo]]
seed = 7
count = 5
span = [-300, 300]
height = [260, 360]
width = 10
color = "#6f7d5c"
leaves = "#7f8f68"
sway = 3

[[layer]]
name = "lamp"
parallax = 1.2
front = true
[[layer.shape]]
points = [[300, 0], [340, 0], [340, 60], [300, 60]]
color = "#ffcc66"
flicker = 0.4
)";
        auto stage = p2::read_stage(text, "test.stage.gongfu.toml");
        check(stage.has_value(), stage ? "" : std::format("{}", texts(stage.error())));
        if (!stage) return;
        check(stage->layers.size() == 3 && stage->layers[0].shapes.size() == 1 + 5 + 4 && stage->layers[2].front, "a lattice's paper and bars; a front layer");
        check(stage->layers[1].shapes.size() > 25, std::format("bamboo: {} shapes", stage->layers[1].shapes.size()));
        auto again = p2::read_stage(text, "test.stage.gongfu.toml");
        check(again && p2::write_stage(*again) == p2::write_stage(*stage), "a generator gives the same shapes from its seed");
        const std::string written = p2::write_stage(*stage);
        auto back = p2::read_stage(written, "written.stage.gongfu.toml");
        check(back && p2::write_stage(*back) == written, back ? "written back as itself" : std::format("{}", texts(back.error())));
        // Drawn: the bamboo sways over time; the far layer moves less than the near one as the camera pans.
        Match m = close_match();
        p2::Effects fx;
        p2::Scene scene{.stage = &*stage};
        auto hash_at = [&](std::int32_t frames) {
            Match mm = m;
            for (int f = 0; f < frames; ++f) mm.tick(pad(5), pad(5));
            omui::render::DisplayList dl;
            p2::draw(mm, ink, paper, fx, scene, dl);
            return dl.hash();
        };
        check(hash_at(1) != hash_at(60), "swaying");
        omui::render::DisplayList plain, art;
        p2::draw(m, ink, paper, fx, {}, plain);
        p2::draw(m, ink, paper, fx, scene, art);
        check(plain.hash() != art.hash(), "the art is drawn");
        // Problems at their place: a misspelt key, a generator's bad field.
        auto bad = p2::read_stage(text + "\n[[layer]]\nname = \"x\"\nparalax = 1\n", "bad.stage.gongfu.toml");
        check(!bad && has(texts(bad.error()), "a layer has no key `paralax`"), bad ? "read" : std::format("{}", texts(bad.error())));
    });

    s.test("problems in a look at their line and column, with the nearest right word", [] {
        const std::string text = R"(gongfu = "0.1"
name = "broken"

[[palette]]
name = "ink"
body = "#1f2430"

[[palette]]
name = "paper"
bdy = "#e8dcc4"

[[bone]]
name = "hip"
joint = [0, 92]
lenght = 10

[[bone]]
name = "torso"
parent = "hips"
joint = [10, 0]
length = 56

[[part]]
bone = "torso"
shape = "capsle"
size = [18, 15]
color = "bodi"

[clip.idle]
frames = 60
ease = "out_cubik"
angle.torso = [[0, 0], [30, 2], [20, 0]]
angle.tail = [[0, 0]]

[impact.light]
shake = [2, 6]
spark = { style = "rays", colour = "#ffd166" }
)";
        auto bad = p2::read_look(text, "broken.look.gongfu.toml");
        check(!bad.has_value());
        if (bad) return;
        const auto t = texts(bad.error());
        for (std::string_view want : {"broken.look.gongfu.toml:10:1: the first palette has no slot `bdy` (did you mean `body`?)",
                                      "broken.look.gongfu.toml:8:1: palette `paper` has no colour for slot `body`",
                                      "broken.look.gongfu.toml:15:1: a bone has no key `lenght` (did you mean `length`?)",
                                      "broken.look.gongfu.toml:19:10: no bone before `torso` is called `hips` (did you mean `hip`?)",
                                      "broken.look.gongfu.toml:25:9: a part's `shape`: `capsle` is not one (did you mean `capsule`?)",
                                      "broken.look.gongfu.toml:27:9: no palette slot is called `bodi` (did you mean `body`?)",
                                      "broken.look.gongfu.toml:31:8: `out_cubik` is not an ease",
                                      "frame 20 comes after frame 30",
                                      "clip `idle` turns `tail`, which is no bone",
                                      "impact `light`'s spark has no key `colour` (did you mean `color`?)"})
            check(has(t, want), std::format("{}: {}", want, t));
    });

    s.test("impacts: a hit's profile by its name, the block's and the KO's by theirs, the rest by default", [&] {
        p2::Look l = ink;
        l.impacts = {{.name = "light", .shake = 9, .shake_frames = 10, .zoom = 0.1f, .zoom_frames = 10, .spark = p2::Spark::bolt},
                     {.name = "block", .spark = p2::Spark::shards}};
        p2::bind(l, fighter());
        check(l.impact_of == std::vector<std::int32_t>{0}, "the fighter's `light` is the look's first");
        Event hit{.kind = Event::Kind::hit, .impact = 0}, block{.kind = Event::Kind::block, .impact = 0}, ko{.kind = Event::Kind::ko};
        check(&p2::impact_for(l, hit) == &l.impacts[0] && &p2::impact_for(l, block) == &l.impacts[1]);
        check(&p2::impact_for(l, ko) == &p2::default_impact(Event::Kind::ko), "no `ko` profile: the default");
        // The camera: punched in and shaken at contact, as the profile says, and still again after.
        Match m = close_match();
        p2::Effects fx;
        const p2::Scene scene;
        m.tick(pad(5, LP), pad(5));
        fx.record(m);
        for (int f = 0; f < 3 && fx.kept().empty(); ++f) {
            m.tick(pad(5), pad(5));
            fx.record(m);
        }
        check(!fx.kept().empty(), "the jab landed");
        const p2::Camera plain = p2::frame_camera(m, scene), punched = p2::view_camera(m, l, l, fx, scene);
        check(punched.zoom > plain.zoom * 1.05 && (punched.x != plain.x || punched.y != plain.y),
              std::format("zoom {} → {}, x {} → {}", plain.zoom, punched.zoom, plain.x, punched.x));
        for (int f = 0; f < 12; ++f) {
            m.tick(pad(5), pad(5));
            fx.record(m);
        }
        const p2::Camera later = p2::view_camera(m, l, l, fx, scene), still = p2::frame_camera(m, scene);
        check(later.zoom == still.zoom && later.x == still.x && later.y == still.y, "the shake and the punch are over");
    });

    s.test("a giant's footfalls: walking, the camera dips on each step and is back between them; standing, it never dips", [&] {
        p2::Look heavy, light;
        heavy.footfall = {.every = 24, .dip = 3, .frames = 8};
        Match m = close_match();
        p2::Effects fx;
        const p2::Scene scene;
        int dips = 0, level = 0;
        for (int f = 0; f < 48; ++f) {
            m.tick(pad(4), pad(5));  // walking back: still a step
            const bool walking = m.state().f[0].mode == Mode::walk_back;
            const double y = p2::view_camera(m, heavy, light, fx, scene).y, plain = p2::view_camera(m, light, light, fx, scene).y;
            if (!walking) continue;
            const std::int32_t phase = m.state().frame % 24;
            if (phase == 0) dips += y < plain;
            if (phase >= 8) level += y == plain;
        }
        check(dips == 2 && level == 32, std::format("dips {}, level {}", dips, level));
        Match idle = close_match();
        for (int f = 0; f < 30; ++f) {
            idle.tick(pad(5), pad(5));
            check(p2::view_camera(idle, heavy, light, fx, scene).y == p2::view_camera(idle, light, light, fx, scene).y, "standing: no dip");
        }
    });

    s.test("set pieces (G-15) drawn: a whole pane, a cracked one, one that turns projectiles back — the owner's glass; pinned", [&] {
        Match m = close_match();
        State st = m.state();
        const Fixed mid = m.system().stage_width / 2;
        st.pieces[0] = {1, 0, {mid - 150_fx, 0_fx}, 2, 300, 0, -1, 0, 0, 0};
        st.pieces[1] = {1, 0, {mid + 150_fx, 0_fx}, 1, 300, 0, -1, 0, 0, 0};
        st.pieces[2] = {1, 1, {mid + 260_fx, 0_fx}, 2, 300, 1, -1, 0, 0, 0};
        st.frame = 40;
        m.restore(st);
        p2::Look glassy = ink;
        glassy.pane.glass = Color::hex(0x9fd8ff, 0.3f);
        p2::Effects fx;
        omui::render::DisplayList dl;
        p2::draw(m, glassy, paper, fx, {}, dl);
        omui::raster::Surface px(960, 540);
        omui::render::CpuRenderer{}.render(dl, px, {}, {.scale = 1});
        save(px, "panes");
        Hash h;
        h.u64(dl.hash());
        std::println("    panes {}", h.hex());
        check_eq(h.hex(), std::string("50533a6966aaed3f"));
        // A pane's colours read back from a look file.
        auto look = p2::read_look("pane = { glass = \"#9fd8ff4c\", edge = \"#ffffffcc\", shine = \"#ffffff80\", reflect = \"#ffd36be6\" }\n" +
                                      read(here() / "data" / "kata.look.gongfu.toml"),
                                  "kata.look.gongfu.toml");
        check(look && look->pane.reflect == Color::parse("#ffd36be6") && p2::read_look(p2::write_look(*look), "w.look.gongfu.toml")->pane.glass == look->pane.glass,
              "the pane's colours, written back");
    });

    s.test("set pieces drawn from a picture: the whole region standing on a pane, the cracked one once worn, a gold edge on one that turns projectiles back; read back", [&] {
        auto look = p2::read_look("pane = { glass = \"#9fd8ff4c\", edge = \"#ffffffcc\", shine = \"#ffffff80\", reflect = \"#ffd36be6\", picture = \"fx\", "
                                  "whole = [0, 0, 10, 30], cracked = [10, 0, 10, 30] }\n" +
                                      read(here() / "data" / "kata.look.gongfu.toml") + "\n[[picture]]\nname = \"fx\"\nfile = \"fx.webp\"\nsize = [20, 30]\n",
                                  "kata.look.gongfu.toml");
        check(look.has_value(), look ? "" : std::format("{}", texts(look.error())));
        if (!look) return;
        const std::string written = p2::write_look(*look);
        auto again = p2::read_look(written, "w.look.gongfu.toml");
        check(again && again->pane.picture == 0 && again->pane.cracked.x == 10 && p2::write_look(*again) == written, "the pane's picture, written back");
        // The picture: its left half blue (whole), its right half red (cracked).
        std::vector<omui::raster::Rgba8> px(20 * 30);
        for (int y = 0; y < 30; ++y)
            for (int x = 0; x < 20; ++x) px[static_cast<std::size_t>(y * 20 + x)] = x < 10 ? omui::raster::Rgba8{0, 0, 255, 255} : omui::raster::Rgba8{255, 0, 0, 255};
        omui::render::ImageStore images;
        look->pictures[0].ref = images.add(20, 30, std::move(px));
        Match m = close_match();
        State st = m.state();
        const Fixed mid = m.system().stage_width / 2;
        st.pieces[0] = {1, 0, {mid - 150_fx, 0_fx}, 2, 300, 0, -1, 0, 0, 0};
        st.pieces[1] = {1, 0, {mid + 150_fx, 0_fx}, 1, 300, 0, -1, 0, 0, 0};
        st.frame = 40;
        m.restore(st);
        p2::Effects fx;
        omui::render::DisplayList dl;
        p2::draw(m, *look, paper, fx, {}, dl);
        omui::raster::Surface out(960, 540);
        omui::render::CpuRenderer{}.render(dl, out, {&images, nullptr}, {.scale = 1});
        save(out, "panes-drawn");
        const p2::Camera cam = p2::frame_camera(m, {});
        const auto at = [&](Fixed x) {
            const Box& box = m.system().piece_box;
            const p2::Point p = p2::on_picture(p2::Scene{}, cam, (x + box.x).to_double() + box.w.to_double() / 2, (box.y + box.h / 2).to_double());
            return out.at(static_cast<int>(p.x), static_cast<int>(p.y));
        };
        const auto whole = at(mid - 150_fx), cracked = at(mid + 150_fx);
        check(whole.b > 200 && whole.r < 60 && cracked.r > 200 && cracked.b < 60,
              std::format("whole {} {} {}, cracked {} {} {}", whole.r, whole.g, whole.b, cracked.r, cracked.g, cracked.b));
    });

    s.test("hit feel: the struck shakes through the hitstop; the camera shakes only where the impact asks", [&] {
        const auto play = [&](float shake) {
            p2::Look l = ink;
            l.impacts = {{.name = "light", .shake = shake, .shake_frames = 8, .spark_count = 0, .spark_frames = 0}};
            p2::bind(l, fighter());
            Match m = close_match();
            p2::Effects fx;
            std::vector<std::uint64_t> lists;
            std::vector<bool> moved;
            std::vector<std::int32_t> stops;
            for (int f = 0; f < 12; ++f) {
                m.tick(f == 0 ? pad(5, LP) : pad(5), pad(5));
                fx.record(m);
                omui::render::DisplayList dl;
                p2::draw(m, l, paper, fx, {}, dl);
                lists.push_back(dl.hash());
                const p2::Camera seen = p2::view_camera(m, l, l, fx, {}), framed = p2::frame_camera(m, {});
                moved.push_back(seen.x != framed.x || seen.y != framed.y);
                stops.push_back(m.state().f[1].stop);
            }
            return std::tuple{lists, moved, stops};
        };
        const auto [still_lists, still_moved, stops] = play(0);
        const auto [shaken_lists, shaken_moved, _] = play(6);
        check(std::ranges::none_of(still_moved, std::identity{}), "a light impact (shake 0) leaves the camera alone");
        check(std::ranges::any_of(shaken_moved, std::identity{}), "an impact that shakes moves it");
        // Both stand still in the hitstop: frame to frame, only the struck's shake changes the picture.
        int stopped = 0, changed = 0;
        for (std::size_t f = 1; f < stops.size(); ++f)
            if (stops[f - 1] > 1 && stops[f] > 0) ++stopped, changed += still_lists[f] != still_lists[f - 1];
        check(stopped >= 3 && changed == stopped, std::format("{} of {} stopped frames shaken", changed, stopped));
    });

    s.test("each spark style and flash draws its own picture, pinned", [&] {
        std::vector<std::uint64_t> lists;
        Hash all;
        for (const p2::Spark style : {p2::Spark::rays, p2::Spark::ring, p2::Spark::splash, p2::Spark::shards, p2::Spark::bolt, p2::Spark::cracks}) {
            for (const p2::Flash flash : {p2::Flash::none, p2::Flash::invert}) {
                if (flash == p2::Flash::invert && style != p2::Spark::bolt) continue;
                p2::Look l = ink;
                l.impacts = {{.name = "light", .flash = flash, .flash_frames = 6, .spark = style, .spark_size = 50, .spark_count = 9, .spark_frames = 16}};
                p2::bind(l, fighter());
                Match m = close_match();
                p2::Effects fx;
                for (int f = 0; f < 6; ++f) {
                    m.tick(f == 0 ? pad(5, LP) : pad(5), pad(5));
                    fx.record(m);
                }
                omui::render::DisplayList dl;
                p2::draw(m, l, paper, fx, {}, dl);
                lists.push_back(dl.hash());
                all.u64(dl.hash());
                omui::raster::Surface px(960, 540);
                omui::render::CpuRenderer{}.render(dl, px, {}, {.scale = 1});
                save(px, std::format("spark-{}-{}", static_cast<int>(style), static_cast<int>(flash)));
            }
        }
        check_eq(std::set<std::uint64_t>(lists.begin(), lists.end()).size(), lists.size());
        std::println("    sparks {}", all.hex());
        check_eq(all.hex(), std::string("bf3e3b2b5499bef8"));
    });

    s.test("the training fighter's look, every clip at three moments: a contact sheet (saved), its picture pinned", [] {
        auto look = p2::read_look(read(here() / "data" / "kata.look.gongfu.toml"), "kata.look.gongfu.toml");
        check(look.has_value());
        if (!look) return;
        // A fighter whose moves are named as the clips are, so pose() plays each.
        FighterDef def = body();
        def.moves.clear();
        for (const auto& [name, clip] : look->clips) {
            MoveDef m{.name = name, .command = {.buttons = LP}, .total = std::max(clip.frames, 1)};
            def.moves.push_back(m);
        }
        const Fighter f = *compile(def);
        const std::size_t columns = 6, rows = (look->clips.size() + columns - 1) / columns;
        const float cell = 200;
        omui::render::DisplayList dl;
        dl.fill_rect({0, 0, cell * columns, cell * static_cast<float>(rows)}, Color::hex(0xf4f1ea));
        p2::Pose pose;
        std::size_t k = 0;
        for (const auto& [name, clip] : look->clips) {
            const float ox = cell * static_cast<float>(k % columns), oy = cell * static_cast<float>(k / columns);
            dl.fill_rect({ox, oy + cell - 20, cell, 1}, Color::hex(0xb7aa90));
            for (const double at : {0.0, 0.5, 1.0}) {
                FighterState s;
                s.mode = Mode::move;
                s.move = f.find(name);
                s.move_frame = 1 + static_cast<std::int32_t>(at * (std::max(clip.frames, 1) - 1));
                s.facing = 1;
                p2::pose(*look, f, s, 0, pose);
                const float alpha = at == 0.5 ? 1.0f : 0.3f;
                for (std::size_t b = 0; b < pose.size(); ++b) {
                    omui::render::Path line;
                    line.move_to({ox + cell / 2 + pose[b].joint.x * 0.8f, oy + cell - 20 - pose[b].joint.y * 0.8f});
                    line.line_to({ox + cell / 2 + pose[b].tip.x * 0.8f, oy + cell - 20 - pose[b].tip.y * 0.8f});
                    const bool front = look->bones[b].name.ends_with("front");
                    dl.stroke_path(line, {.width = 4, .cap = omui::render::LineCap::round},
                                   omui::render::Paint::solid(Color::hex(front ? 0xc0392b : 0x1f2430, alpha)));
                }
            }
            ++k;
        }
        omui::raster::Surface px(static_cast<int>(cell * columns), static_cast<int>(cell * static_cast<float>(rows)));
        omui::render::CpuRenderer{}.render(dl, px, {}, {.scale = 1});
        save(px, "kata-clips");
        std::println("    kata clips {:016x}", picture(px));
        check_eq(picture(px), std::uint64_t{0x9bf4573b547b73eaULL});
    });

    return s.run(argc, argv);
}
