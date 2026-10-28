module gongfu.present2d;

import std;
import gongfu;
import hanim;
import openmui.geom;
import openmui.render.vector;

namespace gongfu::present2d {

namespace {

namespace dm = hanim::dm;
using omui::geom::Affine;
using omui::geom::Rect;
namespace render = omui::render;

constexpr std::array<std::string_view, 14> mode_clips{"idle",    "walk_forward", "walk_back", "crouch", "prejump",   "air",    "landing",
                                                      "move",    "hitstun",      "blockstun", "knockdown", "wakeup", "thrown", "ko"};

double stage(Fixed f) { return f.to_double(); }

// Stage to picture: x right, y down; the camera's point at the middle, on the ground line.
struct View {
    const Scene& scene;
    Camera       cam;
    [[nodiscard]] Point at(double x, double y) const {
        return {static_cast<float>(scene.width / 2 + (x - cam.x) * cam.zoom), static_cast<float>(scene.ground - (y - cam.y) * cam.zoom)};
    }
    [[nodiscard]] float size(double s) const { return static_cast<float>(s * cam.zoom); }
};

Color fade(Color c, double alpha) {
    c.a *= static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    return c;
}

// A circle as a path through 24 points, its trigonometry deterministic.
void circle(render::Path& p, Point c, float r) {
    for (int i = 0; i <= 24; ++i) {
        const double a = dm::two_pi * i / 24;
        const Point q{c.x + r * static_cast<float>(dm::cos(a)), c.y + r * static_cast<float>(dm::sin(a))};
        if (i == 0) p.move_to(q);
        else p.line_to(q);
    }
    p.close();
}

// A capsule from a to b, radius ra at a and rb at b: two circles and the band between.
void capsule(render::Path& p, Point a, Point b, float ra, float rb) {
    circle(p, a, ra);
    circle(p, b, rb);
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0) return;
    const float nx = -dy / len, ny = dx / len;
    // Wound as the circles are (angle increasing), so the nonzero rule fills
    // where they overlap instead of cancelling.
    std::array<Point, 4> q{Point{a.x + nx * ra, a.y + ny * ra}, Point{b.x + nx * rb, b.y + ny * rb}, Point{b.x - nx * rb, b.y - ny * rb},
                           Point{a.x - nx * ra, a.y - ny * ra}};
    float area = 0;
    for (std::size_t i = 0; i < q.size(); ++i) area += q[i].x * q[(i + 1) % q.size()].y - q[(i + 1) % q.size()].x * q[i].y;
    if (area < 0) std::ranges::reverse(q);
    p.move_to(q[0]);
    for (std::size_t i = 1; i < q.size(); ++i) p.line_to(q[i]);
    p.close();
}

void ellipse(render::Path& p, Point centre, double angle, float along, float across) {
    const double ca = dm::cos(angle), sa = dm::sin(angle);
    for (int i = 0; i <= 24; ++i) {
        const double t = dm::two_pi * i / 24;
        const double x = along * dm::cos(t), y = across * dm::sin(t);
        const Point q{static_cast<float>(centre.x + x * ca - y * sa), static_cast<float>(centre.y + x * sa + y * ca)};
        if (i == 0) p.move_to(q);
        else p.line_to(q);
    }
    p.close();
}

// The shake of the most recent heavy contact.
Point shake(const Effects& effects, std::int32_t frame) {
    for (auto it = effects.kept().rbegin(); it != effects.kept().rend(); ++it) {
        const std::int32_t age = frame - it->frame;
        if (age < 0) continue;
        if (age >= 10) break;
        const Event& e = it->event;
        if (e.kind != Event::Kind::hit && e.kind != Event::Kind::counter && e.kind != Event::Kind::ko) continue;
        const double amp = std::min(14.0, e.damage / 70.0) * (1.0 - age / 10.0);
        return {static_cast<float>(amp * dm::sin(age * 2.3 + e.impact)), static_cast<float>(amp * 0.5 * dm::cos(age * 1.7 + e.impact))};
    }
    return {};
}

}  // namespace

std::string_view clip_name(const Fighter& fighter, const FighterState& s) noexcept {
    if (s.mode == Mode::move && s.move >= 0) return fighter.def.moves[static_cast<std::size_t>(s.move)].name;
    return mode_clips[static_cast<std::size_t>(s.mode)];
}

