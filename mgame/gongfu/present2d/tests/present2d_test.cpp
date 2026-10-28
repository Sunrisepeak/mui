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
        check(lists == std::vector<std::uint64_t>{0x8a625a353d3b0427ULL, 0xff0651146a403635ULL, 0xda408be667b5e60bULL}, "lists pinned");
        check(pixels == std::vector<std::uint64_t>{0xe4f8cfc3c89e5a5eULL, 0x3403ad614b461442ULL, 0x7eb4597c5478690aULL}, "pixels pinned");
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
        check_eq(h.hex(), std::string("62e5a7979533bae1"));
        // A pane's colours read back from a look file.
        auto look = p2::read_look("pane = { glass = \"#9fd8ff4c\", edge = \"#ffffffcc\", shine = \"#ffffff80\", reflect = \"#ffd36be6\" }\n" +
                                      read(here() / "data" / "kata.look.gongfu.toml"),
                                  "kata.look.gongfu.toml");
        check(look && look->pane.reflect == Color::parse("#ffd36be6") && p2::read_look(p2::write_look(*look), "w.look.gongfu.toml")->pane.glass == look->pane.glass,
              "the pane's colours, written back");
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
        check_eq(all.hex(), std::string("ddf03bff612ad757"));
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
