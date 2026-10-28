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

constexpr std::array<std::string_view, mode_count> mode_clips{"idle",      "walk_forward", "walk_back", "crouch", "prejump", "air",   "landing",
                                                              "move",      "hitstun",      "blockstun", "knockdown", "wakeup", "thrown", "ko",
                                                              "reset",     "clash",        "broken",    "cinematic"};
static_assert(std::ranges::none_of(mode_clips, &std::string_view::empty), "a clip name for every mode");

double stage(Fixed f) { return f.to_double(); }

void pose_at(const Look& look, const Clip* clip, std::int32_t t, const FighterState& s, Pose& out, std::span<const double> extra = {});
std::pair<const Clip*, std::int32_t> resolve(const Look& look, const Fighter& fighter, const FighterState& s, std::int32_t frame);

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
           k == Event::Kind::clash || k == Event::Kind::deflect || k == Event::Kind::guard_break || k == Event::Kind::piece_hit ||
           k == Event::Kind::shatter || k == Event::Kind::reflect;
}

// Whose look a contact is drawn by: the attacker's; a pane struck, its owner's (the glass).
const Look& look_of(const Event& e, const Look& left, const Look& right) noexcept {
    const std::int32_t by = e.kind == Event::Kind::piece_hit ? 1 - e.attacker : e.attacker;
    return by == 1 ? right : left;
}

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
    const auto [clip, t] = resolve(look, fighter, s, frame);
    pose_at(look, clip, t, s, out);
}

namespace {

// The clip a fighter plays, and where in it.
std::pair<const Clip*, std::int32_t> resolve(const Look& look, const Fighter& fighter, const FighterState& s, std::int32_t frame) {
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
    }
    return {clip, t};
}

}  // namespace

void pose_clip(const Look& look, std::string_view name, std::int32_t t, const FighterState& s, Pose& out) {
    auto it = look.clips.find(name);
    if (it == look.clips.end()) it = look.clips.find(std::string_view("idle"));
    pose_at(look, it == look.clips.end() ? nullptr : &it->second, t, s, out);
}

