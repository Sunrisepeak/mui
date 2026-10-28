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

constexpr std::array<std::string_view, 17> mode_clips{"idle",    "walk_forward", "walk_back", "crouch", "prejump", "air",   "landing", "move",    "hitstun",
                                                      "blockstun", "knockdown", "wakeup", "thrown", "ko",      "reset", "clash", "broken"};

double stage(Fixed f) { return f.to_double(); }

// Stage to picture: x right, y down; the camera's point at the middle, on the ground line.
struct View {
    const Scene& scene;
    Camera       cam;
    [[nodiscard]] Point at(double x, double y) const { return on_picture(scene, cam, x, y); }
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

bool contact(Event::Kind k) noexcept {
    return k == Event::Kind::hit || k == Event::Kind::counter || k == Event::Kind::block || k == Event::Kind::throw_ || k == Event::Kind::ko ||
           k == Event::Kind::clash || k == Event::Kind::deflect || k == Event::Kind::guard_break;
}

const Look& look_of(const Event& e, const Look& left, const Look& right) noexcept { return e.attacker == 1 ? right : left; }

// 1 at contact, easing (quadratically) to 0 after `frames`.
double falloff(std::int32_t age, std::int32_t frames) noexcept {
    if (frames <= 0 || age < 0 || age >= frames) return 0;
    const double u = 1 - static_cast<double>(age) / frames;
    return u * u;
}

// A number in [0, 1) from an event and a counter: a spark's scatter, the
// same on every machine and after every rollback.
double scatter(const Effects::Kept& k, std::uint32_t i) noexcept {
    // splitmix64's finaliser over each field in turn: every bit of the input moves every bit of the result.
    auto mix = [](std::uint64_t z) {
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
        return z ^ (z >> 31);
    };
    std::uint64_t h = 0x9e3779b97f4a7c15ull;
    for (const std::uint64_t v : {static_cast<std::uint64_t>(k.frame), static_cast<std::uint64_t>(k.event.attacker), static_cast<std::uint64_t>(k.event.move),
                                  static_cast<std::uint64_t>(k.event.hit), static_cast<std::uint64_t>(i)})
        h = mix(h + v + 0x9e3779b97f4a7c15ull);
    return static_cast<double>(h >> 40) / static_cast<double>(1u << 24);
}

}  // namespace

std::string_view clip_name(const Fighter& fighter, const FighterState& s) noexcept {
    if (s.mode == Mode::move && s.move >= 0) return fighter.def.moves[static_cast<std::size_t>(s.move)].name;
    return mode_clips[static_cast<std::size_t>(s.mode)];
}

Pose pose(const Look& look, const Fighter& fighter, const FighterState& s, std::int32_t frame) {
    Pose out;
    pose(look, fighter, s, frame, out);
    return out;
}