Pose pose(const Look& look, const Fighter& fighter, const FighterState& s, std::int32_t frame) {
    auto it = look.clips.find(clip_name(fighter, s));
    if (it == look.clips.end()) it = look.clips.find(std::string_view("idle"));
    const Clip* clip = it == look.clips.end() ? nullptr : &it->second;
    // A move's clip at its frame; a stun's at the frames it has taken; the rest at the match's, looping.
    std::int32_t t = frame;
    if (clip) {
        if (s.mode == Mode::move) t = std::max(0, s.move_frame - 1);
        else if (s.mode == Mode::hitstun || s.mode == Mode::blockstun || s.mode == Mode::knockdown || s.mode == Mode::wakeup)
            t = std::max(0, clip->frames - s.stun);
        if (clip->loop && clip->frames > 0) t %= clip->frames;
        else t = std::min(t, clip->frames);
    }
    const hanim::seconds at{t / 60.0};
    const double rise = clip ? clip->rise.value(at) : 0.0;
    const double facing = s.facing < 0 ? -1.0 : 1.0;
    Pose out(look.bones.size());
    for (std::size_t b = 0; b < look.bones.size(); ++b) {
        const Bone& bone = look.bones[b];
        double local = bone.rest;
        if (clip && b < clip->angles.size() && !clip->angles[b].empty()) local += clip->angles[b].value(at);
        BonePlace& p = out[b];
        if (bone.parent < 0) {
            p.joint = {static_cast<float>(stage(s.pos.x) + bone.joint.x * facing), static_cast<float>(stage(s.pos.y) + bone.joint.y + rise)};
            p.angle = facing > 0 ? local : dm::pi - local;
        } else {
            const BonePlace& q = out[static_cast<std::size_t>(bone.parent)];
            const double ca = dm::cos(q.angle), sa = dm::sin(q.angle);
            const double jx = bone.joint.x, jy = bone.joint.y * facing;
            p.joint = {static_cast<float>(q.joint.x + jx * ca - jy * sa), static_cast<float>(q.joint.y + jx * sa + jy * ca)};
            p.angle = q.angle + facing * local;
        }
        p.tip = {static_cast<float>(p.joint.x + bone.length * dm::cos(p.angle)), static_cast<float>(p.joint.y + bone.length * dm::sin(p.angle))};
    }
    return out;
}

Camera frame_camera(const Match& match, const Scene& scene) {
    const auto& a = match.state().f[0];
    const auto& b = match.state().f[1];
    const double x0 = stage(a.pos.x), x1 = stage(b.pos.x);
    const double apart = std::abs(x1 - x0) + 420;  // room for both bodies
    const double zoom = std::clamp(scene.width / apart, 0.55, 1.15);
    const double half = scene.width / 2 / zoom, width = stage(match.system().stage_width);
    const double x = std::clamp((x0 + x1) / 2, half, std::max(half, width - half));
    const double high = std::max(stage(a.pos.y), stage(b.pos.y));
    return {x, std::max(0.0, high - 160), zoom};
}

void Effects::record(const Match& match) {
    for (const Event& e : match.events()) kept_.push_back({match.state().frame, e});
    // A few seconds is all a picture reads.
    const std::int32_t keep_from = match.state().frame - 120;
    std::erase_if(kept_, [&](const Kept& k) { return k.frame < keep_from; });
}

void Effects::rewind(std::int32_t frame) {
    std::erase_if(kept_, [&](const Kept& k) { return k.frame >= frame; });
}

