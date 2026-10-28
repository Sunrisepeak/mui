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
                        .pushback = 2_fx, .impact = 1});
    f.moves.push_back(jab);
    return f;
}

const Fighter& fighter() {
    static const Fighter f = *compile(body());
    return f;
}

enum Bones : std::int32_t { hip, torso, head, upper_back, fore_back, upper_front, fore_front, thigh_back, shin_back, thigh_front, shin_front };

hanim::Track<double> keys(std::initializer_list<std::pair<int, double>> frame_values) {
    hanim::Track<double> t;
    for (const auto& [frame, value] : frame_values) t.key(hanim::seconds(frame / 60.0), value, hanim::ease::out_cubic);
    return t;
}

// An ink-and-paper fighter: eleven bones, capsules and an ellipse head.
p2::Look look(Color body_colour, Color limb_colour) {
    p2::Look l;
    l.name = "shade";
    const double up = 1.5707963267948966, down = 3.141592653589793;
    l.bones = {
        {"hip", -1, {0, 92}, 10, static_cast<float>(up)},
        {"torso", hip, {10, 0}, 56, 0},
        {"head", torso, {58, 0}, 24, 0},
        {"upper_back", torso, {50, 7}, 30, -2.5f},
        {"fore_back", upper_back, {30, 0}, 28, 1.1f},
        {"upper_front", torso, {50, -7}, 30, -2.3f},
        {"fore_front", upper_front, {30, 0}, 28, 1.6f},
        {"thigh_back", hip, {0, 7}, 46, static_cast<float>(down - 0.3)},
        {"shin_back", thigh_back, {46, 0}, 48, -0.25f},
        {"thigh_front", hip, {0, -7}, 46, static_cast<float>(down + 0.35)},
        {"shin_front", thigh_front, {46, 0}, 48, 0.35f},
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
    idle.angles[torso] = keys({{0, 0}, {30, 0.04}, {60, 0}});
    l.clips["idle"] = idle;
    p2::Clip jab{.frames = 12};
    jab.angles.resize(l.bones.size());
    jab.angles[upper_front] = keys({{0, 0}, {3, 0.75}, {5, 0.75}, {11, 0}});  // the shoulder swings forward
    jab.angles[fore_front] = keys({{0, 0}, {3, -1.55}, {5, -1.55}, {11, 0}});  // the elbow straightens
    jab.angles[torso] = keys({{0, 0}, {3, -0.12}, {11, 0}});
    l.clips["5LP"] = jab;
    p2::Clip hurt{.frames = 15};
    hurt.angles.resize(l.bones.size());
    hurt.angles[torso] = keys({{0, 0.35}, {15, 0}});
    hurt.angles[head] = keys({{0, 0.3}, {15, 0}});
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
        check(lists == std::vector<std::uint64_t>{0x581a83c8888df0daULL, 0xc136c369528318afULL, 0x05a902b971dcebe3ULL}, "lists pinned");
        check(pixels == std::vector<std::uint64_t>{0xe4f8cfc3c89e5a5eULL, 0x4494aef9121b6654ULL, 0xb28cb2532553bc57ULL}, "pixels pinned");
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

    return s.run(argc, argv);
}