void pose(const Look& look, const Fighter& fighter, const FighterState& s, std::int32_t frame, Pose& out) {
    const std::string_view name = clip_name(fighter, s);
    auto it = look.clips.find(name);
    // One clip may serve a special's every strength: 236P plays for 236LP, 236MP and 236HP.
    if (it == look.clips.end() && name.size() >= 2) {
        const std::string_view tail = name.substr(name.size() - 2);
        const char general = tail == "LP" || tail == "MP" || tail == "HP" ? 'P' : tail == "LK" || tail == "MK" || tail == "HK" ? 'K' : '\0';
        if (general) {
            std::array<char, 48> buffer{};
            const std::size_t n = std::min(name.size() - 2, buffer.size() - 1);
            std::copy_n(name.begin(), n, buffer.begin());
            buffer[n] = general;
            it = look.clips.find(std::string_view(buffer.data(), n + 1));
        }
    }
    if (it == look.clips.end() && s.mode == Mode::reset) it = look.clips.find(std::string_view("air"));  // an air reset falls as a jump does
    if (it == look.clips.end() && s.mode == Mode::clash) it = look.clips.find(std::string_view("blockstun"));  // thrown back by the clash
    if (it == look.clips.end() && s.mode == Mode::broken) it = look.clips.find(std::string_view("hitstun"));  // staggered
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
    out.resize(look.bones.size());
    for (std::size_t b = 0; b < look.bones.size(); ++b) {
        const Bone& bone = look.bones[b];
        double local = bone.rest;
        if (clip && b < clip->angles.size() && !clip->angles[b].empty()) local += clip->angles[b].value(at);
        local *= dm::pi / 180;  // a look's angles are degrees
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
}

const Impact& default_impact(Event::Kind kind) noexcept {
    static const Impact hit{.name = "hit", .shake = 4, .shake_frames = 8, .spark = Spark::rays, .spark_size = 30, .spark_count = 8, .spark_frames = 16};
    static const Impact block{.name = "block", .shake = 1, .shake_frames = 4, .spark = Spark::ring, .spark_size = 46, .spark_count = 1, .spark_frames = 14,
                              .spark_color = Color::hex(0x7ad7ff)};
    static const Impact grab{.name = "throw", .shake = 6, .shake_frames = 10, .spark = Spark::ring, .spark_size = 56, .spark_count = 1, .spark_frames = 16,
                             .spark_color = Color::hex(0xf2f2f2)};
    static const Impact ko{.name = "ko", .shake = 12, .shake_frames = 20, .zoom = 0.08f, .zoom_frames = 30, .flash = Flash::white, .flash_frames = 12,
                           .spark = Spark::rays, .spark_size = 60, .spark_count = 12, .spark_frames = 24};
    // System 0.3: blades meeting, a deflect, a guard breaking.
    static const Impact clash{.name = "clash", .shake = 6, .shake_frames = 10, .zoom = 0.03f, .zoom_frames = 12, .spark = Spark::rays, .spark_size = 54,
                              .spark_count = 12, .spark_frames = 14, .spark_color = Color::hex(0xfff3c4), .counter_color = Color::hex(0xfff3c4)};
    static const Impact deflect{.name = "deflect", .shake = 2, .shake_frames = 5, .spark = Spark::ring, .spark_size = 36, .spark_count = 1,
                                .spark_frames = 10, .spark_color = Color::white(), .counter_color = Color::white()};
    static const Impact broken{.name = "break", .shake = 10, .shake_frames = 16, .zoom = 0.06f, .zoom_frames = 20, .flash = Flash::white, .flash_frames = 4,
                               .spark = Spark::shards, .spark_size = 72, .spark_count = 14, .spark_frames = 22, .spark_color = Color::hex(0xe8f1ff),
                               .counter_color = Color::hex(0xe8f1ff)};
    switch (kind) {
        case Event::Kind::clash: return clash;
        case Event::Kind::deflect: return deflect;
        case Event::Kind::guard_break: return broken;
        case Event::Kind::block: return block;
        case Event::Kind::throw_:
        case Event::Kind::tech: return grab;
        case Event::Kind::ko: return ko;
        default: return hit;
    }
}

void use_palette(Look& look, std::size_t index) {
    if (index >= look.palettes.size()) return;
    const auto& colours = look.palettes[index];
    for (Part& p : look.parts)
        if (p.slot >= 0 && static_cast<std::size_t>(p.slot) < colours.size()) p.color = colours[static_cast<std::size_t>(p.slot)];
}

void bind(Look& look, const Fighter& fighter) {
    look.impact_of.assign(fighter.def.impacts.size(), -1);
    for (std::size_t i = 0; i < fighter.def.impacts.size(); ++i)
        for (std::size_t k = 0; k < look.impacts.size(); ++k)
            if (look.impacts[k].name == fighter.def.impacts[i]) look.impact_of[i] = static_cast<std::int32_t>(k);
}

const Impact& impact_for(const Look& look, const Event& e) noexcept {
    auto named = [&](std::string_view name) -> const Impact* {
        for (const Impact& i : look.impacts)
            if (i.name == name) return &i;
        return nullptr;
    };
    if (e.kind == Event::Kind::hit || e.kind == Event::Kind::counter || e.kind == Event::Kind::projectile_clash) {
        if (e.impact < look.impact_of.size())
            if (const std::int32_t k = look.impact_of[e.impact]; k >= 0) return look.impacts[static_cast<std::size_t>(k)];
    } else if (e.kind == Event::Kind::block) {
        if (const Impact* i = named("block")) return *i;
    } else if (e.kind == Event::Kind::ko) {
        if (const Impact* i = named("ko")) return *i;
    } else if (e.kind == Event::Kind::throw_ || e.kind == Event::Kind::tech) {
        if (const Impact* i = named("throw")) return *i;
    } else if (e.kind == Event::Kind::clash) {
        if (const Impact* i = named("clash")) return *i;
    } else if (e.kind == Event::Kind::deflect) {
        if (const Impact* i = named("deflect")) return *i;
    } else if (e.kind == Event::Kind::guard_break) {
        if (const Impact* i = named("break")) return *i;
    }
    return default_impact(e.kind);
}

std::size_t reach(const Look& look, const Pose& p, const Pose& rest, Box base, float scale, float margin, std::array<Box, 4>& out) {
    out[0] = base;
    // Each part's limb: the nearest bone up its chain that starts one (−1: the body).
    auto limb_of = [&](std::int32_t bone) {
        for (std::int32_t b = bone; b >= 0; b = look.bones[static_cast<std::size_t>(b)].parent)
            if (look.bones[static_cast<std::size_t>(b)].limb) return b;
        return std::int32_t{-1};
    };
    // A prop held (a brush) is not the body: a hit on it hits nothing.
    auto held = [&](std::int32_t bone) {
        for (std::int32_t b = bone; b >= 0; b = look.bones[static_cast<std::size_t>(b)].parent)
            if (look.bones[static_cast<std::size_t>(b)].prop) return true;
        return false;
    };
    struct Bounds {
        std::int32_t limb = -1;
        double       left = 0, bottom = 0, right = 0, top = 0;
        void add(double l, double lo, double r, double hi) { left = std::min(left, l), bottom = std::min(bottom, lo), right = std::max(right, r), top = std::max(top, hi); }
    };
    // The bounds of each limb's parts at a pose.
    auto limbs_at = [&](const Pose& at, std::array<Bounds, 8>& limbs) {
        std::size_t n = 0;
        for (const Part& part : look.parts) {
            if (part.bone < 0 || static_cast<std::size_t>(part.bone) >= at.size()) continue;
            const std::int32_t limb = limb_of(part.bone);
            if (limb < 0 || held(part.bone)) continue;
            const BonePlace& b = at[static_cast<std::size_t>(part.bone)];
            double l = std::min(b.joint.x, b.tip.x), r = std::max(b.joint.x, b.tip.x), lo = std::min(b.joint.y, b.tip.y), hi = std::max(b.joint.y, b.tip.y);
            if (part.shape == Shape::polygon) {
                const double ca = dm::cos(b.angle), sa = dm::sin(b.angle);
                for (const Point& q : part.points) {
                    const double x = b.joint.x + q.x * ca - q.y * sa, y = b.joint.y + q.x * sa + q.y * ca;
                    l = std::min(l, x), r = std::max(r, x), lo = std::min(lo, y), hi = std::max(hi, y);
                }
            } else {
                const double t = std::max(part.start, part.end) * scale;
                l -= t, r += t, lo -= t, hi += t;
            }
            auto it = std::ranges::find(limbs.begin(), limbs.begin() + static_cast<std::ptrdiff_t>(n), limb, &Bounds::limb);
            if (it != limbs.begin() + static_cast<std::ptrdiff_t>(n)) it->add(l, lo, r, hi);
            else if (n < limbs.size()) limbs[n++] = {limb, l, lo, r, hi};
        }
        return n;
    };
    std::array<Bounds, 8> now{}, before{};
    const std::size_t n = limbs_at(p, now), m = limbs_at(rest, before);
    // How far a limb reaches out past the box and past where it lies at rest, in any direction.
    const double bl = base.x.to_double(), bb = base.y.to_double(), br = bl + base.w.to_double(), bt = bb + base.h.to_double();
    auto outside = [&](const Bounds& r) {
        Bounds allowed{r.limb, bl, bb, br, bt};
        for (std::size_t k = 0; k < m; ++k)
            if (before[k].limb == r.limb) allowed.add(before[k].left, before[k].bottom, before[k].right, before[k].top);
        return std::max({allowed.left - r.left, r.right - allowed.right, allowed.bottom - r.bottom, r.top - allowed.top, 0.0});
    };
    std::sort(now.begin(), now.begin() + static_cast<std::ptrdiff_t>(n), [&](const Bounds& a, const Bounds& b) {
        return outside(a) != outside(b) ? outside(a) > outside(b) : a.limb < b.limb;
    });
    std::size_t count = 1;
    const auto fx = [](double v) { return Fixed::raw(static_cast<std::int32_t>(std::llround(v * Fixed::one_bits))); };
    for (std::size_t k = 0; k < n && count < out.size(); ++k) {
        if (outside(now[k]) < std::max(margin, 1.0f)) break;  // within: nothing to add
        out[count++] = {fx(now[k].left), fx(now[k].bottom), fx(now[k].right - now[k].left), fx(now[k].top - now[k].bottom)};
    }
    return count;
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

Point on_picture(const Scene& scene, const Camera& cam, double x, double y) noexcept {
    return {static_cast<float>(scene.width / 2 + (x - cam.x) * cam.zoom), static_cast<float>(scene.ground - (y - cam.y) * cam.zoom)};
}

Camera view_camera(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene) {
    Camera cam = frame_camera(match, scene);
    const std::int32_t frame = match.state().frame;
    // The latest contact still shaking, and the latest still punching in.
    bool shaken = false, punched = false;
    for (auto it = effects.kept().rbegin(); it != effects.kept().rend() && !(shaken && punched); ++it) {
        const std::int32_t age = frame - it->frame;
        if (age < 0 || !contact(it->event.kind)) continue;
        if (age >= 60) break;
        const Impact& im = impact_for(look_of(it->event, left, right), it->event);
        const double boost = it->event.kind == Event::Kind::counter ? 1.25 : 1.0;
        if (!shaken && age < im.shake_frames) {
            shaken = true;
            const double amp = im.shake * boost * falloff(age, im.shake_frames);
            cam.x -= amp * dm::sin(age * 2.3 + it->event.impact) / cam.zoom;
            cam.y += amp * 0.5 * dm::cos(age * 1.7 + it->event.impact) / cam.zoom;
        }
        if (!punched && age < im.zoom_frames) {
            punched = true;
            cam.zoom *= 1 + im.zoom * boost * falloff(age, im.zoom_frames);
        }
    }
    return cam;
}

void draw(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene, render::DisplayList& out) {
    Painter{}.draw(match, left, right, effects, scene, out);
}

namespace {

// One contact's spark at its age, drawn with `path_` (a reused path).
void spark(const Effects::Kept& k, const Impact& im, std::int32_t age, const View& view, render::Path& path_, render::DisplayList& out) {
    const Event& e = k.event;
    const Point c = view.at(stage(e.at.x), stage(e.at.y));
    const double t = static_cast<double>(age) / im.spark_frames;  // 0 → 1 over its life
    const double grow = 1 - (1 - t) * (1 - t);                     // ease out
    const double alpha = 1 - t;
    const Color colour = fade(e.kind == Event::Kind::counter ? im.counter_color : im.spark_color, alpha);
    const double reach = im.spark_size * (0.35 + 0.65 * grow);     // already visible on the frame it lands
    const auto n = static_cast<std::uint32_t>(std::max(1, im.spark_count));
    auto from = [&](double angle, double r) {
        return Point{c.x + view.size(r) * static_cast<float>(dm::cos(angle)), c.y + view.size(r) * static_cast<float>(dm::sin(angle))};
    };
    switch (im.spark) {
        case Spark::rays:
            for (std::uint32_t r = 0; r < n; ++r) {
                const double a = dm::two_pi * r / n + e.impact * 0.37;
                path_.clear();
                path_.move_to(from(a, reach * 0.35)).line_to(from(a, reach));
                out.stroke_path(path_, {.width = view.size(4 * (1 - t * 0.8)), .cap = render::LineCap::round}, render::Paint::solid(colour));
            }
            break;
        case Spark::ring:
            path_.clear();
            circle(path_, c, view.size(reach * 0.55));
            out.stroke_path(path_, {.width = view.size(3)}, render::Paint::solid(colour));
            break;
        case Spark::splash:
            // Ink: a blot, and drops thrown out at scattered angles, shrinking as they fly.
            path_.clear();
            circle(path_, c, view.size(im.spark_size * 0.22 * (1 - 0.5 * t)));
            out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(colour));
            for (std::uint32_t d = 0; d < n; ++d) {
                const double a = dm::two_pi * scatter(k, 2 * d), far = reach * (0.5 + 0.5 * scatter(k, 2 * d + 1));
                path_.clear();
                circle(path_, from(a, far), view.size((2 + 4 * scatter(k, 2 * d + 1)) * (1 - 0.7 * t)));
                out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(colour));
            }
            break;
        case Spark::shards:
            // Glass: thin triangles flying out, turning.
            for (std::uint32_t d = 0; d < n; ++d) {
                const double a = dm::two_pi * (d + scatter(k, d)) / n, spin = (scatter(k, d + 97) - 0.5) * 6 * t;
                const Point p = from(a, reach);
                const double len = im.spark_size * 0.3 * (1 - 0.5 * t);
                path_.clear();
                path_.move_to({p.x + view.size(len) * static_cast<float>(dm::cos(a + spin)), p.y + view.size(len) * static_cast<float>(dm::sin(a + spin))});
                path_.line_to({p.x + view.size(len * 0.25) * static_cast<float>(dm::cos(a + spin + 2.2)),
                               p.y + view.size(len * 0.25) * static_cast<float>(dm::sin(a + spin + 2.2))});
                path_.line_to({p.x + view.size(len * 0.25) * static_cast<float>(dm::cos(a + spin - 2.2)),
                               p.y + view.size(len * 0.25) * static_cast<float>(dm::sin(a + spin - 2.2))});
                path_.close();
                out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(colour));
            }
            break;
        case Spark::bolt:
            // Electric: jagged lines out from the contact, re-jagged every other frame.
            for (std::uint32_t d = 0; d < n; ++d) {
                const double a = dm::two_pi * d / n + scatter(k, d) * 0.5;
                path_.clear();
                path_.move_to(c);
                for (int seg = 1; seg <= 4; ++seg) {
                    const double jag = (scatter(k, d * 8 + static_cast<std::uint32_t>(seg) + static_cast<std::uint32_t>(age / 2) * 64) - 0.5) * 0.9;
                    path_.line_to(from(a + jag, reach * seg / 4.0));
                }
                out.stroke_path(path_, {.width = view.size(2.5), .join = render::LineJoin::miter}, render::Paint::solid(colour));
            }
            break;
    }
}

}  // namespace