namespace {

void pose_at(const Look& look, const Clip* clip, std::int32_t t, const FighterState& s, Pose& out, std::span<const double> extra) {
    if (clip) {
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
        if (b < extra.size()) local += extra[b];
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
            p.angle = bone.hang ? (facing > 0 ? local : dm::pi - local) : q.angle + facing * local;
        }
        p.tip = {static_cast<float>(p.joint.x + bone.length * dm::cos(p.angle)), static_cast<float>(p.joint.y + bone.length * dm::sin(p.angle))};
    }
}

}  // namespace

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
    // Set pieces (G-15): a pane struck, shattering, turning a projectile back.
    static const Impact pane{.name = "pane", .shake = 1, .shake_frames = 4, .spark = Spark::shards, .spark_size = 30, .spark_count = 5, .spark_frames = 12,
                             .spark_color = Color::hex(0xe8f6ff), .counter_color = Color::hex(0xe8f6ff)};
    static const Impact shatter{.name = "shatter", .shake = 3, .shake_frames = 8, .spark = Spark::shards, .spark_size = 70, .spark_count = 12,
                                .spark_frames = 20, .spark_color = Color::hex(0xe8f6ff), .counter_color = Color::hex(0xe8f6ff)};
    static const Impact reflect{.name = "reflect", .shake = 2, .shake_frames = 5, .spark = Spark::ring, .spark_size = 50, .spark_count = 1,
                                .spark_frames = 12, .spark_color = Color::hex(0xffd36b), .counter_color = Color::hex(0xffd36b)};
    switch (kind) {
        case Event::Kind::piece_hit: return pane;
        case Event::Kind::shatter: return shatter;
        case Event::Kind::reflect: return reflect;
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
    } else if (e.kind == Event::Kind::piece_hit) {
        if (const Impact* i = named("pane")) return *i;
    } else if (e.kind == Event::Kind::shatter) {
        if (const Impact* i = named("shatter")) return *i;
    } else if (e.kind == Event::Kind::reflect) {
        if (const Impact* i = named("reflect")) return *i;
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
    // A prop held (a brush) is not the body, nor is hanging cloth (a sleeve, a sash): a hit on it hits nothing.
    auto held = [&](std::int32_t bone) {
        for (std::int32_t b = bone; b >= 0; b = look.bones[static_cast<std::size_t>(b)].parent)
            if (look.bones[static_cast<std::size_t>(b)].prop || look.bones[static_cast<std::size_t>(b)].hang) return true;
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
            } else if (part.shape == Shape::image) {
                // Drawn art: as thick as its region reaches across the bone, at the bone's scale.
                double ix = part.tip.x - part.anchor.x, iy = -(part.tip.y - part.anchor.y);
                const double len = dm::sqrt(ix * ix + iy * iy);
                const double art = len > 0 ? look.bones[static_cast<std::size_t>(part.bone)].length / len : 1.0;
                if (len <= 0) ix = 1, iy = 0;
                const double c0 = len > 0 ? ix / len : 1.0, s0 = len > 0 ? iy / len : 0.0;
                double across = 0;
                for (const auto& [cx, cy] : {std::pair{part.source.x, part.source.y}, std::pair{part.source.x + part.source.width, part.source.y},
                                             std::pair{part.source.x, part.source.y + part.source.height},
                                             std::pair{part.source.x + part.source.width, part.source.y + part.source.height}}) {
                    const double dx = cx - part.anchor.x, dy = -(cy - part.anchor.y);
                    across = std::max(across, std::abs(-dx * s0 + dy * c0));
                }
                const double t = across * art * scale;
                l -= t, r += t, lo -= t, hi += t;
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

const FighterState* Effects::past(std::int32_t fighter, std::int32_t frame) const noexcept {
    for (const Past& p : past_)
        if (p.frame == frame) return &p.f[static_cast<std::size_t>(fighter)];
    return nullptr;
}

void Effects::record(const Match& match) {
    Past& slot = past_[static_cast<std::size_t>(match.state().frame) % past_.size()];
    slot.frame = match.state().frame;
    slot.f = match.state().f;
    for (const Event& e : match.events()) kept_.push_back({match.state().frame, e});
    // A few seconds is all a picture reads.
    const std::int32_t keep_from = match.state().frame - 240;  // a cinematic's sparks may stand still a while
    std::erase_if(kept_, [&](const Kept& k) { return k.frame < keep_from; });
}

void Effects::rewind(std::int32_t frame) {
    std::erase_if(kept_, [&](const Kept& k) { return k.frame >= frame; });
    for (Past& p : past_)
        if (p.frame >= frame) p.frame = -1;
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
    // A heavy fighter's footfalls: walking, a dip on each step, back over its frames.
    for (std::int32_t i = 0; i < 2; ++i) {
        const Look& look = i == 0 ? left : right;
        const Mode mode = match.state().f[static_cast<std::size_t>(i)].mode;
        if (look.footfall.dip <= 0 || (mode != Mode::walk_forward && mode != Mode::walk_back)) continue;
        cam.y -= look.footfall.dip * falloff(frame % look.footfall.every, look.footfall.frames) / cam.zoom;
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
        case Spark::cracks: {
            // The ground cracking: jagged lines out along the floor from below the contact, either way,
            // opening at once and staying a while before they fade.
            const Point g = view.at(stage(e.at.x), 0);
            const Color dark = fade(e.kind == Event::Kind::counter ? im.counter_color : im.spark_color, 1 - t * t * t);
            for (std::uint32_t d = 0; d < n; ++d) {
                const double side = d % 2 == 0 ? 1.0 : -1.0, slope = 0.05 + 0.5 * scatter(k, d);  // into the floor, a little
                const double length = im.spark_size * (0.6 + 0.4 * scatter(k, d + 31)) * std::min(1.0, 4 * t + 0.5);
                path_.clear();
                path_.move_to(g);
                for (int seg = 1; seg <= 5; ++seg) {
                    const double along = length * seg / 5, jag = (scatter(k, d * 8 + static_cast<std::uint32_t>(seg)) - 0.5) * im.spark_size * 0.12;
                    path_.line_to({g.x + view.size(side * along), g.y + view.size(std::max(0.0, along * slope + jag))});
                }
                out.stroke_path(path_, {.width = view.size(3.5 * (1 - 0.5 * t)), .cap = render::LineCap::round, .join = render::LineJoin::round}, render::Paint::solid(dark));
            }
            break;
        }
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

namespace {

// Smooth 0 → 1 between two frames.
double between(std::int32_t frame, std::int32_t from, std::int32_t to) {
    if (to <= from) return frame >= to ? 1.0 : 0.0;
    const double u = std::clamp(static_cast<double>(frame - from) / (to - from), 0.0, 1.0);
    return u * u * (3 - 2 * u);
}

// The cinematic a lock plays, if the attacker's look has one for its move.
const CinematicLook* cinematic_of(const Match& match, const Look& left, const Look& right) {
    const Lock& l = match.state().lock;
    if (l.owner < 0) return nullptr;
    const Look& look = l.owner == 0 ? left : right;
    const auto& name = match.fighter(l.owner).def.moves[static_cast<std::size_t>(l.move)].name;
    auto it = look.cinematics.find(name);
    return it == look.cinematics.end() ? nullptr : &it->second;
}

// A look's projectile glyph for a move, by its name or its general name (236P for 236LP).
const Look::GlyphProjectile* projectile_of(const Look& look, std::string_view name) {
    if (auto it = look.projectiles.find(name); it != look.projectiles.end()) return &it->second;
    if (name.size() < 2) return nullptr;
    const std::string_view tail = name.substr(name.size() - 2);
    const char general = tail == "LP" || tail == "MP" || tail == "HP" ? 'P' : tail == "LK" || tail == "MK" || tail == "HK" ? 'K' : '\0';
    if (!general) return nullptr;
    std::array<char, 48> buffer{};
    const std::size_t n = std::min(name.size() - 2, buffer.size() - 1);
    std::copy_n(name.begin(), n, buffer.begin());
    buffer[n] = general;
    auto it = look.projectiles.find(std::string_view(buffer.data(), n + 1));
    return it == look.projectiles.end() ? nullptr : &it->second;
}

// A path's offset at a frame, between its keys (none: no offset).
Point path_at(const std::vector<CinematicLook::PathKey>& keys, std::int32_t t) {
    if (keys.empty()) return {};
    std::size_t k = 0;
    while (k + 1 < keys.size() && keys[k + 1].frame <= t) ++k;
    const auto& a = keys[k];
    const auto& b = k + 1 < keys.size() ? keys[k + 1] : keys[k];
    const double u = between(t, a.frame, b.frame);
    return {static_cast<float>(a.at.x + (b.at.x - a.at.x) * u), static_cast<float>(a.at.y + (b.at.y - a.at.y) * u)};
}

// The fighter in a super's flash, and how far into it (0 → 1); −1 when none.
std::pair<std::int32_t, double> flashing(const Match& match) {
    const State& st = match.state();
    if (st.freeze <= 0) return {-1, 0};
    for (std::int32_t i = 0; i < 2; ++i) {
        const FighterState& f = st.f[static_cast<std::size_t>(i)];
        if (f.mode != Mode::move || f.move_frame > 1) continue;  // the flash stands on the move's first frame
        const MoveDef& m = match.fighter(i).def.moves[static_cast<std::size_t>(f.move)];
        if (m.level <= 0 || m.freeze <= 0) continue;
        return {i, 1.0 - static_cast<double>(st.freeze) / m.freeze};
    }
    return {-1, 0};
}

}  // namespace

namespace {

Color mix(Color a, Color b, float t) {
    return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
}

// A closed curve through `points` (Catmull-Rom: through each point, smooth at each), as cubics.
template <class At>
void curve(render::Path& path, std::span<const Point> points, At at) {
    const std::size_t n = points.size();
    if (n < 3) return;
    auto p = [&](std::size_t i) { return at(points[i % n]); };
    path.move_to(p(0));
    for (std::size_t i = 0; i < n; ++i) {
        const Point p0 = p(i + n - 1), p1 = p(i), p2 = p(i + 1), p3 = p(i + 2);
        path.cubic_to({p1.x + (p2.x - p0.x) / 6, p1.y + (p2.y - p0.y) / 6}, {p2.x - (p3.x - p1.x) / 6, p2.y - (p3.y - p1.y) / 6}, p2);
    }
    path.close();
}

}  // namespace

void Painter::body(const Look& look, const Fighter& fighter, const FighterState& s, std::int32_t frame, const Scene& scene, const Camera& camera,
                   render::DisplayList& out, std::string_view clip_name_, std::int32_t clip_t, std::optional<Color> tint) {
    const View view{scene, camera};
    // The clip and its time; then each following bone's lag (secondary motion), from the same clip a few frames before.
    const Clip* clip = nullptr;
    std::int32_t t = 0;
    if (clip_name_.empty()) {
        std::tie(clip, t) = resolve(look, fighter, s, frame);
    } else {
        auto it = look.clips.find(clip_name_);
        if (it == look.clips.end()) it = look.clips.find(std::string_view("idle"));
        clip = it == look.clips.end() ? nullptr : &it->second;
        t = clip_t;
    }
    follow_.assign(look.bones.size(), 0.0);
    bool follows = false;
    for (std::size_t b = 0; b < look.bones.size(); ++b)
        follows = follows || (look.bones[b].follow.lag > 0 && (look.bones[b].follow.amount != 0 || look.bones[b].follow.drag != 0));
    if (follows) {
        pose_at(look, clip, t, s, pose_);
        const double facing = s.facing < 0 ? -1.0 : 1.0;
        for (std::size_t b = 0; b < look.bones.size(); ++b) {
            const Bone::Follow& f = look.bones[b].follow;
            if (f.lag <= 0 || (f.amount == 0 && f.drag == 0)) continue;
            pose_at(look, clip, std::max(0, t - f.lag), s, past_);
            double swung = (pose_[b].angle - past_[b].angle) * 180 / dm::pi;
            while (swung > 180) swung -= 360;
            while (swung < -180) swung += 360;
            // Where its joint went over the lag: the clip's motion and the body's own travel.
            const double travel = (pose_[b].joint.x - past_[b].joint.x) + s.vel.x.to_double() * f.lag;
            follow_[b] = std::clamp(-f.amount * swung - f.drag * travel, -static_cast<double>(f.max), static_cast<double>(f.max)) * facing;
        }
    }
    pose_at(look, clip, t, s, pose_, follow_);
    const Pose& p = pose_;
    const hanim::seconds at{(clip && clip->loop && clip->frames > 0 ? t % clip->frames : clip ? std::min(t, clip->frames) : t) / 60.0};
    order_.resize(look.parts.size());
    std::iota(order_.begin(), order_.end(), std::size_t{0});
    // Insertion sort by depth: stable, in place (std::stable_sort may take a buffer).
    for (std::size_t a = 1; a < order_.size(); ++a)
        for (std::size_t b = a; b > 0 && look.parts[order_[b - 1]].depth > look.parts[order_[b]].depth; --b) std::swap(order_[b - 1], order_[b]);
    // The light's offsets on the picture: a shadow's (away from the light) and a rim's (toward it).
    // The stage's light, where it has one (the world's rules: both fighters lit alike); the look keeps its depths.
    const Stage* world = scene.stage;
    const float lx = world && world->light_from ? world->light_from->x : look.light.from.x;
    const float ly = world && world->light_from ? world->light_from->y : look.light.from.y;
    const Color shadow_colour = world && world->shadow ? *world->shadow : look.light.shadow;
    const Color rim_colour = world && world->rim_color ? *world->rim_color : look.light.rim_color;
    const float lit = view.size(1);
    const bool shading = look.light.shade > 0 && !tint;
    // A glow (neon) behind the outline, then the outline, then the parts; a tinted afterimage is the parts alone, in one colour.
    enum class Pass : std::uint8_t { glow, outline, fill };
    for (const Pass pass : {Pass::glow, Pass::outline, Pass::fill}) {
        if (pass == Pass::glow && (look.glow.width <= 0 || tint)) continue;
        if (pass == Pass::outline && tint) continue;
        const bool outline = pass != Pass::fill;
        for (std::size_t k : order_) {
            const Part& part = look.parts[k];
            if (part.bone < 0 || static_cast<std::size_t>(part.bone) >= p.size()) continue;
            if (part.shape == Shape::image) {  // drawn art: its own outline and light, no pass but the fill
                if (pass == Pass::fill) picture_part(look, part, s, scene, camera, tint, out);
                continue;
            }
            const BonePlace& b = p[static_cast<std::size_t>(part.bone)];
            const float grow = pass == Pass::glow ? look.outline_width + look.glow.width : outline ? look.outline_width : 0;
            render::Path& path = path_;
            path.clear();
            const Point j = view.at(b.joint.x, b.joint.y), tip = view.at(b.tip.x, b.tip.y);
            // The drawing a clip's swap chooses (0: its points).
            const std::vector<Point>* points = &part.points;
            if (clip && k < clip->swaps.size() && !clip->swaps[k].empty()) {
                const auto v = static_cast<std::size_t>(std::max(0.0, std::floor(clip->swaps[k].value(at) + 0.5)));
                if (v > 0 && v <= part.variants.size()) points = &part.variants[v - 1];
            }
            const double ca = dm::cos(b.angle), sa = dm::sin(b.angle), facing = s.facing < 0 ? -1.0 : 1.0;
            auto on_bone = [&](Point q) {
                const double px = q.x, py = q.y * facing;
                return view.at(b.joint.x + px * ca - py * sa, b.joint.y + px * sa + py * ca);
            };
            switch (part.shape) {
                case Shape::capsule: capsule(path, j, tip, view.size(part.start) + grow, view.size(part.end) + grow); break;
                case Shape::ellipse:
                    ellipse(path, {(j.x + tip.x) / 2, (j.y + tip.y) / 2}, -b.angle, view.size(part.start) + grow, view.size(part.end) + grow);
                    break;
                case Shape::polygon:
                    for (std::size_t q = 0; q < points->size(); ++q) {
                        if (q == 0) path.move_to(on_bone((*points)[q]));
                        else path.line_to(on_bone((*points)[q]));
                    }
                    path.close();
                    break;
                case Shape::curve: curve(path, *points, on_bone); break;
                case Shape::image: break;
            }
            const Color colour = tint ? *tint : pass == Pass::glow ? fade(look.glow.color, 0.35) : outline ? look.outline : part.color;
            const bool drawn = part.shape == Shape::polygon || part.shape == Shape::curve;
            if (outline && drawn) {
                out.stroke_path(path, {.width = 2 * grow, .join = render::LineJoin::round}, render::Paint::solid(colour));
            } else if (outline || !shading || !part.shade) {
                out.fill_path(path, render::FillRule::nonzero, render::Paint::solid(colour));
            } else {
                // Lit: inside the part, its shadow; the rim (toward the light, the shadow's depth back); and its colour
                // where neither reaches — the part moved toward the light, and moved away by the rim's width.
                const float d = look.light.shade * lit, r = look.light.rim * lit;
                // (Moved by transforms, so a frame allocates no path.)
                out.push_clip(path, render::FillRule::nonzero);
                out.fill_path(path, render::FillRule::nonzero, render::Paint::solid(mix(colour, shadow_colour, look.light.darken)));
                out.push_transform(Affine::translate(lx * d, ly * d));
                out.fill_path(path, render::FillRule::nonzero, render::Paint::solid(r > 0 ? mix(colour, rim_colour, rim_colour.a) : colour));
                out.pop_transform();
                if (r > 0) {
                    out.push_transform(Affine::translate(-lx * r, -ly * r));
                    out.push_clip(path, render::FillRule::nonzero);
                    out.push_transform(Affine::translate(lx * (d + r), ly * (d + r)));
                    out.fill_path(path, render::FillRule::nonzero, render::Paint::solid(colour));
                    out.pop_transform();
                    out.pop_clip();
                    out.pop_transform();
                }
                out.pop_clip();
            }
        }
    }
}

void Painter::picture_part(const Look& look, const Part& part, const FighterState& s, const Scene& scene, const Camera& camera,
                           std::optional<Color> tint, render::DisplayList& out) {
    if (part.picture < 0 || static_cast<std::size_t>(part.picture) >= look.pictures.size()) return;
    const Picture& pic = look.pictures[static_cast<std::size_t>(part.picture)];
    if (pic.ref.id == 0 || part.source.width <= 0 || part.source.height <= 0) return;
    const View view{scene, camera};
    const double facing = s.facing < 0 ? -1.0 : 1.0;
    pose_at(look, nullptr, 0, s, rest_);  // where the art was drawn: the bones at rest, where the fighter stands
    const auto b = static_cast<std::size_t>(part.bone);
    // The art's own turn and size: anchor to tip along the bone (the picture's y runs down).
    double ix = part.tip.x - part.anchor.x, iy = -(part.tip.y - part.anchor.y);
    double len = dm::sqrt(ix * ix + iy * iy);
    const double scale = len > 0 ? look.bones[b].length / len : 1.0;
    if (len <= 0) ix = 1, iy = 0, len = 1;
    const double c0 = ix / len, s0 = iy / len;
    auto place = [&](const BonePlace& bone, double along, double across) {
        const double ca = dm::cos(bone.angle), sa = dm::sin(bone.angle);
        return Point{static_cast<float>(bone.joint.x + along * ca - across * facing * sa), static_cast<float>(bone.joint.y + along * sa + across * facing * ca)};
    };
    const std::int32_t cols = std::max(1, part.mesh[0]), rows = std::max(1, part.mesh[1]);
    mesh_at_.clear();
    mesh_uv_.clear();
    mesh_index_.clear();
    for (std::int32_t j = 0; j <= rows; ++j)
        for (std::int32_t i = 0; i <= cols; ++i) {
            const double lx = part.source.x + part.source.width * i / cols, ly = part.source.y + part.source.height * j / rows;
            const double dx = lx - part.anchor.x, dy = -(ly - part.anchor.y);
            const double along = (dx * c0 + dy * s0) * scale, across = (-dx * s0 + dy * c0) * scale;
            Point at = place(pose_[b], along, across);
            if (!part.bend.empty()) {
                // Skinning: from the rest pose, each bend bone takes the vertex over across `blend` about its joint.
                const Point rest = place(rest_[b], along, across);
                for (const std::int32_t cb : part.bend) {
                    if (cb < 0 || static_cast<std::size_t>(cb) >= rest_.size()) continue;
                    const BonePlace& rc = rest_[static_cast<std::size_t>(cb)];
                    const double ca = dm::cos(rc.angle), sa = dm::sin(rc.angle);
                    const double rx = rest.x - rc.joint.x, ry = rest.y - rc.joint.y;
                    const double along_c = rx * ca + ry * sa, across_c = (-rx * sa + ry * ca) * facing;
                    const double w = std::clamp(0.5 + along_c / std::max(1.0, part.blend * scale), 0.0, 1.0);
                    const Point moved = place(pose_[static_cast<std::size_t>(cb)], along_c, across_c);
                    at = {static_cast<float>(at.x + (moved.x - at.x) * w), static_cast<float>(at.y + (moved.y - at.y) * w)};
                }
            }
            mesh_at_.push_back(view.at(at.x, at.y));
            mesh_uv_.push_back({static_cast<float>(lx), static_cast<float>(ly)});
        }
    for (std::int32_t j = 0; j < rows; ++j)
        for (std::int32_t i = 0; i < cols; ++i) {
            const auto k = static_cast<std::uint16_t>(j * (cols + 1) + i), w = static_cast<std::uint16_t>(cols + 1);
            for (const std::uint16_t v : {k, static_cast<std::uint16_t>(k + 1), static_cast<std::uint16_t>(k + w + 1), k, static_cast<std::uint16_t>(k + w + 1),
                                          static_cast<std::uint16_t>(k + w)})
                mesh_index_.push_back(v);
        }
    out.draw_mesh(pic.ref, mesh_at_, mesh_uv_, mesh_index_, render::Sampling::linear, tint ? tint->a : 1.0f);
}

void Painter::stage_layers(const Stage& art, bool front, const Scene& scene, const Camera& cam, double middle, std::int32_t frame, float alpha,
                           render::DisplayList& out) {
    const double seconds = frame / 60.0;
    for (const StageLayer& layer : art.layers) {
        if (layer.front != front || layer.shapes.empty()) continue;
        // A layer at parallax p moves p of the camera's way and zooms p of its zoom.
        const double p = layer.parallax, zoom = 1 + (cam.zoom - 1) * p, cx = middle + (cam.x - middle) * p, cy = cam.y * p;
        auto at = [&](double x, double y) {
            return Point{static_cast<float>(scene.width / 2 + (x - cx) * zoom), static_cast<float>(scene.ground - (y - cy) * zoom)};
        };
        if (layer.clip) {
            const Point a = at((*layer.clip)[0], (*layer.clip)[3]), b = at((*layer.clip)[2], (*layer.clip)[1]);
            out.push_clip(Rect{a.x, a.y, b.x - a.x, b.y - a.y});
        }
        std::int64_t first = 0, last = 0;
        if (layer.repeat > 0) {
            const double half = scene.width / 2 / zoom;
            first = static_cast<std::int64_t>(std::floor((cx - half) / layer.repeat)) - 1;
            last = static_cast<std::int64_t>(std::ceil((cx + half) / layer.repeat)) + 1;
        }
        for (std::int64_t k = first; k <= last; ++k) {
            const double ox = static_cast<double>(k) * layer.repeat;
            for (const StageShape& sh : layer.shapes) {
                // Its sway at this frame: a turn about its pivot, a pure function of the frame.
                const double turn = sh.sway > 0 ? sh.sway * dm::pi / 180 * dm::sin(2 * dm::pi * (seconds / sh.period + sh.phase)) : 0.0;
                const double ca = dm::cos(turn), sa = dm::sin(turn);
                // Its drift at this frame, wrapping round (rain falls past the bottom and starts again at the top).
                double mx = sh.drift.x * seconds, my = sh.drift.y * seconds;
                if (sh.wrap > 0 && (sh.drift.x != 0 || sh.drift.y != 0)) {
                    const double len = std::sqrt(static_cast<double>(sh.drift.x) * sh.drift.x + static_cast<double>(sh.drift.y) * sh.drift.y);
                    const double along = std::fmod(len * seconds, static_cast<double>(sh.wrap));
                    mx = sh.drift.x / len * along, my = sh.drift.y / len * along;
                }
                auto placed = [&](Point q) {
                    const double dx = q.x - sh.pivot.x, dy = q.y - sh.pivot.y;
                    return at(ox + mx + sh.pivot.x + dx * ca - dy * sa, my + sh.pivot.y + dx * sa + dy * ca);
                };
                path_.clear();
                float top = std::numeric_limits<float>::max(), bottom = std::numeric_limits<float>::lowest();
                if (sh.shape == Shape::curve) {
                    curve(path_, sh.points, placed);
                } else {
                    for (std::size_t q = 0; q < sh.points.size(); ++q) {
                        const Point pt = placed(sh.points[q]);
                        if (q == 0) path_.move_to(pt);
                        else path_.line_to(pt);
                    }
                    path_.close();
                }
                for (const Point& q : sh.points) {
                    const Point pt = placed(q);
                    top = std::min(top, pt.y), bottom = std::max(bottom, pt.y);
                }
                // A flicker: two slow waves out of step, so it never repeats too plainly.
                double a = layer.opacity * alpha;
                if (sh.flicker > 0) a *= 1 - sh.flicker * (0.5 + 0.25 * dm::sin(seconds * 7.3 + sh.phase * 11) + 0.25 * dm::sin(seconds * 2.9 + sh.phase * 5));
                if (sh.glow > 0) {  // a halo: wide, faint strokes in its colour, behind it
                    for (const float k : {1.0f, 0.5f})
                        out.stroke_path(path_, {.width = static_cast<float>(sh.glow * zoom * k * 2), .join = render::LineJoin::round},
                                        render::Paint::solid(fade(sh.color, a * 0.22 / k)));
                }
                if (sh.color2.a > 0) {
                    const std::array<render::GradientStop, 2> stops{{{0, fade(sh.color, a)}, {1, fade(sh.color2, a)}}};
                    render::Paint paint;
                    paint.kind = render::Paint::Kind::gradient;
                    paint.gradient = {.kind = render::GradientKind::linear, .start = {0, top}, .end = {0, bottom}, .stops = stops};
                    out.fill_path(path_, render::FillRule::nonzero, paint);
                } else {
                    out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(fade(sh.color, a)));
                }
            }
        }
        if (layer.clip) out.pop_clip();
    }
}

void Painter::glyph(const std::vector<Stroke>& strokes, double progress, Point centre, float size, Color color, render::DisplayList& out, float thicken) {
    double total = 0;
    for (const Stroke& st : strokes)
        for (std::size_t k = 1; k < st.points.size(); ++k) total += std::hypot(st.points[k].x - st.points[k - 1].x, st.points[k].y - st.points[k - 1].y);
    double left = std::clamp(progress, 0.0, 1.0) * total;
    std::vector<double> parts;
    for (const Stroke& st : strokes) {
        double len = 0;
        for (std::size_t k = 1; k < st.points.size(); ++k) len += std::hypot(st.points[k].x - st.points[k - 1].x, st.points[k].y - st.points[k - 1].y);
        parts.push_back(len <= 0 ? 1.0 : std::clamp(left / len, 0.0, 1.0));
        left = std::max(0.0, left - len);
    }
    // As glyph_by_frames, each stroke to its fraction.
    const float k = size / 100;
    for (std::size_t n = 0; n < strokes.size(); ++n) {
        const Stroke& st = strokes[n];
        const double upto = parts[n];
        if (upto <= 0 || st.points.size() < 2) continue;
        double len = 0;
        std::vector<double> at{0};
        for (std::size_t q = 1; q < st.points.size(); ++q) at.push_back(len += std::hypot(st.points[q].x - st.points[q - 1].x, st.points[q].y - st.points[q - 1].y));
        // Samples along the drawn part, each with its width (a press, a carry, a lift).
        constexpr int samples = 14;
        std::array<Point, samples + 1> centre_at{};
        std::array<float, samples + 1> width_at{};
        for (int i = 0; i <= samples; ++i) {
            const double u = upto * i / samples, d = u * len;
            std::size_t seg = 1;
            while (seg + 1 < st.points.size() && at[seg] < d) ++seg;
            const double span = at[seg] - at[seg - 1], f = span > 0 ? (d - at[seg - 1]) / span : 0;
            const double x = st.points[seg - 1].x + (st.points[seg].x - st.points[seg - 1].x) * f;
            const double y = st.points[seg - 1].y + (st.points[seg].y - st.points[seg - 1].y) * f;
            centre_at[static_cast<std::size_t>(i)] = {centre.x + static_cast<float>((x - 50) * k), centre.y - static_cast<float>((y - 50) * k)};
            const double c = 2 * st.width[1] - (st.width[0] + st.width[2]) / 2.0;
            width_at[static_cast<std::size_t>(i)] =
                static_cast<float>(((1 - u) * (1 - u) * st.width[0] + 2 * u * (1 - u) * c + u * u * st.width[2]) * k / 2 * thicken);
        }
        for (const float bleed : {1.35f, 1.0f}) {  // the ink bleeding into the paper, then the stroke
            path_.clear();
            for (int i = 0; i <= samples; ++i) {
                const Point a = centre_at[static_cast<std::size_t>(std::max(0, i - 1))], b = centre_at[static_cast<std::size_t>(std::min(samples, i + 1))];
                const float dx = b.x - a.x, dy = b.y - a.y, l = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
                const float w = width_at[static_cast<std::size_t>(i)] * bleed;
                const Point p{centre_at[static_cast<std::size_t>(i)].x - dy / l * w, centre_at[static_cast<std::size_t>(i)].y + dx / l * w};
                if (i == 0) path_.move_to(p);
                else path_.line_to(p);
            }
            for (int i = samples; i >= 0; --i) {
                const Point a = centre_at[static_cast<std::size_t>(std::max(0, i - 1))], b = centre_at[static_cast<std::size_t>(std::min(samples, i + 1))];
                const float dx = b.x - a.x, dy = b.y - a.y, l = std::max(1e-3f, std::sqrt(dx * dx + dy * dy));
                const float w = width_at[static_cast<std::size_t>(i)] * bleed;
                path_.line_to({centre_at[static_cast<std::size_t>(i)].x + dy / l * w, centre_at[static_cast<std::size_t>(i)].y - dx / l * w});
            }
            path_.close();
            circle(path_, centre_at[0], width_at[0] * bleed);
            circle(path_, centre_at[samples], width_at[samples] * bleed);
            Color c = color;
            if (bleed > 1) c.a *= 0.22f;
            out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(c));
        }
    }
}

void Painter::glyph_by_frames(const std::vector<Stroke>& strokes, std::span<const std::int32_t> ends, std::int32_t from, std::int32_t frame, Point centre,
                              float size, Color color, render::DisplayList& out) {
    // Stroke n is written between the previous one's end and ends[n]: as a share of the whole length.
    double total = 0;
    std::vector<double> lens;
    for (const Stroke& st : strokes) {
        double len = 0;
        for (std::size_t k = 1; k < st.points.size(); ++k) len += std::hypot(st.points[k].x - st.points[k - 1].x, st.points[k].y - st.points[k - 1].y);
        lens.push_back(len);
        total += len;
    }
    if (total <= 0) return;
    double written = 0;
    std::int32_t start = from;
    for (std::size_t n = 0; n < strokes.size(); ++n) {
        const std::int32_t end = n < ends.size() ? ends[n] : start + 15;
        const double f = end > start ? std::clamp(static_cast<double>(frame - start) / (end - start), 0.0, 1.0) : (frame >= end ? 1.0 : 0.0);
        written += lens[n] * f;
        if (f < 1) break;
        start = end;
    }
    glyph(strokes, written / total, centre, size, color, out);
}

void Painter::draw(const Match& match, const Look& left, const Look& right, const Effects& effects, const Scene& scene, render::DisplayList& out) {
    const State& st = match.state();
    Camera camera = view_camera(match, left, right, effects, scene);
    const CinematicLook* cine = cinematic_of(match, left, right);
    const auto [flasher, flash_progress] = flashing(match);
    if (cine && !cine->camera.empty()) {
        // The cinematic's camera, between its keys, from where the attacker stood.
        const auto& keys = cine->camera;
        const std::int32_t t = st.lock.frame;
        std::size_t k = 0;
        while (k + 1 < keys.size() && keys[k + 1].frame <= t) ++k;
        const auto& a = keys[k];
        const auto& b = k + 1 < keys.size() ? keys[k + 1] : keys[k];
        const double u = between(t, a.frame, b.frame);
        const double fx = st.lock.facing;
        camera.zoom = a.zoom + (b.zoom - a.zoom) * u;
        camera.x = stage(st.lock.x) + fx * (a.at.x + (b.at.x - a.at.x) * u);
        // The key's point at the picture's middle: the camera's y is the height the ground line shows.
        camera.y = stage(st.lock.y) + (a.at.y + (b.at.y - a.at.y) * u) - (scene.ground - scene.height / 2) / camera.zoom;
        // Quakes: the picture shaken, and swelling for a moment.
        for (const auto& q : cine->quakes) {
            const std::int32_t age = t - q.frame;
            if (age < 0 || age >= q.frames) continue;
            const double fall = 1 - static_cast<double>(age) / q.frames;
            camera.x += q.strength * fall * dm::sin(age * 2.9) / camera.zoom;
            camera.y += q.strength * 0.7 * fall * dm::cos(age * 2.3) / camera.zoom;
            camera.zoom *= 1 + 0.06 * fall * fall;
        }
    } else if (flasher >= 0) {
        // A super's flash: the camera leans in on who flashes.
        const FighterState& f = st.f[static_cast<std::size_t>(flasher)];
        const double e = between(static_cast<std::int32_t>(flash_progress * 100), 0, 30);
        camera.x += (stage(f.pos.x) - camera.x) * 0.5 * e;
        camera.zoom *= 1 + 0.35 * e;
    }
    const View view{scene, camera};

    const std::size_t first = out.size();  // where this picture's commands begin (a stop turns them grey)

    // The stage: sky, floor, the ground line, and marks along it that move with the camera; its art's layers behind the fighters.
    const Stage* art = scene.stage;
    const std::array<render::GradientStop, 2> sky{{{0, art ? art->sky_top : scene.sky_top}, {1, art ? art->sky_bottom : scene.sky_bottom}}};
    render::Paint sky_paint;
    sky_paint.kind = render::Paint::Kind::gradient;
    sky_paint.gradient = {.kind = render::GradientKind::linear, .start = {0, 0}, .end = {0, scene.ground}, .stops = sky};
    out.fill_rect({0, 0, scene.width, scene.ground}, sky_paint);
    const Point ground = view.at(0, 0);
    const Color floor = art ? art->floor : scene.floor, line = art ? art->line : scene.line;
    out.fill_rect({0, ground.y, scene.width, scene.height - ground.y}, floor);
    const double middle = stage(match.system().stage_width) / 2;
    if (art) stage_layers(*art, false, scene, view.cam, middle, st.frame, 1, out);
    out.fill_rect({0, ground.y, scene.width, 2}, line);
    if (!art || art->marks)
        for (int m = 0; m <= static_cast<int>(stage(match.system().stage_width)); m += 160) {
            const Point p = view.at(m, 0);
            if (p.x >= -4 && p.x <= scene.width + 4) out.fill_rect({p.x - 1, p.y, 2, view.size(14)}, fade(line, 0.6));
        }

    // A cinematic's backdrop over the stage, fading in and out.
    if (cine) {
        const std::int32_t t = st.lock.frame;
        const double in = between(t, cine->backdrop_in[0], cine->backdrop_in[1]);
        const double out_ = cine->backdrop_out[1] > cine->backdrop_out[0] ? 1 - between(t, cine->backdrop_out[0], cine->backdrop_out[1]) : 1.0;
        out.fill_rect({0, 0, scene.width, scene.height}, fade(cine->backdrop, in * out_));
        // Its clouds: each three puffs, where a number from its index puts it (the same everywhere).
        auto unit = [](std::uint64_t z) {
            z = (z + 0x9e3779b97f4a7c15ull) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return static_cast<double>((z ^ (z >> 31)) >> 40) / static_cast<double>(1u << 24);
        };
        for (std::int32_t n = 0; n < cine->clouds.count; ++n) {
            const auto k = static_cast<std::uint64_t>(n) * 4;
            const double x = stage(st.lock.x) + (unit(k) * 2 - 1) * 700, y = stage(st.lock.y) + cine->clouds.low + unit(k + 1) * (cine->clouds.high - cine->clouds.low);
            const float w = view.size(static_cast<float>(90 + 110 * unit(k + 2))), h = w * 0.32f;
            const Point c = view.at(x, y);
            const render::Paint paint = render::Paint::solid(fade(cine->clouds.color, (0.55 + 0.35 * unit(k + 3)) * in * out_));
            for (const auto& [dx, dy, s] : {std::tuple{-0.32f, 0.f, 0.7f}, std::tuple{0.f, -0.25f, 1.f}, std::tuple{0.34f, 0.05f, 0.75f}}) {
                const float pw = w * s, ph = h * s * 1.4f;
                out.fill_round_rect({c.x + dx * w - pw / 2, c.y + dy * h - ph / 2, pw, ph}, omui::geom::CornerRadii::all(ph / 2), paint);
            }
        }
    }

    // A clock's face: its hands go round while time runs, and stand while it stands.
    if (cine && cine->clock.from >= 0 && st.lock.frame >= cine->clock.from && st.lock.frame <= cine->clock.to) {
        const auto& ck = cine->clock;
        const double in = std::min({1.0, (st.lock.frame - ck.from) / 8.0, (ck.to - st.lock.frame) / 8.0 + 0.0001});
        const Color colour = fade(ck.color, std::max(0.0, in));
        const Point c = view.at(stage(st.lock.x) + st.lock.facing * ck.at.x, stage(st.lock.y) + ck.at.y);
        const float r = view.size(ck.size / 2);
        path_.clear();
        circle(path_, c, r);
        out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(fade(Color::hex(0x101014), 0.55 * std::max(0.0, in))));
        out.stroke_path(path_, {.width = view.size(4)}, render::Paint::solid(colour));
        for (int h = 0; h < 12; ++h) {
            const double a = dm::two_pi * h / 12;
            const float inner = h % 3 == 0 ? 0.78f : 0.86f;
            path_.clear();
            path_.move_to({c.x + r * inner * static_cast<float>(dm::sin(a)), c.y - r * inner * static_cast<float>(dm::cos(a))})
                .line_to({c.x + r * 0.95f * static_cast<float>(dm::sin(a)), c.y - r * 0.95f * static_cast<float>(dm::cos(a))});
            out.stroke_path(path_, {.width = view.size(h % 3 == 0 ? 4.f : 2.f), .cap = render::LineCap::round}, render::Paint::solid(colour));
        }
        // Time on its face: running, standing over the stop, then running again.
        std::int32_t t = st.lock.frame;
        if (cine->stop.from >= 0 && t >= cine->stop.from) t = cine->stop.from + std::max(0, t - cine->stop.to);
        for (const auto& [turn, length, width] : {std::tuple{t * 6.0, 0.82f, 2.5f}, std::tuple{330 + t * 0.5, 0.6f, 5.f}}) {
            const double a = turn * dm::pi / 180;
            path_.clear();
            path_.move_to(c).line_to({c.x + r * length * static_cast<float>(dm::sin(a)), c.y - r * length * static_cast<float>(dm::cos(a))});
            out.stroke_path(path_, {.width = view.size(width), .cap = render::LineCap::round}, render::Paint::solid(colour));
        }
        path_.clear();
        circle(path_, c, view.size(6));
        out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(colour));
    }

    // Afterimages: during a move, the poses of a few frames before, fading (from the states the Effects keep).
    if (!cine)
        for (std::int32_t i = 0; i < 2; ++i) {
            const Look& look = i == 0 ? left : right;
            const FighterState& f = st.f[static_cast<std::size_t>(i)];
            if (look.trail.frames.empty() || f.mode != Mode::move) continue;
            for (std::size_t n = look.trail.frames.size(); n-- > 0;) {
                const FighterState* then = effects.past(i, st.frame - look.trail.frames[n]);
                if (!then || then->mode != Mode::move) continue;
                const double a = look.trail.alpha * (1 - static_cast<double>(n) / static_cast<double>(look.trail.frames.size()));
                body(look, match.fighter(i), *then, st.frame - look.trail.frames[n], scene, view.cam, out, {}, 0, fade(look.trail.color, a));
            }
        }

    // Clones (G-21), in their side's tint; a rewind's ghosts along the way back (G-20).
    for (std::int32_t i = 0; i < 2; ++i) {
        const Look& look = i == 0 ? left : right;
        const Clone& c = st.clones[static_cast<std::size_t>(i)];
        if (c.alive) body(look, match.fighter(i), c.body, st.frame, scene, view.cam, out, {}, 0, look.clone.tint);
        if (look.rewind.ghosts <= 0) continue;
        for (const Effects::Kept& k : effects.kept()) {
            const std::int32_t age = st.frame - k.frame;
            if (k.event.kind != Event::Kind::rewind || k.event.attacker != i || age < 0 || age >= look.rewind.frames) continue;
            const FighterState& now = st.f[static_cast<std::size_t>(i)];
            for (std::int32_t g = 1; g <= look.rewind.ghosts; ++g) {
                const double u = static_cast<double>(g) / (look.rewind.ghosts + 1);
                FighterState ghost = now;
                ghost.pos.x = k.event.at.x + Fixed::raw(static_cast<std::int32_t>(std::lround((now.pos.x - k.event.at.x).to_double() * u * Fixed::one_bits)));
                ghost.pos.y = Fixed{};
                const double a = (1 - static_cast<double>(age) / look.rewind.frames) * (0.4 + 0.6 * u);
                body(look, match.fighter(i), ghost, st.frame - g * 3, scene, view.cam, out, {}, 0, fade(look.rewind.color, a));
            }
        }
    }

    // The fighters (in a cinematic, each plays the clip it says, at the lock's frame).
    for (std::int32_t i = 0; i < 2; ++i) {
        std::string_view clip;
        FighterState drawn = st.f[static_cast<std::size_t>(i)];
        if (cine) {
            const bool attacker = i == st.lock.owner;
            clip = attacker ? std::string_view(cine->attacker_clip) : std::string_view(cine->defender_clip);
            // A path places the fighter from where the lock began (the attacker's feet), whatever the gap.
            const auto& path = attacker ? cine->attacker_path : cine->defender_path;
            if (!path.empty()) {
                const Point off = path_at(path, st.lock.frame);
                drawn.pos.x = st.lock.x + Fixed::raw(static_cast<std::int32_t>(std::lround(off.x * st.lock.facing * Fixed::one_bits)));
                drawn.pos.y = st.lock.y + Fixed::raw(static_cast<std::int32_t>(std::lround(off.y * Fixed::one_bits)));
            }
        }
        // A prism: the defender again either side, faint, as through cut glass.
        if (cine && i != st.lock.owner && cine->prism.images > 0 && st.lock.frame >= cine->prism.from && st.lock.frame <= cine->prism.to) {
            const double in = std::min(1.0, (st.lock.frame - cine->prism.from) / 8.0);
            for (std::int32_t k = 1; k <= cine->prism.images; ++k)
                for (const std::int32_t side : {-1, 1}) {
                    FighterState image = drawn;
                    const double sway = 1 + 0.15 * dm::sin(st.lock.frame * 0.11 + k);  // the images breathe a little
                    image.pos.x += Fixed::raw(static_cast<std::int32_t>(std::lround(side * k * cine->prism.spread * sway * Fixed::one_bits)));
                    body(i == 0 ? left : right, match.fighter(i), image, st.frame, scene, view.cam, out, clip, st.lock.frame,
                         fade(cine->prism.tint, in / k));
                }
        }
        body(i == 0 ? left : right, match.fighter(i), drawn, st.frame, scene, view.cam, out, clip, st.lock.frame);
    }
    // The stage's layers in front of the fighters (gone under a cinematic's backdrop).
    if (art) {
        double shown = 1;
        if (cine) {
            const std::int32_t t = st.lock.frame;
            const double in = between(t, cine->backdrop_in[0], cine->backdrop_in[1]);
            const double out_ = cine->backdrop_out[1] > cine->backdrop_out[0] ? 1 - between(t, cine->backdrop_out[0], cine->backdrop_out[1]) : 1.0;
            shown = 1 - in * out_;
        }
        if (shown > 0) stage_layers(*art, true, scene, view.cam, stage(match.system().stage_width) / 2, st.frame, static_cast<float>(shown), out);
    }

    // Cracks across the picture: one for each of the cinematic's hits landed so far.
    if (cine && cine->cracks.on) {
        const MoveDef& md = match.fighter(st.lock.owner).def.moves[static_cast<std::size_t>(st.lock.move)];
        std::int32_t landed = 0;
        if (md.cinematic)
            for (const CinematicHit& h : md.cinematic->hits) landed += h.frame <= st.lock.frame;
        auto unit = [](std::uint64_t z) {
            z = (z + 0x9e3779b97f4a7c15ull) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return static_cast<double>((z ^ (z >> 31)) >> 40) / static_cast<double>(1u << 24);
        };
        for (std::int32_t n = 0; n < landed; ++n) {
            const auto k = static_cast<std::uint64_t>(n) * 64;
            const Point c{scene.width * static_cast<float>(0.12 + 0.76 * unit(k)), scene.height * static_cast<float>(0.12 + 0.7 * unit(k + 1))};
            const int lines = 5 + static_cast<int>(unit(k + 2) * 3);
            for (int l = 0; l < lines; ++l) {
                double a = dm::two_pi * (l + unit(k + 3 + static_cast<std::uint64_t>(l))) / lines;
                Point at = c;
                path_.clear();
                path_.move_to(at);
                for (int seg = 0; seg < 4; ++seg) {
                    a += (unit(k + 16 + static_cast<std::uint64_t>(l * 4 + seg)) - 0.5) * 0.7;
                    const float len = scene.height * static_cast<float>(0.04 + 0.05 * unit(k + 40 + static_cast<std::uint64_t>(l * 4 + seg)));
                    at = {at.x + len * static_cast<float>(dm::cos(a)), at.y + len * static_cast<float>(dm::sin(a))};
                    path_.line_to(at);
                }
                out.stroke_path(path_, {.width = 1.6f, .cap = render::LineCap::round, .join = render::LineJoin::round}, render::Paint::solid(cine->cracks.color));
            }
        }
    }

    // The cinematic's glyph, written stroke by stroke over the defender, and its seal.
    if (cine) {
        const Look& look = st.lock.owner == 0 ? left : right;
        const double fx = st.lock.facing;
        auto world = [&](Point at) { return view.at(stage(st.lock.x) + fx * at.x, stage(st.lock.y) + at.y); };
        if (auto g = look.glyphs.find(cine->glyph); g != look.glyphs.end())
            glyph_by_frames(g->second, cine->glyph_ends, cine->glyph_from, st.lock.frame, world(cine->glyph_at), view.size(cine->glyph_size), cine->glyph_color, out);
        if (cine->seal_frame >= 0 && st.lock.frame >= cine->seal_frame) {
            const Point c = world(cine->seal_at);
            const float half = view.size(cine->seal_size) / 2 * static_cast<float>(0.7 + 0.3 * between(st.lock.frame, cine->seal_frame, cine->seal_frame + 4));
            out.fill_round_rect({c.x - half, c.y - half, 2 * half, 2 * half}, omui::geom::CornerRadii::all(half * 0.12f), render::Paint::solid(cine->seal_color));
            const float w = half * 0.14f;  // a seal's carved lines: a frame and a cross
            for (const omui::geom::Rect r : {omui::geom::Rect{c.x - half * 0.7f, c.y - half * 0.7f, half * 1.4f, w}, omui::geom::Rect{c.x - half * 0.7f, c.y + half * 0.7f - w, half * 1.4f, w},
                                             omui::geom::Rect{c.x - half * 0.7f, c.y - half * 0.7f, w, half * 1.4f}, omui::geom::Rect{c.x + half * 0.7f - w, c.y - half * 0.7f, w, half * 1.4f},
                                             omui::geom::Rect{c.x - w / 2, c.y - half * 0.7f, w, half * 1.4f}, omui::geom::Rect{c.x - half * 0.7f, c.y - w / 2, half * 1.4f, w}})
                out.fill_rect(r, Color::hex(0xf4ece0));
        }
    }

    // Glyphs being written before they fly: in front of who writes them, over the move's frames up to its spawn.
    for (std::int32_t i = 0; i < 2; ++i) {
        const FighterState& f = st.f[static_cast<std::size_t>(i)];
        if (f.mode != Mode::move) continue;
        const MoveDef& m = match.fighter(i).def.moves[static_cast<std::size_t>(f.move)];
        if (!m.projectile || f.move_frame >= m.projectile->spawn) continue;
        const Look& look = i == 0 ? left : right;
        const auto* g = projectile_of(look, m.name);
        if (!g) continue;
        auto strokes = look.glyphs.find(g->glyph);
        if (strokes == look.glyphs.end()) continue;
        const Point at = view.at(stage(f.pos.x + m.projectile->offset.x * f.facing), stage(f.pos.y + m.projectile->offset.y));
        const double written = static_cast<double>(f.move_frame) / m.projectile->spawn;
        if (g->halo.a > 0) glyph(strokes->second, written, at, view.size(g->size), g->halo, out, 2.4f);
        glyph(strokes->second, written, at, view.size(g->size), g->color, out);
    }

    // Projectiles: a glyph the look writes for its move, else a core and a halo.
    for (const ProjectileState& pr : st.p) {
        if (!pr.alive) continue;
        const Point c = view.at(stage(pr.pos.x), stage(pr.pos.y));
        const Look& look = pr.from == 0 ? left : right;  // drawn as its thrower draws it, turned back or not
        const auto& name = match.fighter(pr.from).def.moves[static_cast<std::size_t>(pr.move)].name;
        if (const auto* g = projectile_of(look, name)) {
            if (auto strokes = look.glyphs.find(g->glyph); strokes != look.glyphs.end()) {
                // Flying, the ink bleeds a little more each frame (into its halo, if it has one).
                const double spread = 1.0 + std::min(0.25, pr.frame * 0.004);
                if (g->spin != 0)  // turning about its middle as it flies
                    out.push_transform(omui::geom::Affine::translate(c.x, c.y) * render::rotate(g->spin * pr.frame * dm::pi / 180 * pr.facing) *
                                       omui::geom::Affine::translate(-c.x, -c.y));
                if (g->halo.a > 0) glyph(strokes->second, 1.0, c, view.size(g->size), g->halo, out, 2.4f);
                Color bleed = g->color;
                bleed.a *= 0.18f;
                glyph(strokes->second, 1.0, c, view.size(g->size * static_cast<float>(spread)), bleed, out);
                glyph(strokes->second, 1.0, c, view.size(g->size), g->color, out);
                if (g->spin != 0) out.pop_transform();
                continue;
            }
        }
        path_.clear();
        circle(path_, c, view.size(26));
        out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(Color::hex(0x6fd3ff, 0.35f)));
        path_.clear();
        circle(path_, c, view.size(14));
        out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(Color::hex(0xe9fbff)));
    }

    // Set pieces (G-15): panes of glass, their shine, cracks as they lose durability; an edge of gold on one that turns projectiles back.
    for (std::size_t q = 0; q < st.pieces.size(); ++q) {
        const PieceState& piece = st.pieces[q];
        if (!piece.alive) continue;
        const Look& look = piece.owner == 0 ? left : right;
        const Box& box = match.system().piece_box;
        const double half = std::max(14.0, stage(box.w) / 2), middle = stage(piece.pos.x + box.x) + stage(box.w) / 2;  // drawn a little wider than it meets
        const Point lo = view.at(middle - half, stage(piece.pos.y + box.y));
        const Point hi = view.at(middle + half, stage(piece.pos.y + box.y + box.h));
        const omui::geom::Rect r{std::min(lo.x, hi.x), std::min(lo.y, hi.y), std::abs(hi.x - lo.x), std::abs(hi.y - lo.y)};
        // Fading in over its first frames.
        const double in = std::min(1.0, (st.frame - piece.placed + 1) / 6.0);
        out.fill_rect(r, fade(look.pane.glass, in));
        path_.clear();
        path_.move_to({r.x + r.width * 0.15f, r.y + r.height * 0.55f}).line_to({r.x + r.width * 0.85f, r.y + r.height * 0.3f}).line_to({r.x + r.width * 0.85f, r.y + r.height * 0.38f})
            .line_to({r.x + r.width * 0.15f, r.y + r.height * 0.63f}).close();
        out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(fade(look.pane.shine, in)));
        path_.clear();
        path_.move_to({r.x, r.y}).line_to({r.x + r.width, r.y}).line_to({r.x + r.width, r.y + r.height}).line_to({r.x, r.y + r.height}).close();
        out.stroke_path(path_, {.width = view.size(piece.reflects > 0 ? 3.f : 2.f)},
                        render::Paint::solid(fade(piece.reflects > 0 ? look.pane.reflect : look.pane.edge, in)));
        // Cracks: none whole; four at one left; from a point its slot and placing frame choose.
        const std::int32_t cracks = 4 * std::max(0, 2 - piece.durability);
        auto unit = [&](std::uint32_t i) {
            std::uint64_t z = (static_cast<std::uint64_t>(q) << 40) ^ (static_cast<std::uint64_t>(piece.placed) << 8) ^ i;
            z = (z + 0x9e3779b97f4a7c15ull) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return static_cast<double>((z ^ (z >> 31)) >> 40) / static_cast<double>(1u << 24);
        };
        const Point from{r.x + r.width * static_cast<float>(0.3 + 0.4 * unit(0)), r.y + r.height * static_cast<float>(0.3 + 0.4 * unit(1))};
        for (std::int32_t c = 0; c < cracks; ++c) {
            const double a = dm::two_pi * (c + unit(static_cast<std::uint32_t>(2 + c))) / cracks;
            path_.clear();
            path_.move_to(from);
            Point at = from;
            for (int seg = 0; seg < 3; ++seg) {
                const double turn = a + (unit(static_cast<std::uint32_t>(20 + c * 3 + seg)) - 0.5) * 0.8;
                at = {at.x + r.width * 0.45f * static_cast<float>(dm::cos(turn)), at.y + r.height * 0.12f * static_cast<float>(dm::sin(turn))};
                path_.line_to({std::clamp(at.x, r.x, r.x + r.width), std::clamp(at.y, r.y, r.y + r.height)});
            }
            out.stroke_path(path_, {.width = view.size(1.5f), .cap = render::LineCap::round}, render::Paint::solid(fade(look.pane.edge, in)));
        }
    }

    // Sparks and flashes, each contact as its impact profile says. A spark of
    // a hit while time stands still (a cinematic's stop) stands still a few
    // frames in, and goes on from the bell.
    const std::int32_t stop_from = cine && cine->stop.from >= 0 ? st.frame - st.lock.frame + cine->stop.from : 0;
    const std::int32_t stop_to = cine && cine->stop.from >= 0 ? st.frame - st.lock.frame + cine->stop.to : 0;
    for (const Effects::Kept& k : effects.kept()) {
        const std::int32_t age = st.frame - k.frame;
        const Event& e = k.event;
        if (age < 0 || !contact(e.kind)) continue;
        const Impact& im = impact_for(look_of(e, left, right), e);
        std::int32_t spark_age = age;
        if (k.frame >= stop_from && k.frame < stop_to) {
            // Held in the air: each where its own number puts it about the hit, so the hits hang apart.
            spark_age = std::min(std::min(st.frame, stop_to) - k.frame, 3) + std::max(0, st.frame - stop_to);
            Effects::Kept held = k;
            held.event.at.x += Fixed::raw(static_cast<std::int32_t>(std::lround((scatter(k, 7) - 0.5) * 140 * Fixed::one_bits)));
            held.event.at.y += Fixed::raw(static_cast<std::int32_t>(std::lround((scatter(k, 8) - 0.5) * 140 * Fixed::one_bits)));
            if (spark_age < im.spark_frames) spark(held, im, spark_age, view, path_, out);
        } else if (spark_age < im.spark_frames) {
            spark(k, im, spark_age, view, path_, out);
        }
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

    // A cinematic's shock rings, spreading and fading.
    if (cine)
        for (const auto& r : cine->rings) {
            const std::int32_t age = st.lock.frame - r.frame;
            if (age < 0 || age >= r.frames) continue;
            const double u = static_cast<double>(age) / r.frames;
            const Point c = view.at(stage(st.lock.x) + st.lock.facing * r.at.x, stage(st.lock.y) + r.at.y);
            path_.clear();
            circle(path_, c, view.size(r.size * (0.15 + 0.85 * (1 - (1 - u) * (1 - u)))));
            out.stroke_path(path_, {.width = view.size(10 * (1 - u) + 2)}, render::Paint::solid(fade(r.color, 1 - u)));
        }

    // The picture falling apart: pieces of it, turning, falling and fading.
    if (cine && cine->fall.frame >= 0 && st.lock.frame >= cine->fall.frame) {
        const double t = st.lock.frame - cine->fall.frame;
        auto unit = [](std::uint64_t z) {
            z = (z + 0x9e3779b97f4a7c15ull) * 0xbf58476d1ce4e5b9ull;
            z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
            return static_cast<double>((z ^ (z >> 31)) >> 40) / static_cast<double>(1u << 24);
        };
        const double alpha = std::max(0.0, 1 - t / 30);
        for (std::int32_t n = 0; n < cine->fall.count && alpha > 0; ++n) {
            const auto k = 1000 + static_cast<std::uint64_t>(n) * 8;
            const double size = scene.height * (0.03 + 0.05 * unit(k));
            const double x = scene.width * unit(k + 1), y0 = scene.height * unit(k + 2) * 0.9;
            const double y = y0 + 0.35 * (0.6 + unit(k + 3)) * t * t + t * 2;
            const double spin = (unit(k + 4) - 0.5) * 0.3 * t + unit(k + 5) * dm::two_pi;
            path_.clear();
            for (int v = 0; v < 3; ++v) {
                const double a = spin + v * (dm::two_pi / 3) + (unit(k + 6) - 0.5) * 0.6 * (v == 1);
                const Point p{static_cast<float>(x + size * dm::cos(a)), static_cast<float>(y + size * 0.8 * dm::sin(a))};
                if (v == 0) path_.move_to(p);
                else path_.line_to(p);
            }
            path_.close();
            out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(fade(cine->fall.color, alpha)));
        }
    }

    // A cinematic's screen lines, as its backdrop shows.
    if (cine && cine->scanlines) {
        const std::int32_t t = st.lock.frame;
        const double in = between(t, cine->backdrop_in[0], cine->backdrop_in[1]);
        const double out_ = cine->backdrop_out[1] > cine->backdrop_out[0] ? 1 - between(t, cine->backdrop_out[0], cine->backdrop_out[1]) : 1.0;
        const Color line = Color::hex(0x000000, static_cast<float>(0.22 * in * out_));
        for (float y = static_cast<float>(t % 4); y < scene.height; y += 4) out.fill_rect({0, y, scene.width, 1.5f}, line);
    }

    // A cinematic's flashes.
    if (cine)
        for (const auto& f : cine->flashes) {
            const std::int32_t age = st.lock.frame - f.frame;
            if (age < 0 || age >= f.frames) continue;
            if (f.kind == Flash::white) {
                out.fill_rect({0, 0, scene.width, scene.height}, Color::hex(0xffffff, static_cast<float>(0.8 * (1 - static_cast<double>(age) / f.frames))));
            } else if (f.kind == Flash::invert) {
                out.push_layer(1, render::BlendMode::difference);
                out.fill_rect({0, 0, scene.width, scene.height}, Color::white());
                out.pop_layer();
            }
        }

    // A super's flash: the picture dimmed but for who flashes, a band of their colour, and for a level 3 a close-up in it.
    if (flasher >= 0) {
        const Look& look = flasher == 0 ? left : right;
        const FighterState& f = st.f[static_cast<std::size_t>(flasher)];
        const MoveDef& m = match.fighter(flasher).def.moves[static_cast<std::size_t>(f.move)];
        const double in = between(static_cast<std::int32_t>(flash_progress * 100), 0, 20);
        const double gone = 1 - between(static_cast<std::int32_t>(flash_progress * 100), 85, 100);
        out.fill_rect({0, 0, scene.width, scene.height}, Color::hex(0x05060a, static_cast<float>(0.55 * in * gone)));
        // The band: a slanted strip across the picture, sweeping in.
        const float h = scene.height * 0.34f, y = scene.height * 0.18f, slant = 60;
        const float reach = static_cast<float>(scene.width * (0.2 + 0.8 * in));
        path_.clear();
        path_.move_to({0, y + slant}).line_to({reach, y}).line_to({reach, y + h}).line_to({0, y + h + slant}).close();
        out.fill_path(path_, render::FillRule::nonzero, render::Paint::solid(fade(look.accent, 0.85 * gone)));
        if (m.level >= 3) {
            // The close-up: the fighter three times as large, framed by the band.
            out.push_clip(omui::geom::Rect{0, y + slant * 0.5f, reach, h});  // the band's middle
            const Scene big{.width = scene.width, .height = scene.height, .ground = scene.ground};
            // Centred on the head, which lands in the band's middle.
            Camera c{stage(f.pos.x) - 40 * f.facing, 0, 3.0};
            c.y = stage(f.pos.y) + 178 - (scene.ground - (y + h / 2)) / 3.0;
            body(look, match.fighter(flasher), f, st.frame, big, c, out);
            out.pop_clip();
        }
        // Who flashes, over the dimming.
        body(look, match.fighter(flasher), f, st.frame, scene, view.cam, out);
    }

    // Time standing still: the whole picture grey, toward the stop's tint (its brightness kept).
    if (cine && cine->stop.from >= 0 && st.lock.frame >= cine->stop.from && st.lock.frame < cine->stop.to) {
        const Color tint = cine->stop.tint;
        const float top = std::max({tint.r, tint.g, tint.b, 1e-3f});
        out.recolor(first, out.size(), [&](Color c) {
            const float y = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
            return Color{std::min(1.f, y * tint.r / top), std::min(1.f, y * tint.g / top), std::min(1.f, y * tint.b / top), c.a};
        });
    }
}

}  // namespace gongfu::present2d