void draw(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene, render::DisplayList& out) {
    const State& st = match.state();
    Camera cam = frame_camera(match, scene);
    const Point jolt = shake(effects, st.frame);
    cam.x -= jolt.x / cam.zoom;
    cam.y += jolt.y / cam.zoom;
    const View view{scene, cam};

    // The stage: sky, floor, the ground line, and marks along it that move with the camera.
    const std::array<render::GradientStop, 2> sky{{{0, scene.sky_top}, {1, scene.sky_bottom}}};
    render::Paint sky_paint;
    sky_paint.kind = render::Paint::Kind::gradient;
    sky_paint.gradient = {.kind = render::GradientKind::linear, .start = {0, 0}, .end = {0, scene.ground}, .stops = sky};
    out.fill_rect({0, 0, scene.width, scene.ground}, sky_paint);
    const Point ground = view.at(0, 0);
    out.fill_rect({0, ground.y, scene.width, scene.height - ground.y}, scene.floor);
    out.fill_rect({0, ground.y, scene.width, 2}, scene.line);
    for (int m = 0; m <= static_cast<int>(stage(match.system().stage_width)); m += 160) {
        const Point p = view.at(m, 0);
        if (p.x >= -4 && p.x <= scene.width + 4) out.fill_rect({p.x - 1, p.y, 2, view.size(14)}, fade(scene.line, 0.6));
    }

    // The fighters: outlines behind, parts back to front.
    for (std::int32_t i = 0; i < 2; ++i) {
        const Look& look = i == 0 ? left : right;
        const FighterState& s = st.f[static_cast<std::size_t>(i)];
        const Pose p = pose(look, match.fighter(i), s, st.frame);
        std::vector<std::size_t> order(look.parts.size());
        std::iota(order.begin(), order.end(), std::size_t{0});
        std::ranges::stable_sort(order, {}, [&](std::size_t k) { return look.parts[k].depth; });
        for (const bool outline : {true, false}) {
            for (std::size_t k : order) {
                const Part& part = look.parts[k];
                if (part.bone < 0 || static_cast<std::size_t>(part.bone) >= p.size()) continue;
                const BonePlace& b = p[static_cast<std::size_t>(part.bone)];
                const float grow = outline ? look.outline_width : 0;
                render::Path path;
                const Point j = view.at(b.joint.x, b.joint.y), t = view.at(b.tip.x, b.tip.y);
                switch (part.shape) {
                    case Shape::capsule: capsule(path, j, t, view.size(part.start) + grow, view.size(part.end) + grow); break;
                    case Shape::ellipse:
                        ellipse(path, {(j.x + t.x) / 2, (j.y + t.y) / 2}, -b.angle, view.size(part.start) + grow, view.size(part.end) + grow);
                        break;
                    case Shape::polygon: {
                        const double ca = dm::cos(b.angle), sa = dm::sin(b.angle), facing = s.facing < 0 ? -1.0 : 1.0;
                        for (std::size_t q = 0; q < part.points.size(); ++q) {
                            const double px = part.points[q].x, py = part.points[q].y * facing;
                            const Point at = view.at(b.joint.x + px * ca - py * sa, b.joint.y + px * sa + py * ca);
                            if (q == 0) path.move_to(at);
                            else path.line_to(at);
                        }
                        path.close();
                        break;
                    }
                }
                if (outline && part.shape == Shape::polygon) {
                    out.stroke_path(path, {.width = 2 * look.outline_width, .join = render::LineJoin::round}, render::Paint::solid(look.outline));
                } else {
                    out.fill_path(path, render::FillRule::nonzero, render::Paint::solid(outline ? look.outline : part.color));
                }
            }
        }
    }

    // Projectiles: a core and a halo.
    for (const ProjectileState& pr : st.p) {
        if (!pr.alive) continue;
        const Point c = view.at(stage(pr.pos.x), stage(pr.pos.y));
        render::Path halo, core;
        circle(halo, c, view.size(26));
        circle(core, c, view.size(14));
        out.fill_path(halo, render::FillRule::nonzero, render::Paint::solid(Color::hex(0x6fd3ff, 0.35f)));
        out.fill_path(core, render::FillRule::nonzero, render::Paint::solid(Color::hex(0xe9fbff)));
    }

    // Sparks: rays for a hit (red for a counter), a ring for a block or a throw; a KO flashes.
    for (const Effects::Kept& k : effects.kept()) {
        const std::int32_t age = st.frame - k.frame;
        if (age < 0 || age >= 16) continue;
        const Event& e = k.event;
        const Point c = view.at(stage(e.at.x), stage(e.at.y));
        const double grow = 1 - (1 - age / 16.0) * (1 - age / 16.0);  // ease out
        const double alpha = 1 - age / 16.0;
        if (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter) {
            const Color colour = e.kind == Event::Kind::counter ? Color::hex(0xff4d5e) : Color::hex(0xffd166);
            const double reach = (18 + e.damage / 30.0) * (0.35 + 0.65 * grow);  // already visible on the frame it lands
            for (int r = 0; r < 8; ++r) {
                const double a = dm::two_pi * r / 8 + e.impact * 0.37;
                render::Path ray;
                ray.move_to({c.x + view.size(reach * 0.35) * static_cast<float>(dm::cos(a)), c.y + view.size(reach * 0.35) * static_cast<float>(dm::sin(a))});
                ray.line_to({c.x + view.size(reach) * static_cast<float>(dm::cos(a)), c.y + view.size(reach) * static_cast<float>(dm::sin(a))});
                out.stroke_path(ray, {.width = view.size(4 * (1 - age / 20.0)), .cap = render::LineCap::round}, render::Paint::solid(fade(colour, alpha)));
            }
        } else if (e.kind == Event::Kind::block || e.kind == Event::Kind::throw_ || e.kind == Event::Kind::tech) {
            render::Path ring;
            circle(ring, c, view.size(16 + 30 * grow));
            const Color colour = e.kind == Event::Kind::block ? Color::hex(0x7ad7ff) : Color::hex(0xf2f2f2);
            out.stroke_path(ring, {.width = view.size(3)}, render::Paint::solid(fade(colour, alpha)));
        } else if (e.kind == Event::Kind::ko && age < 12) {
            out.fill_rect({0, 0, scene.width, scene.height}, Color::hex(0xffffff, static_cast<float>(0.6 * (1 - age / 12.0))));
        }
    }
}

}  // namespace gongfu::present2d