void Painter::body(const Look& look, const Fighter& fighter, const FighterState& s, std::int32_t frame, const Scene& scene, const Camera& camera,
                   render::DisplayList& out) {
    const View view{scene, camera};
    // Outlines behind, parts back to front.
    pose(look, fighter, s, frame, pose_);
    const Pose& p = pose_;
    order_.resize(look.parts.size());
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    // Insertion sort by depth: stable, in place (std::stable_sort may take a buffer).
    for (std::size_t a = 1; a < order_.size(); ++a)
        for (std::size_t b = a; b > 0 && look.parts[order_[b - 1]].depth > look.parts[order_[b]].depth; --b) std::swap(order_[b - 1], order_[b]);
    for (const bool outline : {true, false}) {
        for (std::size_t k : order_) {
            const Part& part = look.parts[k];
            if (part.bone < 0 || static_cast<std::size_t>(part.bone) >= p.size()) continue;
            const BonePlace& b = p[static_cast<std::size_t>(part.bone)];
            const float grow = outline ? look.outline_width : 0;
            render::Path& path = path_;
            path.clear();
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

void Painter::draw(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene, render::DisplayList& out) {
    const State& st = match.state();
    const View view{scene, view_camera(match, left, right, effects, scene)};

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

    // The fighters.
    for (std::int32_t i = 0; i < 2; ++i)
        body(i == 0 ? left : right, match.fighter(i), st.f[static_cast<std::size_t>(i)], st.frame, scene, view.cam, out);

    // Projectiles: a core and a halo.
    for (const ProjectileState& pr : st.p) {
        if (!pr.alive) continue;
        const Point c = view.at(stage(pr.pos.x), stage(pr.pos.y));
        path_.clear();
        circle(path_, c, view.size(26));
        out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(Color::hex(0x6fd3ff, 0.35f)));
        path_.clear();
        circle(path_, c, view.size(14));
        out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(Color::hex(0xe9fbff)));
    }

    // Sparks and flashes, each contact as its impact profile says.
    for (const Effects::Kept& k : effects.kept()) {
        const std::int32_t age = st.frame - k.frame;
        const Event& e = k.event;
        if (age < 0 || !contact(e.kind)) continue;
        const Impact& im = impact_for(look_of(e, left, right), e);
        if (age < im.spark_frames) spark(k, im, age, view, path_, out);
        if (im.flash != Flash::none && age < im.flash_frames) {
            if (im.flash == Flash::white) {
                const float a = static_cast<float>(0.65 * (1 - static_cast<double>(age) / im.flash_frames));
                out.fill_rect({0, 0, scene.width, scene.height}, Color::hex(0xffffff, a));
            } else {
                // The negative, whole, for its frames (a fading inversion is only grey): difference with white.
                out.push_layer(1, render::BlendMode::difference);
                out.fill_rect({0, 0, scene.width, scene.height}, Color::white());
                out.pop_layer();
            }
        }
    }
}

}  // namespace gongfu::present2d
